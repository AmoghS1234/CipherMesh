cmake_minimum_required(VERSION 3.16)

project(CipherMesh VERSION 1.0.0 LANGUAGES CXX)

# --- NEW: FetchContent Module ---
include(FetchContent)

# Fetch libdatachannel (libdatachannel already bundles its deps in-tree)
FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.23.2 
)
FetchContent_MakeAvailable(LibDataChannel)

# --- END FetchContent ---

add_subdirectory(src)

option(BUILD_TESTS "Build the CipherMesh test suite" OFF)

if(BUILD_TESTS)
    message(STATUS "Building with tests enabled.")
    enable_testing()
    add_subdirectory(tests)
else()
    message(STATUS "Building with tests disabled (default).")
endif()