# --- CRITICAL FIXES FOR ARCH LINUX & ANDROID ---
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
# -----------------------------------------------

cmake_minimum_required(VERSION 3.24) # Bumped to 3.24 for LINK_LIBRARY support

project(CipherMesh VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. FORCE VARIABLES ---
set(NO_LOGGING ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)

# Prevent shared libs for dependencies (forces .a static libs)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

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

# --- [FIX] Target Normalization Safety Net ---
# Ensure the target 'sodium' always exists, regardless of what FetchContent named it.
if(NOT TARGET sodium AND TARGET libsodium)
    message(STATUS "Aliasing target 'libsodium' to 'sodium'")
    add_library(sodium ALIAS libsodium)
elseif(TARGET sodium)
    message(STATUS "Target 'sodium' exists and will be used.")
else()
    message(FATAL_ERROR "Libsodium target not found! Checked 'sodium' and 'libsodium'.")
endif()

# --- [FIX] Header Compatibility Layer (Kept for version.h/export.h) ---
set(SODIUM_COMPAT_DIR "${CMAKE_BINARY_DIR}/sodium_compat")
file(MAKE_DIRECTORY "${SODIUM_COMPAT_DIR}/sodium")

if(EXISTS "${sodium_BINARY_DIR}/version.h")
    file(COPY "${sodium_BINARY_DIR}/version.h" DESTINATION "${SODIUM_COMPAT_DIR}/sodium")
else()
    if(EXISTS "${sodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in")
        set(VERSION "1.0.18")
        set(SODIUM_LIBRARY_VERSION_MAJOR 10)
        set(SODIUM_LIBRARY_VERSION_MINOR 3)
        configure_file(
            "${sodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in"
            "${SODIUM_COMPAT_DIR}/sodium/version.h"
        )
    endif()
endif()

if(NOT EXISTS "${SODIUM_COMPAT_DIR}/sodium/export.h")
    file(WRITE "${SODIUM_COMPAT_DIR}/sodium/export.h" 
        "#ifndef SODIUM_EXPORT\n#define SODIUM_EXPORT\n#endif\n#ifndef SODIUM_EXPORT_WEAK\n#define SODIUM_EXPORT_WEAK\n#endif\n"
    )
endif()
# -------------------------------------------------------------------

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

target_include_directories(ciphermesh-core SYSTEM BEFORE PUBLIC
    ${SODIUM_COMPAT_DIR}
)

target_include_directories(ciphermesh-core PUBLIC
    src/core
    src/p2p
    src/p2p_webrtc
    ${sodium_SOURCE_DIR}/src/libsodium/include
    ${libdatachannel_SOURCE_DIR}/include
    ${mbedtls_SOURCE_DIR}/include
)

# Optional: Silence warnings
target_compile_options(ciphermesh-core PRIVATE -Wno-macro-redefined)

# Standard linking for the static core library
# No generator expressions needed here (Static -> Static)
target_link_libraries(ciphermesh-core PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::WebSockets
    
    sodium 
    mbedtls
    mbedcrypto
    mbedx509
    LibDataChannel::LibDataChannelStatic
)

# --- 5. Android / Mobile Build ---
if(ANDROID)
    qt_add_executable(CipherMesh-Mobile MANUAL_FINALIZATION WIN32 MACOSX_BUNDLE
        src/mobile/main_mobile.cpp
        src/mobile/vaultqmlwrapper.cpp
        src/mobile/qml.qrc
    )

    target_include_directories(CipherMesh-Mobile PRIVATE src/mobile)

    # ✅ THE CANONICAL FIX: 
    # Use $<LINK_LIBRARY:WHOLE_ARCHIVE,target> to handle paths + whole-archive automatically.
    # This works because we ensured the 'sodium' target exists above.
    target_link_libraries(CipherMesh-Mobile PRIVATE
        ciphermesh-core
        
        $<LINK_LIBRARY:WHOLE_ARCHIVE,sodium>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,mbedtls>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,mbedcrypto>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,mbedx509>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,LibDataChannel::LibDataChannelStatic>

        Qt6::Quick
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
    )

    set_target_properties(CipherMesh-Mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
    )

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