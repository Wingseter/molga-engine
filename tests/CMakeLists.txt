# ── doctest (vendored single header) ──────────────────────────────────────────
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_SOURCE_DIR}/external/doctest)

# 공용 main: 각 테스트 실행 파일에 링크되어 main()을 제공
add_library(doctest_main STATIC doctest_main.cpp)
target_link_libraries(doctest_main PUBLIC doctest)

function(molga_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE molga_core doctest_main molga_warnings)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS "unit")
endfunction()

molga_add_test(test_types             test_types.cpp)
molga_add_test(test_text              test_text.cpp)
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
molga_add_test(test_hierarchy         test_hierarchy.cpp)
molga_add_test(test_command_history   test_command_history.cpp)
molga_add_test(test_world             test_world.cpp)
molga_add_test(test_world_lifecycle   test_world_lifecycle.cpp)
molga_add_test(test_prefab            test_prefab.cpp)
molga_add_test(test_camera            test_camera.cpp)
molga_add_test(test_path_service       test_path_service.cpp)
molga_add_test(test_shader_material   test_shader_material.cpp)
molga_add_test(test_game_builder      test_game_builder.cpp)
molga_add_test(test_script_compiler  test_script_compiler.cpp)
molga_add_test(test_script_reflection test_script_reflection.cpp)
molga_add_test(test_physics_query     test_physics_query.cpp)
molga_add_test(test_scheduler         test_scheduler.cpp)
molga_add_test(test_lifecycle         test_lifecycle.cpp)
molga_add_test(test_input_actions     test_input_actions.cpp)
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
