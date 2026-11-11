cmake_minimum_required(VERSION 3.25)

# Newer CMake behaviour where it is available, so the build prints no
# warnings: CMP0156 lets Apple's linker see each static library once
# (otherwise ld warns "ignoring duplicate libraries"), CMP0177 normalizes
# install() destinations like ".".
foreach(_bld_policy CMP0156 CMP0177)
    if(POLICY ${_bld_policy})
        cmake_policy(SET ${_bld_policy} NEW)
    endif()
endforeach()

# Single source of truth for the version stamped into the binaries and every
# package. Release CI overrides it from the git tag (-DBLD_VERSION=1.2.3);
# nightly CI stamps the latest release plus its run number
# (-DBLD_VERSION=1.2.3.456 -DBLD_CHANNEL=nightly), so a nightly is newer
# than the release it follows and older than the next one.
set(BLD_VERSION "2.1.3" CACHE STRING "Version stamped into binaries and packages (X.Y.Z or X.Y.Z.N)")
set(BLD_CHANNEL "" CACHE STRING "Build channel shown in About: empty for a release, nightly for a nightly build")

# The oldest macOS the app runs on. Qt's own frameworks set the floor: Qt
# 6.12's are built for macOS 14.4 (their LC_BUILD_VERSION minos), and dyld
# refuses an app on an older system before it can say anything. Info.plist's
# LSMinimumSystemVersion says the same, so Finder explains instead; CI checks
# no bundled binary needs a newer macOS (scripts/ci/check-macos-minimum.sh).
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "Oldest macOS the app runs on (Qt 6.12's minimum)")

project(BrickLayoutDesigner
    VERSION ${BLD_VERSION}
    DESCRIPTION "Cross-platform maintainer fork of BlueBrick"
    LANGUAGES C CXX)

# The fourth number of a nightly's version, 0 for a release (Windows'
# VERSIONINFO always has four).
if(PROJECT_VERSION_TWEAK STREQUAL "")
    set(BLD_VERSION_TWEAK 0)
else()
    set(BLD_VERSION_TWEAK ${PROJECT_VERSION_TWEAK})
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# WebSockets (src/sync) is found here, not in src/sync, so the app's Qt
# finalizer in src/app can see the target bld_sync links.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport WebSockets)
find_package(Qt6 6.8 QUIET COMPONENTS LinguistTools)
# Tests and fuzz seeds write ZIPs with Qt's private QZipWriter (the app reads
# them with src/import/zip/SafeZip and needs no private Qt). CorePrivate is a
# package of its own from Qt 6.9; Qt 6.8 defines the target with Core.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 QUIET COMPONENTS CorePrivate)
if(NOT TARGET Qt6::CorePrivate)
    message(FATAL_ERROR "Qt6::CorePrivate (Qt's private headers) not found")
endif()
qt_standard_project_setup()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/permissive-)
endif()

# Warnings: our own targets (src/, tests/, fuzz/) build warning-free at
# -Wall -Wextra -Wpedantic (/W4 on MSVC), and CI turns warnings into errors.
# A git checkout does the same by default, so a warning stops the build here
# and not a round later in CI. A source archive (no .git: a packager's or a
# user's build) keeps them as warnings, so a newer compiler's new warnings
# never stop that build. A build folder configured earlier keeps the value
# in its cache; -DBLD_WARNINGS_AS_ERRORS=ON or =OFF sets it either way.
# Fetched dependencies (googletest, QtKeychain, yrs) and vendored code marked
# BLD_THIRD_PARTY (puff.c) keep their own flags.
if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    set(_bld_werror_default ON)
else()
    set(_bld_werror_default OFF)
