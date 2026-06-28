cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 우리 코드에만 적용할 경고/sanitizer 모음 (external 라이브러리는 제외)
set(MOLGA_SANITIZER "" CACHE STRING "Enable a sanitizer: address | undefined | thread (empty = off)")

add_library(molga_warnings INTERFACE)
target_compile_options(molga_warnings INTERFACE -Wall -Wextra -Wpedantic)
if(NOT MOLGA_SANITIZER STREQUAL "")
    target_compile_options(molga_warnings INTERFACE
        -fsanitize=${MOLGA_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(molga_warnings INTERFACE -fsanitize=${MOLGA_SANITIZER})
endif()

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
    src/Rendering/Shader.cpp
    src/Rendering/ShaderManager.cpp
    src/Rendering/Material.cpp
    src/Rendering/Texture.cpp
    src/Rendering/Sprite.cpp
    src/Rendering/Renderer.cpp
    src/Rendering/RenderPassState.cpp
    src/Rendering/Framebuffer.cpp
    src/Core/MolgaTime.cpp
    src/Systems/Input.cpp
    src/Rendering/Camera2D.cpp
    src/Rendering/SpriteSheet.cpp
    src/Rendering/Animation.cpp
    src/Physics/Collision.cpp
    src/Physics/PhysicsWorld.cpp
    src/Physics/Physics2D.cpp
    src/Rendering/Tilemap.cpp
    src/UI/UI.cpp
    src/Systems/Audio.cpp
    src/Core/Scene.cpp
    src/Systems/Particle.cpp
    src/ECS/Component.cpp
    src/ECS/GameObject.cpp
    src/ECS/Components/Transform.cpp
    src/ECS/Components/SpriteRenderer.cpp
    src/ECS/Components/TilemapRenderer.cpp
    src/ECS/Components/TextRenderer2D.cpp
    src/ECS/Components/ParticleSystem.cpp
    src/ECS/Components/BoxCollider2D.cpp
    src/ECS/Components/CircleCollider2D.cpp
    src/ECS/Components/Rigidbody2D.cpp
    src/ECS/Components/Collider2D.cpp
    src/ECS/Components/AudioSource.cpp
    src/ECS/Components/AudioListener.cpp
    src/ECS/Components/Camera.cpp
    src/ECS/Components/PrefabInstance.cpp
    src/Core/SceneSerializer.cpp
    src/Core/PrefabRegistry.cpp
    src/Core/PrefabUtil.cpp
    src/Core/World.cpp
    src/Core/Scheduler.cpp
    src/Scripting/Script.cpp
    src/Scripting/ScriptManager.cpp
    src/Scripting/ScriptPackageLoader.cpp
    src/Scripting/ScriptReloadService.cpp
    src/Scripting/BuiltinScripts.cpp
    src/Platform/Platform.cpp
    src/Rendering/TextRenderer.cpp
    src/Core/Bootstrap.cpp
    src/Core/EventBus.cpp
    src/Core/ProjectSettings.cpp
    src/Common/Log.cpp
    src/Common/StdoutSink.cpp
    src/Common/FileSink.cpp
    src/Common/SmokeReportSink.cpp
    src/Core/PathService.cpp
    src/Core/TextureManager.cpp
    src/Core/Profiling/ProfilerService.cpp
    src/Core/BuildManifest.cpp
    src/Core/SmokeReport.cpp
    src/Core/PackageLayout.cpp
    src/Core/PackageFinalizer.cpp
    src/Core/BuildProfile.cpp
    src/Core/GameConfig.cpp
    src/Editor/Selection/SelectionService.cpp
    src/Core/AssetDatabase.cpp
    src/Core/Importers/TextureImporter.cpp
    src/Core/Importers/AudioImporter.cpp
    src/Core/Importers/PrefabImporter.cpp
)

# ── Marrow skeletal animation dependency check ───────────────────────────────
set(MARROW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Maroow")
if(EXISTS "${MARROW_DIR}/include/marrow/runtime/skeleton.hpp" AND EXISTS "${MARROW_DIR}/build/libmarrow_runtime.a")
    message(STATUS "Marrow runtime dependency found. Enabling Marrow support.")
    set(MOLGA_MARROW_SUPPORT ON)
    list(APPEND ENGINE_SOURCES src/ECS/Components/MarrowRenderer.cpp)
else()
    message(WARNING "Marrow runtime dependency NOT found at ${MARROW_DIR}. Skeletal animation will be disabled.")
    set(MOLGA_MARROW_SUPPORT OFF)
endif()

# ── molga_core static library ──────────────────────────────────────────────────
add_library(molga_core STATIC ${ENGINE_SOURCES})
target_include_directories(molga_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/miniaudio
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
)

if(MOLGA_MARROW_SUPPORT)
    target_include_directories(molga_core PUBLIC ${MARROW_DIR}/include)
    target_compile_definitions(molga_core PUBLIC MOLGA_MARROW_SUPPORT)
endif()

target_link_libraries(molga_core PUBLIC glad glfw)

if(MOLGA_MARROW_SUPPORT)
    target_link_libraries(molga_core PUBLIC ${MARROW_DIR}/build/libmarrow_runtime.a)
endif()

target_link_libraries(molga_core PRIVATE molga_warnings)

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
    src/Editor/EditorConsoleSink.cpp
    src/Editor/EditorTaskService.cpp
    src/Platform/Process.cpp
    src/Editor/Commands/ObjectCommands.cpp
    src/Editor/Commands/PrefabCommands.cpp
    src/Editor/Commands/TransformCommand.cpp
    src/Editor/Gizmos/TransformGizmo.cpp
    src/Editor/FontManager.cpp
    src/Editor/VSCodeIntegration.cpp
    src/Editor/UIRegistry.cpp
    src/Editor/WindowManager.cpp
    src/Editor/SceneOperations.cpp
    src/Editor/BuildManager.cpp
    src/Editor/GameBuilder.cpp
    src/Editor/Windows/SceneViewWindow.cpp
    src/Editor/Windows/StatsWindow.cpp
    src/Editor/Windows/ProfilerWindow.cpp
    src/Editor/Windows/HierarchyWindow.cpp
    src/Editor/Windows/InspectorWindow.cpp
    src/Editor/Windows/ProjectWindow.cpp
    src/Editor/Windows/ProjectBrowserWindow.cpp
    src/Editor/Windows/ScriptWindow.cpp
    src/Editor/Windows/ProjectSettingsWindow.cpp
    src/Editor/Windows/ConsoleWindow.cpp
    src/Editor/Project.cpp
    src/Scripting/ScriptCompiler.cpp
    src/Editor/AssetReferenceScan.cpp
    src/Editor/Commands/ProjectFileCommands.cpp
    src/Editor/Commands/CreateSpriteFromAssetCommand.cpp
    src/Editor/Watcher/AssetWatcher.cpp
)

# ── Main editor executable ─────────────────────────────────────────────────────
add_executable(molga_engine src/main.cpp ${EDITOR_SOURCES})
target_link_libraries(molga_engine PRIVATE molga_core imgui)
target_link_libraries(molga_engine PRIVATE molga_warnings)
target_compile_definitions(molga_engine PRIVATE MOLGA_EDITOR)
set_target_properties(molga_engine PROPERTIES ENABLE_EXPORTS ON)

# ── Runtime executable ─────────────────────────────────────────────────────────
add_executable(molga_runtime src/runtime_main.cpp)
target_link_libraries(molga_runtime PRIVATE molga_core)
target_link_libraries(molga_runtime PRIVATE molga_warnings)
set_target_properties(molga_runtime PROPERTIES ENABLE_EXPORTS ON)

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

add_test(
    NAME smoke_end_to_end
    COMMAND "${CMAKE_COMMAND}"
        "-DEDITOR=$<TARGET_FILE:molga_engine>"
        "-DFIXTURE_SCRIPT=${CMAKE_SOURCE_DIR}/tests/smoke/create_fixture.cmake"
        "-DWORK_ROOT=${CMAKE_BINARY_DIR}/smoke-e2e"
        "-DDUMMY_LIB=$<TARGET_FILE:dummy_valid_lib>"
        -P "${CMAKE_SOURCE_DIR}/tests/smoke/run_end_to_end.cmake"
)
set_tests_properties(smoke_end_to_end PROPERTIES
    LABELS "smoke;e2e"
    TIMEOUT 30
    RUN_SERIAL TRUE
)
