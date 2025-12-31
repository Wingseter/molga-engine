cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)

# ImGui 라이브러리
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

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
    src/Particle.cpp
    src/Editor/ImGuiLayer.cpp
    src/Editor/EditorState.cpp
    src/Core/Application.cpp
    src/ECS/GameObject.cpp
    src/ECS/Components/Transform.cpp
    src/ECS/Components/SpriteRenderer.cpp
    src/ECS/Components/BoxCollider2D.cpp
    src/Editor/Editor.cpp
    src/Editor/Windows/HierarchyWindow.cpp
    src/Editor/Windows/InspectorWindow.cpp
    src/Core/SceneSerializer.cpp
    src/Scripting/Script.cpp
    src/Scripting/ScriptManager.cpp
    src/Scripting/BuiltinScripts.cpp
    src/Platform/Platform.cpp
    src/Scenes/MenuScene.cpp
    src/Scenes/GameScene.cpp
)

add_subdirectory(external/glfw)

# GLAD 라이브러리 추가
add_library(glad external/glad/src/glad.c)
include_directories(external/glad/include)

# stb 라이브러리 추가
include_directories(external/stb)

# miniaudio 라이브러리 추가
include_directories(external/miniaudio)

# nlohmann/json 라이브러리 추가
include_directories(external/nlohmann_json/include)

# ImGui에 GLFW 링크
target_link_libraries(imgui glfw)

# 타겟에 라이브러리 링크
target_link_libraries(molga_engine glad glfw imgui)

# macOS audio frameworks for miniaudio
if(APPLE)
    target_link_libraries(molga_engine "-framework CoreAudio" "-framework AudioToolbox")
endif()

# Copy assets to build directory
add_custom_command(TARGET molga_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:molga_engine>/assets
    COMMENT "Copying assets to build directory"
)
