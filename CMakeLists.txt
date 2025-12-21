# --- CRITICAL FIXES FOR ANDROID NDK + LIBDATACHANNEL ---
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
cmake_minimum_required(VERSION 3.16)

project(CipherMesh VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. FORCE VARIABLES ---
set(NO_LOGGING ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)

cmake_policy(SET CMP0077 NEW)
add_compile_definitions(MBEDTLS_SSL_DTLS_SRTP)

# --- 2. Fetch Dependencies ---
include(FetchContent)

# A. MbedTLS
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mbedtls)

# 🔑 [THE CRITICAL BRIDGE] Force FindMbedTLS to succeed using absolute FILEPATHs
set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MbedTLS_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MbedX509_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MbedCrypto_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)

# B. Libsodium
FetchContent_Declare(
    sodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG stable
    GIT_SHALLOW TRUE
)
set(SODIUM_LIBRARY_MINIMAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sodium)

# C. LibDataChannel (Will now find MbedTLS correctly via variables above)
FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.22.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(LibDataChannel)

# --- 3. Archive Path Variables (For final linking) ---
set(SODIUM_STATIC_LIB "${sodium_BINARY_DIR}/src/libsodium/.libs/liblibsodium.a")
set(MBEDTLS_STATIC_LIB "${MbedTLS_LIBRARY}")
set(MBEDX509_STATIC_LIB "${MbedX509_LIBRARY}")
set(MBEDCRYPTO_STATIC_LIB "${MbedCrypto_LIBRARY}")

# --- 4. Find Qt Packages ---
find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Concurrent Quick WebSockets)

# --- 5. Libsodium Compatibility Headers ---
set(SODIUM_COMPAT_DIR "${CMAKE_BINARY_DIR}/sodium_compat")
file(MAKE_DIRECTORY "${SODIUM_COMPAT_DIR}/sodium")
file(WRITE "${SODIUM_COMPAT_DIR}/sodium/export.h" "#define SODIUM_EXPORT\n#define SODIUM_EXPORT_WEAK\n")
file(WRITE "${SODIUM_COMPAT_DIR}/sodium/version.h" "#define SODIUM_VERSION_STRING \"1.0.18\"\n")

# --- 6. Core Library ---
add_library(ciphermesh-core STATIC
    src/core/crypto.cpp
    src/core/vault.cpp
    src/core/database.cpp
    src/p2p/ip2pservice.cpp
    src/p2p_webrtc/webrtcservice.cpp
)

target_include_directories(ciphermesh-core SYSTEM BEFORE PUBLIC ${SODIUM_COMPAT_DIR})
target_include_directories(ciphermesh-core PUBLIC
    src/core src/p2p src/p2p_webrtc
    ${sodium_SOURCE_DIR}/src/libsodium/include
    ${libdatachannel_SOURCE_DIR}/include
    ${MbedTLS_INCLUDE_DIR}
)

target_compile_options(ciphermesh-core PRIVATE -Wno-macro-redefined)

# Link using absolute paths to bypass NDK target-visibility issues
target_link_libraries(ciphermesh-core PRIVATE
    Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent Qt6::WebSockets
    LibDataChannel::LibDataChannelStatic
    "${SODIUM_STATIC_LIB}"
    "${MBEDTLS_STATIC_LIB}"
    "${MBEDX509_STATIC_LIB}"
    "${MBEDCRYPTO_STATIC_LIB}"
)

# --- 7. Android Build ---
if(ANDROID)
    qt_add_executable(CipherMesh-Mobile MANUAL_FINALIZATION WIN32 MACOSX_BUNDLE
        src/mobile/main_mobile.cpp
        src/mobile/vaultqmlwrapper.cpp
        src/mobile/qml.qrc
    )

    target_link_libraries(CipherMesh-Mobile PRIVATE
        ciphermesh-core
        -Wl,--whole-archive
        "${SODIUM_STATIC_LIB}"
        "${MBEDTLS_STATIC_LIB}"
        "${MBEDX509_STATIC_LIB}"
        "${MBEDCRYPTO_STATIC_LIB}"
        -Wl,--no-whole-archive
        LibDataChannel::LibDataChannelStatic
        Qt6::Quick Qt6::Core Qt6::Network Qt6::WebSockets
    )

    set_target_properties(CipherMesh-Mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
    )
    qt_finalize_executable(CipherMesh-Mobile)
endif()

# --- 8. Desktop Build ---
if(NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/main.cpp")
    add_executable(CipherMesh-Desktop
        src/desktop/main.cpp
        src/desktop/mainwindow.cpp
    )
    target_link_libraries(CipherMesh-Desktop PRIVATE ciphermesh-core Qt6::Widgets)
endif()