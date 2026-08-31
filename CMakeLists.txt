cmake_minimum_required(VERSION 3.16)

project(bus_ticket_system VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(bus_ticket_system
    main.cpp
    widget.cpp
    widget.h
    widget.ui
)

# 将全局 QSS 编译进可执行文件，发布时无需额外复制样式文件。
qt_add_resources(bus_ticket_system "bus_ticket_system_styles"
    PREFIX "/bus_ticket_system"
    FILES
        styles/app.qss
)

target_link_libraries(bus_ticket_system PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

set_target_properties(bus_ticket_system PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS bus_ticket_system
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
