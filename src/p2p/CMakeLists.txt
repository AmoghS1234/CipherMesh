# src/p2p/CMakeLists.txt

# This is a pure C++ library.
add_library(p2p
    ip2pservice.hpp
    ip2pservice.cpp
)

# CRITICAL FIX: Link against the core library.
# "PUBLIC" ensures that any target linking 'p2p' also gets 'ciphermesh-core's include paths.
target_link_libraries(p2p
    PUBLIC
    ciphermesh-core 
)

# Export the header path so other targets can use this library
target_include_directories(p2p PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)