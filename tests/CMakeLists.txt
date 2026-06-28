# ── doctest (vendored single header) ──────────────────────────────────────────
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_SOURCE_DIR}/external/doctest)

# 공용 main: 각 테스트 실행 파일에 링크되어 main()을 제공
add_library(doctest_main STATIC doctest_main.cpp)
target_link_libraries(doctest_main PUBLIC doctest)

function(molga_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE molga_core doctest_main molga_warnings)
    set_target_properties(${TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS "unit")
endfunction()

molga_add_test(test_types             test_types.cpp)
molga_add_test(test_guid              test_guid.cpp)
molga_add_test(test_asset_meta        test_asset_meta.cpp)
molga_add_test(test_asset_database    test_asset_database.cpp)
molga_add_test(test_importer          test_importer.cpp)
target_sources(test_importer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Core/Importers/TextureImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/Core/Importers/AudioImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/Core/Importers/PrefabImporter.cpp)
molga_add_test(test_asset_reference_migration test_asset_reference_migration.cpp)
molga_add_test(test_asset_reference_scan test_asset_reference_scan.cpp)
target_sources(test_asset_reference_scan PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/AssetReferenceScan.cpp)
molga_add_test(test_project_file_commands test_project_file_commands.cpp)
target_sources(test_project_file_commands PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/ProjectFileCommands.cpp)
molga_add_test(test_asset_watcher test_asset_watcher.cpp)
target_sources(test_asset_watcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Watcher/AssetWatcher.cpp)
molga_add_test(test_create_sprite_from_asset test_create_sprite_from_asset.cpp)
target_sources(test_create_sprite_from_asset PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/CreateSpriteFromAssetCommand.cpp)
molga_add_test(test_text              test_text.cpp)
molga_add_test(test_log_message      test_log_message.cpp)
molga_add_test(test_log_sinks        test_log_sinks.cpp)
target_include_directories(test_log_sinks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
molga_add_test(test_editor_console_sink test_editor_console_sink.cpp)
target_sources(test_editor_console_sink PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorConsoleSink.cpp)
target_include_directories(test_editor_console_sink PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
molga_add_test(test_editor_task_service test_editor_task_service.cpp)
target_sources(test_editor_task_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorTaskService.cpp)
target_include_directories(test_editor_task_service PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
molga_add_test(test_process_runner test_process_runner.cpp)
target_sources(test_process_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Platform/Process.cpp)
target_include_directories(test_process_runner PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
molga_add_test(test_script_field_snapshot test_script_field_snapshot.cpp)
molga_add_test(test_script_reload_service test_script_reload_service.cpp)
molga_add_test(test_play_mode_compile_policy test_play_mode_compile_policy.cpp)
molga_add_test(test_collision         test_collision.cpp)
molga_add_test(test_physics           test_physics.cpp)
molga_add_test(test_tilemap           test_tilemap.cpp)
molga_add_test(test_ecs               test_ecs.cpp)
molga_add_test(test_scene_serializer  test_scene_serializer.cpp)
molga_add_test(test_time              test_time.cpp)
molga_add_test(test_event             test_event.cpp)
molga_add_test(test_audio             test_audio.cpp)
molga_add_test(test_particle          test_particle.cpp)
molga_add_test(test_renderer_contract test_renderer_contract.cpp)
molga_add_test(test_render_queue      test_render_queue.cpp)
molga_add_test(test_hierarchy         test_hierarchy.cpp)
molga_add_test(test_command_history   test_command_history.cpp)
molga_add_test(test_world             test_world.cpp)
molga_add_test(test_world_lifecycle   test_world_lifecycle.cpp)
molga_add_test(test_prefab            test_prefab.cpp)
molga_add_test(test_camera            test_camera.cpp)
molga_add_test(test_path_service       test_path_service.cpp)
molga_add_test(test_shader_material   test_shader_material.cpp)
molga_add_test(test_game_builder      test_game_builder.cpp)
molga_add_test(test_package_finalizer test_package_finalizer.cpp)

molga_add_test(test_profile_scope     test_profile_scope.cpp)
molga_add_test(test_frame_counters    test_frame_counters.cpp)
molga_add_test(test_profiler_service  test_profiler_service.cpp)
target_include_directories(test_package_finalizer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

molga_add_test(test_selection_service test_selection_service.cpp)
molga_add_test(test_viewport_math test_viewport_math.cpp)
molga_add_test(test_scene_picker test_scene_picker.cpp)
molga_add_test(test_transform_gizmo_math test_transform_gizmo_math.cpp)

molga_add_test(test_transform_command test_transform_command.cpp)
target_sources(test_transform_command PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/TransformCommand.cpp
)
target_include_directories(test_transform_command PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_transform_command PRIVATE imgui)

molga_add_test(test_script_compiler  test_script_compiler.cpp)
molga_add_test(test_script_reflection test_script_reflection.cpp)
molga_add_test(test_physics_query     test_physics_query.cpp)
molga_add_test(test_scheduler         test_scheduler.cpp)
molga_add_test(test_lifecycle         test_lifecycle.cpp)
molga_add_test(test_input_actions     test_input_actions.cpp)
molga_add_test(test_build_profile     test_build_profile.cpp)
molga_add_test(test_project_build_profile test_project_build_profile.cpp)
target_sources(test_project_build_profile PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
)
target_include_directories(test_project_build_profile PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

molga_add_test(test_build_manager test_build_manager.cpp)
target_sources(test_build_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/BuildManager.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/GameBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorConsoleSink.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorTaskService.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorState.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/WindowManager.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Windows/ConsoleWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/Scripting/ScriptCompiler.cpp
)
target_include_directories(test_build_manager PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_build_manager PRIVATE imgui)

molga_add_test(test_scene_operations test_scene_operations.cpp)
target_sources(test_scene_operations PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/SceneOperations.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
)
target_include_directories(test_scene_operations PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")


target_sources(test_script_compiler PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Scripting/ScriptCompiler.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Project.cpp
)

molga_add_test(build_smoke
    test_build_smoke.cpp
)
target_include_directories(build_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(build_smoke PROPERTIES LABELS "unit;smoke")

molga_add_test(editor_smoke
    test_editor_smoke.cpp
)
target_include_directories(editor_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(editor_smoke PROPERTIES LABELS "unit;smoke")

molga_add_test(runtime_smoke
    test_runtime_smoke.cpp
)
target_include_directories(runtime_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(runtime_smoke PROPERTIES LABELS "unit;smoke")

molga_add_test(test_runtime_script_package_loader
    test_runtime_script_package_loader.cpp
)
target_include_directories(test_runtime_script_package_loader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

molga_add_test(test_editor_undo_dirty test_editor_undo_dirty.cpp)
target_sources(test_editor_undo_dirty PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/SceneSnapshots.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/PropertyCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/ComponentCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/Editor/Commands/ObjectCommands.cpp
)
target_include_directories(test_editor_undo_dirty PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_editor_undo_dirty PRIVATE imgui)

add_library(dummy_valid_lib SHARED dummy_valid_lib.cpp)
target_include_directories(dummy_valid_lib PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
)
if(APPLE)
    set_target_properties(dummy_valid_lib PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif()

add_library(dummy_missing_register_lib SHARED dummy_missing_register_lib.cpp)
target_link_libraries(dummy_missing_register_lib PRIVATE molga_core)

add_library(dummy_missing_api_lib SHARED dummy_missing_api_lib.cpp)
target_link_libraries(dummy_missing_api_lib PRIVATE molga_core)

add_library(dummy_mismatch_api_lib SHARED dummy_mismatch_api_lib.cpp)
target_link_libraries(dummy_mismatch_api_lib PRIVATE molga_core)

add_dependencies(test_runtime_script_package_loader
    dummy_valid_lib
    dummy_missing_register_lib
    dummy_missing_api_lib
    dummy_mismatch_api_lib
)

target_compile_definitions(test_runtime_script_package_loader PRIVATE
    DUMMY_VALID_LIB_PATH="$<TARGET_FILE:dummy_valid_lib>"
    DUMMY_MISSING_REGISTER_LIB_PATH="$<TARGET_FILE:dummy_missing_register_lib>"
    DUMMY_MISSING_API_PATH="$<TARGET_FILE:dummy_missing_api_lib>"
    DUMMY_MISMATCH_API_PATH="$<TARGET_FILE:dummy_mismatch_api_lib>"
)
