# Services library - Platform-independent business logic
# Shared between desktop (Qt) and mobile (Android) platforms

add_library(ciphermesh-services STATIC
    breach_service.cpp
    password_service.cpp
    totp_service.cpp
    theme_service.cpp
)

target_include_directories(ciphermesh-services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/utils
)

# Link dependencies
if(ANDROID)
    target_link_libraries(ciphermesh-services PUBLIC
        ciphermesh-core
        log
    )
else()
    target_link_libraries(ciphermesh-services PUBLIC
        ciphermesh-core
        Qt6::Core
        Qt6::Network
    )
endif()
