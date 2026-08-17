cmake_minimum_required(VERSION 3.27)
project(molga_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    # Keep source decoding deterministic on non-English Windows hosts. Vendored
    # DXC contains UTF-8 source text and promotes code-page warnings to errors.
    add_compile_options(/utf-8)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Minimum macOS SDK deployment target used for build compatibility")
endif()

# 우리 코드에만 적용할 경고/sanitizer 모음 (external 라이브러리는 제외)
set(MOLGA_SANITIZER "" CACHE STRING "Enable a sanitizer: address | undefined | thread (empty = off)")

add_library(molga_warnings INTERFACE)
if(MSVC)
    target_compile_options(molga_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(molga_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()
if(NOT MOLGA_SANITIZER STREQUAL "")
    if(MSVC)
        message(FATAL_ERROR "MOLGA_SANITIZER presets require Clang or GCC")
    endif()
    target_compile_options(molga_warnings INTERFACE
        -fsanitize=${MOLGA_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(molga_warnings INTERFACE -fsanitize=${MOLGA_SANITIZER})
endif()

# ── External dependencies ──────────────────────────────────────────────────────
include(FetchContent)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG        v3.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(box2d)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/SDL EXCLUDE_FROM_ALL)

set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/SDL_shadercross EXCLUDE_FROM_ALL)

# ── ImGui library ──────────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# ── Common engine source files ─────────────────────────────────────────────────
set(ENGINE_SOURCES
    src/Common/Sha256.cpp
    src/Rendering/GraphicsDevice.cpp
    src/Rendering/ShaderBundle.cpp
    src/Rendering/Shader.cpp
    src/Rendering/ShaderManager.cpp
    src/Rendering/Material.cpp
    src/Rendering/Texture.cpp
    src/Rendering/Sprite.cpp
    src/Rendering/Renderer.cpp
    src/Rendering/RenderSystem2D.cpp
    src/Rendering/GameOutputRenderer.cpp
    src/Rendering/LightingFrame2D.cpp
    src/Rendering/LightingPipeline2D.cpp
    src/Rendering/PostProcessProfile2D.cpp
    src/Rendering/PostProcessProfileResolver.cpp
    src/Rendering/PostProcessPipeline.cpp
    src/Rendering/OutputPresentationLayout.cpp
    src/Rendering/CameraOutputLayout.cpp
    src/Rendering/WorldSort2D.cpp
    src/Rendering/WorldRenderTraversal.cpp
    src/Rendering/SpriteBatcher.cpp
    src/Rendering/RenderPassState.cpp
    src/Rendering/RenderTarget.cpp
    src/Rendering/Utf8.cpp
    src/Rendering/FontFace.cpp
    src/Rendering/FontAtlas.cpp
    src/Core/MolgaTime.cpp
    src/Systems/Input.cpp
    src/Rendering/Camera2D.cpp
    src/Rendering/SpriteSheet.cpp
    src/Rendering/Animation.cpp
    src/Rendering/AnimationClip2D.cpp
    src/Rendering/AnimatorController2D.cpp
    src/Physics/Collision.cpp
    src/Physics/PhysicsWorld.cpp
    src/Physics/Physics2D.cpp
    src/Physics/PhysicsConversions.cpp
    src/Physics/Box2DBackend.cpp
    src/Rendering/Tilemap.cpp
    src/UI/UI.cpp
    src/UI/UISystem.cpp
    src/Systems/Audio.cpp
    src/Core/Scene.cpp
    src/Systems/Particle.cpp
    src/ECS/Component.cpp
    src/ECS/BuiltinComponents.cpp
    src/ECS/GameObject.cpp
    src/ECS/Components/Transform.cpp
    src/ECS/Components/SpriteRenderer.cpp
    src/ECS/Components/Animator2D.cpp
    src/ECS/Components/TilemapRenderer.cpp
    src/ECS/Components/TextRenderer2D.cpp
    src/ECS/Components/UICanvas.cpp
    src/ECS/Components/RectTransform.cpp
    src/ECS/Components/UIImage.cpp
    src/ECS/Components/UILabel.cpp
    src/ECS/Components/UIButton.cpp
    src/ECS/Components/ParticleSystem.cpp
    src/ECS/Components/BoxCollider2D.cpp
    src/ECS/Components/CircleCollider2D.cpp
    src/ECS/Components/Rigidbody2D.cpp
    src/ECS/Components/Collider2D.cpp
    src/ECS/Components/AudioSource.cpp
    src/ECS/Components/AudioListener.cpp
    src/ECS/Components/Camera.cpp
    src/ECS/Components/PointLight2D.cpp
    src/ECS/Components/ShadowOccluder2D.cpp
    src/ECS/Components/PrefabInstance.cpp
    src/Core/SceneSerializer.cpp
    src/Core/PrefabRegistry.cpp
    src/Core/PrefabUtil.cpp
    src/Core/World.cpp
    src/Core/SceneRuntime.cpp
    src/Core/Scheduler.cpp
    src/Scripting/Script.cpp
    src/Scripting/ScriptInvocationBoundary.cpp
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
    src/Core/BuildPlan.cpp
    src/Core/SmokeReport.cpp
    src/Core/PackageLayout.cpp
    src/Core/PackageFinalizer.cpp
    src/Core/BuildProfile.cpp
    src/Core/GameConfig.cpp
    src/Core/PersistentStorage.cpp
    src/Core/PlayerPrefs.cpp
    src/Core/SaveSystem.cpp
    src/Editor/Selection/SelectionService.cpp
    src/Editor/Properties/EditorPropertyDescriptor.cpp
    src/Editor/GameViewLayout.cpp
    src/Editor/EditorPreferences.cpp
    src/Core/AssetDatabase.cpp
    src/Core/AssetDependencyValidator.cpp
    src/Core/SpriteResolver.cpp
    src/Core/TileSetAsset.cpp
    src/Core/Importers/ImporterRegistry.cpp
    src/Core/Importers/TextureImporter.cpp
    src/Core/Importers/AudioImporter.cpp
    src/Core/Importers/PrefabImporter.cpp
    src/Core/Importers/FontImporter.cpp
    src/Core/Importers/PostProcessProfileImporter.cpp
)

# ── Marrow skeletal animation dependency check ───────────────────────────────
set(MOLGA_MARROW_DIR "" CACHE PATH
    "Explicit path to a pinned Marrow checkout with build/libmarrow_runtime.a")
if(MOLGA_MARROW_DIR AND
   EXISTS "${MOLGA_MARROW_DIR}/include/marrow/runtime/skeleton.hpp" AND
   EXISTS "${MOLGA_MARROW_DIR}/build/libmarrow_runtime.a")
    message(STATUS "Marrow runtime dependency found. Enabling Marrow support.")
    set(MOLGA_MARROW_SUPPORT ON)
    list(APPEND ENGINE_SOURCES src/ECS/Components/MarrowRenderer.cpp)
else()
    message(STATUS
        "Marrow support disabled. Set MOLGA_MARROW_DIR to the pinned checkout.")
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
target_include_directories(molga_core PRIVATE ${IMGUI_DIR})

if(MOLGA_MARROW_SUPPORT)
    target_include_directories(molga_core PUBLIC ${MOLGA_MARROW_DIR}/include)
    target_compile_definitions(molga_core PUBLIC MOLGA_MARROW_SUPPORT)
endif()

target_link_libraries(molga_core
    PRIVATE SDL3::SDL3
    PUBLIC box2d
)
if(MOLGA_MARROW_SUPPORT)
    target_link_libraries(molga_core PUBLIC
        ${MOLGA_MARROW_DIR}/build/libmarrow_runtime.a)
endif()

target_link_libraries(molga_core PRIVATE molga_warnings)
target_compile_definitions(molga_core PRIVATE
    MOLGA_SHADER_BUNDLE_DIR="${CMAKE_BINARY_DIR}/ShaderBundle")

if(APPLE)
    target_link_libraries(molga_core PUBLIC
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
endif()

# ── Editor-only source files ───────────────────────────────────────────────────
set(EDITOR_SOURCES
    src/Editor/ImGuiLayer.cpp
    src/Editor/ImGuiTextureBridge.cpp
    src/Editor/EditorState.cpp
    src/Editor/Editor.cpp
    src/Editor/EditorConsoleSink.cpp
    src/Editor/EditorTaskService.cpp
    src/Platform/Process.cpp
    src/Editor/Commands/ObjectCommands.cpp
    src/Editor/Commands/PrefabCommands.cpp
    src/Editor/Commands/TransformCommand.cpp
    src/Editor/Commands/SceneSnapshots.cpp
    src/Editor/Commands/PropertyCommands.cpp
    src/Editor/Commands/ComponentCommands.cpp
    src/Editor/Commands/TilePaintCommand.cpp
    src/Editor/Gizmos/TransformGizmo.cpp
    src/Editor/FontManager.cpp
    src/Editor/VSCodeIntegration.cpp
    src/Editor/UIRegistry.cpp
    src/Editor/WindowManager.cpp
    src/Editor/SceneOperations.cpp
    src/Editor/BuildManager.cpp
    src/Editor/GameBuilder.cpp
    src/Editor/Windows/SceneViewWindow.cpp
    src/Editor/Windows/GameViewWindow.cpp
    src/Editor/Windows/StatsWindow.cpp
    src/Editor/Windows/ProfilerWindow.cpp
    src/Editor/Windows/HierarchyWindow.cpp
    src/Editor/Windows/InspectorWindow.cpp
    src/Editor/Windows/ProjectWindow.cpp
    src/Editor/Windows/ProjectBrowserWindow.cpp
    src/Editor/Windows/ScriptWindow.cpp
    src/Editor/Windows/ProjectSettingsWindow.cpp
    src/Editor/Windows/ConsoleWindow.cpp
    src/Editor/Windows/TilePaletteWindow.cpp
    src/Editor/Windows/AnimationWindow.cpp
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

add_executable(molga_migrate src/Tools/InputMigrator.cpp)
target_link_libraries(molga_migrate PRIVATE molga_core molga_warnings)

add_executable(molga_shaderc
    src/Tools/ShaderCompiler.cpp
    src/Common/Sha256.cpp
    src/Rendering/ShaderBundle.cpp
)
target_include_directories(molga_shaderc PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
)
target_link_libraries(molga_shaderc PRIVATE
    SDL3_shadercross::SDL3_shadercross-static
    SDL3::SDL3-static
    molga_warnings
)

file(GLOB_RECURSE MOLGA_SHADER_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/Shaders/*.hlsl"
    "${CMAKE_SOURCE_DIR}/src/Shaders/*.shader.json")
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/ShaderBundle/manifest.json"
    COMMAND $<TARGET_FILE:molga_shaderc> build
            --descriptors "${CMAKE_SOURCE_DIR}/src/Shaders"
            --output "${CMAKE_BINARY_DIR}/ShaderBundle"
    DEPENDS molga_shaderc ${MOLGA_SHADER_INPUTS}
    COMMENT "Building validated SDL_GPU shader bundle"
    VERBATIM)
add_custom_target(molga_shader_bundle ALL
    DEPENDS "${CMAKE_BINARY_DIR}/ShaderBundle/manifest.json")
add_dependencies(molga_core molga_shader_bundle)
add_dependencies(molga_engine molga_shader_bundle)
target_link_libraries(molga_engine PRIVATE molga_warnings)
target_compile_definitions(molga_engine PRIVATE
    MOLGA_EDITOR
    MOLGA_ENGINE_SHADER_DESCRIPTOR_DIR="${CMAKE_SOURCE_DIR}/src/Shaders")
set_target_properties(molga_engine PROPERTIES ENABLE_EXPORTS ON)

# ── Runtime executable ─────────────────────────────────────────────────────────
add_executable(molga_runtime src/runtime_main.cpp)
target_link_libraries(molga_runtime PRIVATE molga_core)
target_link_libraries(molga_runtime PRIVATE molga_warnings)
add_dependencies(molga_runtime molga_shader_bundle)
set_target_properties(molga_runtime PROPERTIES ENABLE_EXPORTS ON)

# ── Copy assets to build directory ─────────────────────────────────────────────
add_custom_command(TARGET molga_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:molga_engine>/assets
    COMMENT "Copying assets to build directory"
)

# ── Copy editor resources to build directory ─────────────────────────────────
add_custom_command(TARGET molga_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:molga_engine>/Editor
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/src/Editor/missing_texture.png $<TARGET_FILE_DIR:molga_engine>/Editor/missing_texture.png
    COMMENT "Copying editor resources to build directory"
)
add_custom_command(TARGET molga_runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:molga_runtime>/Editor
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_SOURCE_DIR}/src/Editor/missing_texture.png $<TARGET_FILE_DIR:molga_runtime>/Editor/missing_texture.png
    COMMENT "Copying editor resources to build directory (runtime)"
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
    TIMEOUT 180
    RUN_SERIAL TRUE
)
