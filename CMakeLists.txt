cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)   
project(CipherMesh VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

# --- FIX: Stop Install Rule Errors ---
set(CMAKE_SKIP_INSTALL_RULES ON)

if(NOT ANDROID)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
endif()

include(FetchContent)

# --- 1. Libsodium ---
if(ANDROID)
    message(STATUS "Android: Fetching libsodium via Git...")
    set(SODIUM_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        sodium
        GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(sodium)
    set(SODIUM_INCLUDE_DIR "${sodium_SOURCE_DIR}/libsodium/src/libsodium/include")
    if(NOT TARGET libsodium)
        add_library(libsodium ALIAS sodium)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Sodium REQUIRED IMPORTED_TARGET libsodium)
    if(NOT TARGET libsodium)
        add_library(libsodium INTERFACE)
        target_link_libraries(libsodium INTERFACE PkgConfig::Sodium)
    endif()
    set(SODIUM_INCLUDE_DIR ${Sodium_INCLUDE_DIRS})
endif()

# --- 2. OpenSSL ---
if(ANDROID)
    message(STATUS "Android: Fetching OpenSSL...")
    set(WITH_APPS OFF CACHE BOOL "" FORCE)
    set(WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(OPENSSL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        openssl-cmake
        GIT_REPOSITORY https://github.com/janbar/openssl-cmake.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(openssl-cmake)
    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL ALIAS ssl)
    endif()
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto ALIAS crypto)
    endif()
    set(OPENSSL_INCLUDE_DIR "${openssl-cmake_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)
    set(OpenSSL_FOUND TRUE CACHE BOOL "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY crypto CACHE FILEPATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY ssl CACHE FILEPATH "" FORCE)
    set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto CACHE INTERNAL "" FORCE)
else()
    find_package(OpenSSL REQUIRED)
endif()

# --- 3. LibDataChannel ---
set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(USE_OPENSSL ON CACHE BOOL "" FORCE)
set(USE_NICE OFF CACHE BOOL "" FORCE)

# Enable WebSockets for Signaling
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)

set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.22.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(LibDataChannel)

# --- 3.1 nlohmann_json ---
message(STATUS "Fetching nlohmann_json...")
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# --- 4. SQLite / Qt ---
if(NOT ANDROID)
    # [FIX] Core Sql is REQUIRED for database.hpp
    find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Concurrent Quick WebSockets Widgets Svg)
endif()

if(ANDROID)
    message(STATUS "Android: Fetching SQLite3 Source...")
    FetchContent_Declare(
        sqlite3_src
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
    )
    FetchContent_GetProperties(sqlite3_src)
    if(NOT sqlite3_src_POPULATED)
        FetchContent_Populate(sqlite3_src)
    endif()
    set(SQLITE_ROOT "${sqlite3_src_SOURCE_DIR}")
    if(EXISTS "${SQLITE_ROOT}/sqlite3.c")
        set(SQLITE_SOURCE "${SQLITE_ROOT}/sqlite3.c")
    elseif(EXISTS "${SQLITE_ROOT}/sqlite-amalgamation-3450100/sqlite3.c")
        set(SQLITE_ROOT "${SQLITE_ROOT}/sqlite-amalgamation-3450100")
        set(SQLITE_SOURCE "${SQLITE_ROOT}/sqlite3.c")
    endif()
    if(NOT TARGET sqlite3)
        add_library(sqlite3 STATIC "${SQLITE_SOURCE}")
        target_include_directories(sqlite3 PUBLIC "${SQLITE_ROOT}")
        target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_TEMP_STORE=3)
    endif()
    set(ANDROID_SQLITE_INCLUDE_DIR "${SQLITE_ROOT}")
else()
    find_package(SQLite3 REQUIRED)
    if(NOT TARGET sqlite3)
        add_library(sqlite3 INTERFACE)
        target_link_libraries(sqlite3 INTERFACE SQLite::SQLite3)
    endif()
endif()

# --- 5. Core Library ---
set(CORE_SOURCES
    src/core/crypto.cpp
    src/core/vault.cpp
    src/core/database.cpp
    src/utils/totp.cpp
    src/utils/passwordstrength.cpp
    src/p2p/ip2pservice.cpp
    src/p2p_webrtc/webrtcservice.cpp
)

add_library(ciphermesh-core STATIC ${CORE_SOURCES})

target_include_directories(ciphermesh-core PUBLIC
    src/core
    src/p2p
    src/p2p_webrtc
    src/utils
    ${SODIUM_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
)

if(ANDROID)
    target_include_directories(ciphermesh-core PRIVATE "${ANDROID_SQLITE_INCLUDE_DIR}")
endif()

target_link_libraries(ciphermesh-core PUBLIC
    LibDataChannel::LibDataChannelStatic
)

target_link_libraries(ciphermesh-core PRIVATE
    libsodium
    OpenSSL::SSL
    OpenSSL::Crypto
    sqlite3
)

if(ANDROID)
    target_link_libraries(ciphermesh-core PRIVATE log)
else()
    # [CRITICAL FIX] Changed PRIVATE to PUBLIC. 
    # Because database.hpp includes <QSqlDatabase>, consumers of this library 
    # (like Desktop) MUST inherit the Qt6::Sql include paths.
    target_link_libraries(ciphermesh-core PUBLIC
        Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent Qt6::WebSockets
    )
endif()

# --- 6. Targets ---
if(ANDROID)
    add_library(ciphermesh_jni SHARED src/mobile/app/src/main/cpp/native-lib.cpp)
    
    target_link_options(ciphermesh_jni PRIVATE 
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
    )
    
    target_link_libraries(ciphermesh_jni PRIVATE ciphermesh-core log android)
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/main.cpp")
        file(GLOB_RECURSE DESKTOP_SOURCES "src/desktop/*.cpp" "src/desktop/*.hpp" "src/desktop/*.h")
        
        qt_add_executable(CipherMesh-Desktop ${DESKTOP_SOURCES})
        
        target_link_libraries(CipherMesh-Desktop PRIVATE
            ciphermesh-core
            Qt6::Quick Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Widgets Qt6::Svg
            LibDataChannel::LibDataChannelStatic libsodium OpenSSL::SSL OpenSSL::Crypto sqlite3
        )
    endif()

    # Build Vault Service (Native Messaging Host)
    add_subdirectory(extensions/vault-service)
endif()