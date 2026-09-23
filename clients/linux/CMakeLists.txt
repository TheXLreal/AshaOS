cmake_minimum_required(VERSION 3.20)
project(ashaos-linux VERSION 1.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets)

qt_add_executable(ashaos-linux
  main.cpp
  sender_worker.cpp
  sender_worker.h
  protocol.cpp
  protocol.h
  resources.qrc
)

target_link_libraries(ashaos-linux PRIVATE
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets
)

install(TARGETS ashaos-linux RUNTIME DESTINATION bin)
install(FILES io.github.ashaos.LinuxSender.desktop
        DESTINATION share/applications)
install(FILES assets/ashaos.png
        DESTINATION share/icons/hicolor/512x512/apps
        RENAME io.github.ashaos.LinuxSender.png)