endif()
option(BLD_WARNINGS_AS_ERRORS "Treat compiler warnings in our own targets as errors" ${_bld_werror_default})
function(bld_apply_warnings dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        get_target_property(_third_party ${_t} BLD_THIRD_PARTY)
        if(_third_party OR NOT _type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            continue()
        endif()
        if(MSVC)
            target_compile_options(${_t} PRIVATE /W4 $<$<BOOL:${BLD_WARNINGS_AS_ERRORS}>:/WX>)
        else()
            target_compile_options(${_t} PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${BLD_WARNINGS_AS_ERRORS}>:-Werror>)
        endif()
    endforeach()
    # Only our own directories: FetchContent's live under the build tree.
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subdirs)
        cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${_sub}" NORMALIZE _ours)
        if(_ours)
            bld_apply_warnings("${_sub}")
        endif()
    endforeach()
endfunction()

include(CTest)
if(WIN32)
    # Flat layout: Qt's deploy script puts the Qt DLLs and qt.conf in
    # CMAKE_INSTALL_BINDIR, so it must be the exe's own folder.
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executable install directory")
endif()
include(GNUInstallDirs)

# ----- Install layout ---------------------------------------------------------
# `cmake --install` produces a runnable tree on every platform; CI packages it
# as-is. The runtime probes in src/app/main.cpp (translations) and
# MainWindow::defaultVendoredPartsRoot (parts library) mirror these paths.
#   Windows: <prefix>/brick-layout-designer.exe, <prefix>/{parts,translations}
#   macOS:   <prefix>/brick-layout-designer.app, Contents/Resources/{parts,translations}
#   Linux:   <prefix>/bin, <prefix>/share/brick-layout-designer/{parts,translations}
if(WIN32)
    set(BLD_INSTALL_BINDIR  ".")
    set(BLD_INSTALL_DATADIR ".")
elseif(APPLE)
    set(BLD_INSTALL_BINDIR  ".")
    set(BLD_INSTALL_DATADIR "brick-layout-designer.app/Contents/Resources")
else()
    set(BLD_INSTALL_BINDIR  "${CMAKE_INSTALL_BINDIR}")
    set(BLD_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/brick-layout-designer")
endif()

# Bundle the Qt runtime (libs + plugins + qt.conf + MSVC runtime) into the
# install tree via windeployqt / macdeployqt. Linux bundling is done by
# linuxdeploy in CI instead: Qt's generic Linux deployer also sweeps in glibc
# and the dynamic loader, which breaks the result on other distros.
if(WIN32 OR APPLE)
    option(BLD_DEPLOY_QT "Bundle the Qt runtime into the install tree" ON)
endif()
option(BLD_INSTALL_PARTS "Install the vendored BlueBrickParts library" ON)
# The server features (src/sync: live layouts, server modules, the catalog)
# are always built. They read the shared layout documents with yrs, a Rust
# library, so building needs a Rust toolchain (cargo).
find_program(BLD_CARGO cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")
if(NOT BLD_CARGO)
    message(FATAL_ERROR
        "Rust (cargo) was not found.\n"
        "Collaborative Brick Layout Designer's server features (live layouts, server modules, the catalog) "
        "read shared layouts with yrs, a Rust library, so building needs a Rust toolchain.\n"
        "Install it with rustup from https://rustup.rs (on Windows pick the MSVC toolchain), "
        "open a new terminal so cargo is on PATH, and configure again.")
endif()

# compile_commands.json for clang-tidy and editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fuzzing (fuzz/README.md): clang only; instruments everything.
option(BLD_FUZZ "Build the libFuzzer parser harnesses (clang)" OFF)
if(BLD_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "BLD_FUZZ needs clang (libFuzzer)")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src)
if(BLD_FUZZ)
    add_subdirectory(fuzz)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
bld_apply_warnings("${CMAKE_SOURCE_DIR}")

# ----- Localization -----------------------------------------------------------
# Base English is implicit (Qt returns the tr() argument when no catalogue is
# loaded). Supported target languages match BlueBrick's shipped set plus a
# few we want to cover going forward. Translators populate the .ts files via
# `lupdate`; `lrelease` compiles them to .qm which the build installs next to
# the binary. See docs/TRANSLATING.md.
# "en" holds only English's plurals ("1 part", "3 parts"; see
# scripts/fill-english-plurals.py).
set(BLD_LANGUAGES en fr de es it nl pt sv no zh ja)

set(BLD_TS_FILES "")
foreach(_lang IN LISTS BLD_LANGUAGES)
    list(APPEND BLD_TS_FILES "${CMAKE_SOURCE_DIR}/translations/bld_${_lang}.ts")
endforeach()

# Only wire the binaries when every .ts file exists so a fresh clone before
# any translations are stubbed out still builds.
set(BLD_HAVE_TS TRUE)
foreach(_ts IN LISTS BLD_TS_FILES)
    if(NOT EXISTS "${_ts}")
        set(BLD_HAVE_TS FALSE)
    endif()
endforeach()
if(BLD_HAVE_TS AND TARGET bld_app AND Qt6LinguistTools_FOUND)
    qt_add_translations(bld_app
        TS_FILES ${BLD_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE BLD_QM_FILES)
    install(FILES ${BLD_QM_FILES} DESTINATION "${BLD_INSTALL_DATADIR}/translations" OPTIONAL)
    # The Translations test reads bld_en.qm (English's plurals).
    if(TARGET bld_ui_tests AND TARGET release_translations)
        add_dependencies(bld_ui_tests release_translations)
    endif()
endif()

# Qt's own words (its dialogs' buttons) in the UI languages. windeployqt
# writes them as qt_<code>.qm beside ours and Linux packages read them from
# the bundled Qt, but macdeployqt brings none: copy them into the bundle.
# Qt has Portuguese as pt_BR/pt_PT, Chinese as zh_CN/zh_TW, and Norwegian as
# nn (core/QtCatalogs.cpp picks the right one at start).
if(APPLE)
    set(_bld_qt_translations "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    set(_bld_qt_qm "")
    foreach(_code fr de nl pt_BR pt_PT es it nb nn sv zh_CN zh_TW ja)
        if(EXISTS "${_bld_qt_translations}/qtbase_${_code}.qm")
            list(APPEND _bld_qt_qm "${_bld_qt_translations}/qtbase_${_code}.qm")
        endif()
    endforeach()
    if(_bld_qt_qm)
        install(FILES ${_bld_qt_qm} DESTINATION "${BLD_INSTALL_DATADIR}/translations")
    else()
        message(WARNING "No Qt translations in ${_bld_qt_translations}: the app's Qt dialogs stay in English")
    endif()
endif()

# ----- Data files -------------------------------------------------------------
if(BLD_INSTALL_PARTS AND EXISTS "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts")
    # The upstream library keeps its maintainer's Windows packaging
    # scripts (CreatePackage.bat) beside the parts; ship only the parts.
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
            DESTINATION "${BLD_INSTALL_DATADIR}/parts/BlueBrickParts"
            PATTERN "*.bat" EXCLUDE
            PATTERN "*.cmd" EXCLUDE)
    install(FILES "${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/README.md"
            DESTINATION "${BLD_INSTALL_DATADIR}/parts/BlueBrickParts")
endif()

# BlueBrick's manual as offline HTML (Help > Contents), made from its .chm
# by scripts/help/chm_to_html.py.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/help" DESTINATION "${BLD_INSTALL_DATADIR}")

# THIRD-PARTY-NOTICES.txt: the licenses of what the app links and embeds
# (scripts/gen-third-party-notices.py). Help > About links to it, looking in
# these places (core/Version.cpp, thirdPartyNoticesUrl).
if(WIN32)
    install(FILES README.md THIRD-PARTY-NOTICES.txt DESTINATION .)
    install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
elseif(APPLE)
    install(FILES README.md LICENSE THIRD-PARTY-NOTICES.txt DESTINATION "${BLD_INSTALL_DATADIR}")
else()
    install(FILES README.md LICENSE THIRD-PARTY-NOTICES.txt DESTINATION "${CMAKE_INSTALL_DOCDIR}")
    install(FILES packaging/linux/brick-layout-designer.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES packaging/linux/brick-layout-designer.xml
            DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
    # The app icon at every size, each rendered at exactly that size, and
    # the SVG it comes from (packaging/icons/source/build-icons.sh).
    foreach(_px 16 24 32 48 64 128 512)
        install(FILES packaging/icons/png/brick-layout-designer-${_px}.png
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_px}x${_px}/apps"
                RENAME brick-layout-designer.png)
    endforeach()
    install(FILES packaging/icons/brick-layout-designer.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps")
    install(FILES packaging/icons/source/logo.svg
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME brick-layout-designer.svg)
endif()
