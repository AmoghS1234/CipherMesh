cmake_minimum_required(VERSION 3.16)

# Utils library - platform-independent utilities
add_library(ciphermesh-utils STATIC
    passwordstrength.cpp
)

target_include_directories(ciphermesh-utils
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# No external dependencies needed for utils
