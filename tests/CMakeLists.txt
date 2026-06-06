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
endfunction()

molga_add_test(test_types             test_types.cpp)
molga_add_test(test_collision         test_collision.cpp)
molga_add_test(test_ecs               test_ecs.cpp)
molga_add_test(test_scene_serializer  test_scene_serializer.cpp)
molga_add_test(test_time              test_time.cpp)
molga_add_test(test_event             test_event.cpp)
molga_add_test(test_renderer_contract test_renderer_contract.cpp)
molga_add_test(test_hierarchy         test_hierarchy.cpp)
molga_add_test(test_command_history   test_command_history.cpp)
molga_add_test(test_world             test_world.cpp)
molga_add_test(test_path_service       test_path_service.cpp)
