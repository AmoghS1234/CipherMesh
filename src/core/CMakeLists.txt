cmake_minimum_required(VERSION 3.16)

# ======================
# 1. Dependencies
# ======================

# Qt6 Core (for SHA-1 in breach detection)
find_package(Qt6 REQUIRED COMPONENTS Core)

# libsodium
if(WIN32)
    find_package(unofficial-sodium REQUIRED CONFIG)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED libsodium)
endif()

# SQLite3
if(WIN32)
    # vcpkg uses builtin FindSQLite3, no CONFIG
    find_package(SQLite3 REQUIRED)
else()
    find_package(SQLite3 REQUIRED)
endif()

add_library(ciphermesh-core
    vault.cpp
    crypto.cpp
    database.cpp
)

# ======================
# 2. Includes
# ======================
if(WIN32)
    target_include_directories(ciphermesh-core
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )
else()
    target_include_directories(ciphermesh-core
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        PRIVATE
            ${SQLite3_INCLUDE_DIRS}
            ${SODIUM_INCLUDE_DIRS}
    )
endif()

# ======================
# 3. Linking
# ======================
if(WIN32)
    target_link_libraries(ciphermesh-core
        PUBLIC
            unofficial-sodium::sodium
            SQLite::SQLite3
            Qt6::Core
    )
else()
    target_link_libraries(ciphermesh-core
        PUBLIC
            ${SODIUM_LIBRARIES}
            SQLite::SQLite3
            Qt6::Core
    )
endif()
