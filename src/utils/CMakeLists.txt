cmake_minimum_required(VERSION 3.16)

# Find OpenSSL for HMAC-SHA1 in TOTP
find_package(OpenSSL REQUIRED)

# Utils library - platform-independent utilities
add_library(ciphermesh-utils STATIC
    passwordstrength.cpp
    totp.cpp
)

target_include_directories(ciphermesh-utils
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${OPENSSL_INCLUDE_DIR}
)

# Link OpenSSL for TOTP
target_link_libraries(ciphermesh-utils
    PRIVATE
    OpenSSL::Crypto
)
