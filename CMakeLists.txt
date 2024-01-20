cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)

add_executable(molga_engine src/main.cpp
        src/Application.cpp
        src/Application.h
        src/Window.cpp
        src/Window.h)

add_subdirectory(external/glfw)
target_link_libraries(molga_engine glfw)