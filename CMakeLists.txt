cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)

add_executable(molga_engine main.cpp
        Application.cpp
        Application.h
        Window.cpp
        Window.h)

add_subdirectory(external/glfw-3.3.9)
target_link_libraries(molga_engine glfw)