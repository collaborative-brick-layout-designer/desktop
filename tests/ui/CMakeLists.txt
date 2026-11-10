# QTest drives the help buttons (keyboard focus, clicks, Esc).
find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

add_executable(bld_ui_tests
    MapViewTest.cpp
    LiveRedrawTest.cpp
    RotateOracleTest.cpp
    ImportPipelineTest.cpp
    ImportReplaceTest.cpp
    AddPartsFromZipTest.cpp
    LDrawMapTest.cpp
    BrickPlacementTest.cpp
    ConnectivityTest.cpp
    FlexMoveTest.cpp
    UpdateCheckTest.cpp
    QtCatalogsTest.cpp
    QtCatalogsShotsTest.cpp
    FuzzRegressionTest.cpp
    VenueLibraryPanelTest.cpp
    VenueDesignerDialogTest.cpp
    PartDifferencesDialogTest.cpp
    MainWindowFileTest.cpp
    ReviewShotsA7Test.cpp
    FileSafetyTest.cpp
    DamagedFilesTest.cpp
    ReadmeShotsTest.cpp
    ThemeTest.cpp
    SettingsDialogTest.cpp
    HelpButtonTest.cpp
    HelpTextsTest.cpp
    SavedViewsTest.cpp
    ViewsPanelTest.cpp
    PanelDockingTest.cpp
    RenderParityTest.cpp
    LoadingCardTest.cpp
    LayerPanelTest.cpp
    ActiveSheetTest.cpp
    ActiveSheetShotsTest.cpp
    ThemeSwitchTest.cpp
    ToursTest.cpp
    RecentFilesTest.cpp
    TouchTest.cpp
    PartsPacksTest.cpp
    PartsIconSizeTest.cpp
    PartsTooltipTest.cpp
    UiWordingTest.cpp
    SnapFeelTest.cpp
    GridSnapTest.cpp
    SetDragTest.cpp
    SetUpdatesTest.cpp
    ModuleLookTest.cpp
    ModuleEditTest.cpp
    ModuleSheetsTest.cpp
    ConfirmDialogTest.cpp
    NameMigrationTest.cpp
    BrandNameTest.cpp
    OwnSetsTest.cpp
    RulerDialogTest.cpp
    DialogKeyboardTest.cpp
    AppIconTest.cpp
    PlainWordsTest.cpp
    PlainWordsShotsTest.cpp
    MenuKeysTest.cpp
    ReviewShotsTest.cpp
    SettingsApplyTest.cpp
    ShellTest.cpp
    TranslationsTest.cpp
)

target_compile_definitions(bld_ui_tests PRIVATE
    BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BLD_BINARY_DIR="${CMAKE_BINARY_DIR}"
    BLD_BBM_CORPUS_DIR="${CMAKE_SOURCE_DIR}/fixtures/bbm-corpus"
    BLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
)

target_link_libraries(bld_ui_tests
    PRIVATE
        bld_ui
        bld_import
        bld_edit
        bld_saveload
        bld_parts
        bld_core
        GTest::gtest
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Test
)

gtest_discover_tests(bld_ui_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DISCOVERY_TIMEOUT 60
)
