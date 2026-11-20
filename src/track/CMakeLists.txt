# The track network (trains design): which connections are track, the ways
# through each part, switches, and the network of a map. Derived from the
# bricks and the parts library; nothing here is saved.
add_library(bld_track STATIC
    CircuitPath.cpp
    CircuitPath.h
    PartTrack.cpp
    PartTrack.h
    TrackNetwork.cpp
    TrackNetwork.h
)

target_include_directories(bld_track PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_track PUBLIC bld_core bld_parts Qt6::Core Qt6::Gui)

# The shared contracts' tables (fixtures/contracts/track-kinds.json and
# track-paths.json, word for word; TrackNetworkTest checks they match).
qt_add_resources(bld_track "bld_track_data"
    PREFIX "/bld/track"
    BASE data
    FILES data/track-kinds.json data/track-paths.json
)
