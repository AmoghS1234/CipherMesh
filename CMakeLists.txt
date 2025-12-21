# --- CRITICAL FIXES FOR ARCH LINUX & ANDROID ---
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
# -----------------------------------------------

cmake_minimum_required(VERSION 3.16)

project(CipherMesh VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. FORCE VARIABLES ---
set(NO_LOGGING ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_OPENSSL OFF CACHE BOOL "" FORCE)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)

cmake_policy(SET CMP0077 NEW)

# --- [FIX] Force MbedTLS to enable DTLS-SRTP ---
add_compile_definitions(MBEDTLS_SSL_DTLS_SRTP)
# -----------------------------------------------

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

# Bridge variables
set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MbedTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MbedCrypto_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(MbedX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MbedTLS_LIBRARIES mbedtls mbedcrypto mbedx509 CACHE STRING "" FORCE)

# B. Libsodium
FetchContent_Declare(
    sodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG stable
    GIT_SHALLOW TRUE
)
set(SODIUM_LIBRARY_MINIMAL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sodium)

# C. LibDataChannel
FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.22.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(LibDataChannel)

# --- 3. Find Qt Packages ---
find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Concurrent Quick WebSockets)

# --- 4. Core Library ---
add_library(ciphermesh-core STATIC
    src/core/crypto.cpp
    src/core/vault.cpp
    src/core/database.cpp
    src/p2p/ip2pservice.cpp
    src/p2p_webrtc/webrtcservice.cpp
)

target_include_directories(ciphermesh-core PUBLIC
    src/core
    src/p2p
    src/p2p_webrtc
    ${sodium_SOURCE_DIR}/src/libsodium/include
    ${sodium_BINARY_DIR}                        # <--- FIX: Finds generated version.h
    ${sodium_BINARY_DIR}/src/libsodium/include  # <--- FIX: Backup path
    ${libdatachannel_SOURCE_DIR}/include
    ${mbedtls_SOURCE_DIR}/include
)

target_link_libraries(ciphermesh-core PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::WebSockets
    sodium
    LibDataChannel::LibDataChannelStatic
    mbedtls
    mbedcrypto
    mbedx509
)

# --- 5. Android / Mobile Build ---
# --- 5. Android / Mobile Build ---
# --- 5. Android / Mobile Build ---
if(ANDROID)
    # CHANGE: Use qt_add_executable with MANUAL_FINALIZATION
    qt_add_executable(CipherMesh-Mobile MANUAL_FINALIZATION WIN32 MACOSX_BUNDLE
        src/mobile/main_mobile.cpp
        src/mobile/vaultqmlwrapper.cpp
        src/mobile/qml.qrc
    )

    target_include_directories(CipherMesh-Mobile PRIVATE src/mobile)

    target_link_libraries(CipherMesh-Mobile PRIVATE
        ciphermesh-core
        Qt6::Quick
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
    )

    set_target_properties(CipherMesh-Mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
    )

    # This line will now work because of MANUAL_FINALIZATION above
    qt_finalize_executable(CipherMesh-Mobile)
endif()

# --- 6. Desktop Build ---
if(NOT ANDROID)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/totp.cpp")
        add_executable(CipherMesh-Desktop
            src/desktop/main.cpp
            src/desktop/mainwindow.cpp
            src/desktop/totp.cpp 
        )
        target_link_libraries(CipherMesh-Desktop PRIVATE
            ciphermesh-core
            Qt6::Widgets
        )
    endif()
endif()