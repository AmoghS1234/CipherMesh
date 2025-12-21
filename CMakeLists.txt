cmake_minimum_required(VERSION 3.16)
project(CipherMesh VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NO_LOGGING ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)

add_compile_definitions(MBEDTLS_SSL_DTLS_SRTP)

include(FetchContent)

if(NOT DEFINED sodium_INCLUDE_DIR OR NOT DEFINED sodium_LIBRARY_RELEASE)
    message(FATAL_ERROR "Prebuilt libsodium not provided.  Set sodium_INCLUDE_DIR and sodium_LIBRARY_RELEASE.")
endif()

add_library(sodium STATIC IMPORTED GLOBAL)
set_target_properties(sodium PROPERTIES
    IMPORTED_LOCATION "${sodium_LIBRARY_RELEASE}"
    INTERFACE_INCLUDE_DIRECTORIES "${sodium_INCLUDE_DIR}"
)

message(STATUS "Using prebuilt libsodium:  ${sodium_LIBRARY_RELEASE}")

FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mbedtls)

set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MbedTLS_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MbedX509_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MbedCrypto_LIBRARY "${mbedtls_BINARY_DIR}/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)

FetchContent_Declare(
    LibDataChannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.22.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(LibDataChannel)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Concurrent Quick WebSockets)

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
    ${MbedTLS_INCLUDE_DIR}
)

target_include_directories(ciphermesh-core PUBLIC
    $<TARGET_PROPERTY:sodium,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_options(ciphermesh-core PRIVATE -Wno-macro-redefined)

target_link_libraries(ciphermesh-core PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::WebSockets
    LibDataChannel::LibDataChannelStatic
    sodium
    "${MbedTLS_LIBRARY}"
    "${MbedX509_LIBRARY}"
    "${MbedCrypto_LIBRARY}"
)

if(ANDROID)
    qt_add_executable(CipherMesh-Mobile
        MANUAL_FINALIZATION
        src/mobile/main_mobile.cpp
        src/mobile/vaultqmlwrapper.cpp
        src/mobile/qml.qrc
    )

    target_link_libraries(CipherMesh-Mobile PRIVATE
        ciphermesh-core
        -Wl,--whole-archive
        sodium
        "${MbedTLS_LIBRARY}"
        "${MbedX509_LIBRARY}"
        "${MbedCrypto_LIBRARY}"
        -Wl,--no-whole-archive
        LibDataChannel::LibDataChannelStatic
        Qt6::Quick
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
    )

    set_target_properties(CipherMesh-Mobile PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/mobile/android
    )

    qt_finalize_executable(CipherMesh-Mobile)
endif()

if(NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/main.cpp")
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6Widgets_FOUND)
        add_executable(CipherMesh-Desktop
            src/desktop/main.cpp
            src/desktop/mainwindow.cpp
        )
        target_link_libraries(CipherMesh-Desktop PRIVATE
            ciphermesh-core
            Qt6::Widgets
        )
    endif()
endif()