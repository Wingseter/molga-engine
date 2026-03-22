cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)

# ── External dependencies ──────────────────────────────────────────────────────
add_subdirectory(external/glfw)

add_library(glad external/glad/src/glad.c)
target_include_directories(glad PUBLIC ${CMAKE_SOURCE_DIR}/external/glad/include)

# ── ImGui library ──────────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/external/glad/include
)
target_link_libraries(imgui glfw)

# ── Common engine source files ─────────────────────────────────────────────────
set(ENGINE_SOURCES
    src/Shader.cpp
    src/Texture.cpp
    src/Sprite.cpp
    src/Renderer.cpp
    src/MolgaTime.cpp
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
    src/ECS/Component.cpp
    src/ECS/GameObject.cpp
    src/ECS/Components/Transform.cpp
    src/ECS/Components/SpriteRenderer.cpp
    src/ECS/Components/BoxCollider2D.cpp
    src/Core/SceneSerializer.cpp
    src/Scripting/Script.cpp
    src/Scripting/ScriptManager.cpp
    src/Scripting/BuiltinScripts.cpp
    src/Platform/Platform.cpp
    src/TextRenderer.cpp
    src/Core/Bootstrap.cpp
)

# ── molga_core static library ──────────────────────────────────────────────────
add_library(molga_core STATIC ${ENGINE_SOURCES})
target_include_directories(molga_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/miniaudio
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
)
target_link_libraries(molga_core PUBLIC glad glfw)

if(APPLE)
    target_link_libraries(molga_core PUBLIC
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()

# ── Editor-only source files ───────────────────────────────────────────────────
set(EDITOR_SOURCES
    src/Editor/ImGuiLayer.cpp
    src/Editor/EditorState.cpp
    src/Editor/Editor.cpp
    src/Editor/FontManager.cpp
    src/Editor/VSCodeIntegration.cpp
    src/Editor/Windows/HierarchyWindow.cpp
    src/Editor/Windows/InspectorWindow.cpp
    src/Editor/Windows/ProjectWindow.cpp
    src/Editor/Windows/ProjectBrowserWindow.cpp
    src/Editor/Windows/ScriptWindow.cpp
    src/Core/GameBuilder.cpp
    src/Core/Project.cpp
    src/Core/TextureManager.cpp
    src/Scripting/ScriptCompiler.cpp
    src/Scenes/MenuScene.cpp
    src/Scenes/GameScene.cpp
)

# ── Main editor executable ─────────────────────────────────────────────────────
add_executable(molga_engine src/main.cpp ${EDITOR_SOURCES})
target_link_libraries(molga_engine PRIVATE molga_core imgui)
target_compile_definitions(molga_engine PRIVATE MOLGA_EDITOR)

# ── Runtime executable ─────────────────────────────────────────────────────────
add_executable(molga_runtime src/runtime_main.cpp)
target_link_libraries(molga_runtime PRIVATE molga_core)

# ── Copy assets to build directory ─────────────────────────────────────────────
add_custom_command(TARGET molga_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:molga_engine>/assets
    COMMENT "Copying assets to build directory"
)

# ── Copy shaders to build directory ──────────────────────────────────────────
add_custom_command(TARGET molga_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/Shaders $<TARGET_FILE_DIR:molga_engine>/Shaders
    COMMENT "Copying shaders to build directory"
)
add_custom_command(TARGET molga_runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/Shaders $<TARGET_FILE_DIR:molga_runtime>/Shaders
    COMMENT "Copying shaders to build directory (runtime)"
)

# ── Testing ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)
