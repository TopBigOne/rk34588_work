cmake_minimum_required(VERSION 4.3)
project(day7_qt_env)

set(CMAKE_CXX_STANDARD 17)

# AUTOMOC：CMake 自动找出带 Q_OBJECT 的头文件，先用 moc 生成信号槽需要的 moc_xxx.cpp，再一起编译。
# 用板子工具链编译时，moc 是 rk3588-toolchain.cmake 里定义的 Qt5::moc（转发到 rk-dev 里执行）
set(CMAKE_AUTOMOC ON)

# 在 sysroot 里找 Qt5 的 CMake 配置（usr/lib/cmake/Qt5Widgets/Qt5WidgetsConfig.cmake），
# Widgets 会顺带找到它依赖的 Core、Gui
find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(day7_qt_env
        src/main.cpp
        src/ui/click_counter_window.h
        src/ui/click_counter_window.cpp
        src/ui/video_tile_widget.h
        src/ui/video_tile_widget.cpp
        src/ui/nvr_preview_window.h
        src/ui/nvr_preview_window.cpp)

target_include_directories(day7_qt_env PRIVATE src)

# -Wshadow：参数和成员同名时编译器警告（项目代码规范要求）
target_compile_options(day7_qt_env PRIVATE -Wall -Wextra -Wshadow)

# Qt5::Widgets 是导入的 target：链接 libQt5Widgets.so（以及 Gui、Core），头文件目录也一起加上
target_link_libraries(day7_qt_env PRIVATE Qt5::Widgets)
