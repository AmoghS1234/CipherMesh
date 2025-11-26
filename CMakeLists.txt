cmake_minimum_required(VERSION 3.16)

project(CipherMesh VERSION 1.0.0 LANGUAGES CXX)

# --- NEW: FetchContent Module ---
include(FetchContent)

# Fetch libdatachannel (libdatachannel already bundles its deps in-tree)
# This also brings in nlohmann/json as a dependency
FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.23.2 
)
FetchContent_MakeAvailable(LibDataChannel)

# Use the nlohmann_json that comes with libdatachannel instead of fetching separately
# This avoids the target name conflict
if(NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()

# --- END FetchContent ---

add_subdirectory(src)

# Build vault-service for browser extensions (standalone, no desktop app required)
add_subdirectory(extensions/vault-service)

option(BUILD_TESTS "Build the CipherMesh test suite" OFF)

if(BUILD_TESTS)
    message(STATUS "Building with tests enabled.")
    enable_testing()
    add_subdirectory(tests)
else()
    message(STATUS "Building with tests disabled (default).")
endif()