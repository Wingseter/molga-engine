cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)

add_executable(molga_engine
    src/main.cpp
    src/Shader.cpp
    src/Texture.cpp
    src/Sprite.cpp
    src/Renderer.cpp
    src/Time.cpp
    src/Input.cpp
    src/Camera2D.cpp
    src/SpriteSheet.cpp
    src/Animation.cpp
    src/Collision.cpp
    src/Tilemap.cpp
    src/UI.cpp
    src/Audio.cpp
    src/Scene.cpp
)

add_subdirectory(external/glfw)

# GLAD 라이브러리 추가
add_library(glad external/glad/src/glad.c)
include_directories(external/glad/include)

# stb 라이브러리 추가
include_directories(external/stb)

# miniaudio 라이브러리 추가
include_directories(external/miniaudio)

# 타겟에 라이브러리 링크
target_link_libraries(molga_engine glad glfw)

# macOS audio frameworks for miniaudio
if(APPLE)
    target_link_libraries(molga_engine "-framework CoreAudio" "-framework AudioToolbox")
endif()
