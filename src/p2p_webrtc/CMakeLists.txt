# src/p2p_webrtc/CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

# Find necessary Qt components
find_package(Qt6 REQUIRED COMPONENTS Core WebSockets) 

set(CMAKE_AUTOMOC ON)

add_library(webrtc-client
    webrtcservice.hpp
    webrtcservice.cpp
)

# 3. Link your library to the targets provided by the external library
target_link_libraries(webrtc-client
    PUBLIC
    Qt6::Core
    Qt6::WebSockets 
    p2p          
    LibDataChannel::LibDataChannel
)