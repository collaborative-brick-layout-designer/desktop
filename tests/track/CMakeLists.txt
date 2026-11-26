add_executable(bld_track_tests
    TrackNetworkTest.cpp
    RoutesTest.cpp
    TrainsTest.cpp
    CutsTest.cpp
    FitChecksTest.cpp
    PlacesOnTrackTest.cpp
    OpsTest.cpp
    OpsSessionTest.cpp
    AnimationTest.cpp
    StockGuessTest.cpp
)

target_compile_definitions(bld_track_tests PRIVATE
    BLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BLD_PARTS_LIBRARY_ROOT="${CMAKE_SOURCE_DIR}/parts/BlueBrickParts/parts"
)

target_link_libraries(bld_track_tests
    PRIVATE
        bld_track
        bld_import
        bld_saveload
        bld_parts
        bld_core
        GTest::gtest_main
        Qt6::Core
        Qt6::Gui
)

gtest_discover_tests(bld_track_tests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    DISCOVERY_TIMEOUT 60
)
