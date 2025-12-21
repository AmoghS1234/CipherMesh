# --- FINAL PRODUCTION-READY ANDROID + AUTOTOOLS BUILD ---
set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
cmake_minimum_required(VERSION 3.16)

project(CipherMesh VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. PRE-FLIGHT CHECKS ---
if(ANDROID AND NOT DEFINED ANDROID_NDK)
    message(FATAL_ERROR "ANDROID_NDK is not set. Required for libsodium ExternalProject.")
endif()

# --- 2. FORCE VARIABLES ---
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

include(FetchContent)
include(ExternalProject)

# --- 3. Libsodium via ExternalProject (HARDENED CROSS-COMPILE) ---
set(SODIUM_PREFIX "${CMAKE_BINARY_DIR}/libsodium")
set(SODIUM_STATIC_LIB "${SODIUM_PREFIX}/src/libsodium-build/src/libsodium/.libs/liblibsodium.a")

ExternalProject_Add(libsodium_ext
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG stable
    GIT_SHALLOW TRUE
    PREFIX "${SODIUM_PREFIX}"
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        --build=x86_64-linux-gnu
        --host=armv7a-linux-androideabi
        --enable-static
        --disable-shared
        CC=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
        CFLAGS=--target=armv7a-linux-androideabi21
        AR=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar
        RANLIB=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ranlib
    BUILD_COMMAND make -j$(nproc)
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${SODIUM_STATIC_LIB}"
)

set_source_files_properties("${SODIUM_STATIC_LIB}" PROPERTIES GENERATED TRUE)
ExternalProject_Get_Property(libsodium_ext SOURCE_DIR)
set(LIBSODIUM_SOURCE_DIR ${SOURCE_DIR})

# --- 4. Fetch CMake-based Dependencies ---

# A. MbedTLS
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mbedtls)

# Force Bridge variables for LibDataChannel (Must use absolute FILEPATHs)
set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MbedTLS_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MbedX509_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MbedCrypto_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)

# B. LibDataChannel
FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.22.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(LibDataChannel)

# --- 5. Find Qt Packages ---
find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Concurrent Quick WebSockets)

# --- 6. Libsodium Compatibility Headers ---
set(SODIUM_COMPAT_DIR "${CMAKE_BINARY_DIR}/sodium_compat")
file(MAKE_DIRECTORY "${SODIUM_COMPAT_DIR}/sodium")
file(WRITE "${SODIUM_COMPAT_DIR}/sodium/export.h" "#define SODIUM_EXPORT\n#define SODIUM_EXPORT_WEAK\n")
file(WRITE "${SODIUM_COMPAT_DIR}/sodium/version.h" "#define SODIUM_VERSION_STRING \"1.0.18\"\n")

# --- 7. Core Library ---
add_library(ciphermesh-core STATIC
    src/core/crypto.cpp
    src/core/vault.cpp
    src/core/database.cpp
    src/p2p/ip2pservice.cpp
    src/p2p_webrtc/webrtcservice.cpp
)

add_dependencies(ciphermesh-core libsodium_ext)

target_include_directories(ciphermesh-core SYSTEM BEFORE PUBLIC ${SODIUM_COMPAT_DIR})
target_include_directories(ciphermesh-core PUBLIC
    src/core src/p2p src/p2p_webrtc
    ${LIBSODIUM_SOURCE_DIR}/src/libsodium/include
    ${libdatachannel_SOURCE_DIR}/include
    ${MbedTLS_INCLUDE_DIR}
)

target_compile_options(ciphermesh-core PRIVATE -Wno-macro-redefined)

target_link_libraries(ciphermesh-core PRIVATE
    Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent Qt6::WebSockets
    LibDataChannel::LibDataChannelStatic
    "${SODIUM_STATIC_LIB}"
    "${MbedTLS_LIBRARY}"
    "${MbedX509_LIBRARY}"
    "${MbedCrypto_LIBRARY}"
)

# --- 8. Android Build ---
if(ANDROID)
    qt_add_executable(CipherMesh-Mobile MANUAL_FINALIZATION WIN32 MACOSX_BUNDLE
        src/mobile/main_mobile.cpp
        src/mobile/vaultqmlwrapper.cpp
        src/mobile/qml.qrc
    )

    add_dependencies(CipherMesh-Mobile libsodium_ext)

    target_link_libraries(CipherMesh-Mobile PRIVATE
        ciphermesh-core
        -Wl,--whole-archive
        "${SODIUM_STATIC_LIB}"
        "${MbedTLS_LIBRARY}"
        "${MbedX509_LIBRARY}"
        "${MbedCrypto_LIBRARY}"
        -Wl,--no-whole-archive
        LibDataChannel::LibDataChannelStatic
        Qt6::Quick Qt6::Core Qt6::Network Qt6::WebSockets
    )

    set_target_properties(CipherMesh-Mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
    )
    qt_finalize_executable(CipherMesh-Mobile)
endif()

# --- 9. Desktop Build ---
if(NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/main.cpp")
    add_executable(CipherMesh-Desktop
        src/desktop/main.cpp
        src/desktop/mainwindow.cpp
    )
    target_link_libraries(CipherMesh-Desktop PRIVATE ciphermesh-core Qt6::Widgets)
endif()