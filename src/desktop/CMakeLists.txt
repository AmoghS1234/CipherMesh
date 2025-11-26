cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg WebSockets Network) 

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(CipherMesh-Desktop
    main.cpp
    mainwindow.cpp
    unlockdialog.cpp
    newentrydialog.cpp
    newgroupdialog.cpp
    sharegroupdialog.cpp
    passwordgeneratordialog.cpp
    locationeditdialog.cpp
    changepassworddialog.cpp
    settingsdialog.cpp
    passwordhistorydialog.cpp
    toast.cpp
    breach_checker.cpp
)

# Add the path to the p2p interface header directory
target_include_directories(CipherMesh-Desktop 
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src/p2p           
    ${CMAKE_SOURCE_DIR}/src/p2p_webrtc
    ${CMAKE_SOURCE_DIR}/src/utils
)

target_link_libraries(CipherMesh-Desktop
    PRIVATE
    Qt6::Widgets
    Qt6::Svg
    Qt6::WebSockets
    Qt6::Network
    ciphermesh-core
    ciphermesh-utils
    p2p
    webrtc-client
)