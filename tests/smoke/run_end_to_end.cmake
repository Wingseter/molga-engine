foreach(required EDITOR FIXTURE_SCRIPT WORK_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(PROJECT_ROOT "${WORK_ROOT}/project")
set(PACKAGE_ROOT "${WORK_ROOT}/package")
set(EDITOR_REPORT "${WORK_ROOT}/editor-report.json")
set(RUNTIME_REPORT "${WORK_ROOT}/runtime-report.json")
set(TEST_HOME "${WORK_ROOT}/home")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}" "${TEST_HOME}")

# Pass DUMMY_LIB forward if defined
set(FIXTURE_ARGS "-DFIXTURE_ROOT=${PROJECT_ROOT}")
if(DEFINED DUMMY_LIB)
    list(APPEND FIXTURE_ARGS "-DDUMMY_LIB=${DUMMY_LIB}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        ${FIXTURE_ARGS}
        -P "${FIXTURE_SCRIPT}"
    RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
    message(FATAL_ERROR "Could not create smoke fixture")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${TEST_HOME}"
        "${EDITOR}" --smoke-build
        "${PROJECT_ROOT}" "${PACKAGE_ROOT}" "${EDITOR_REPORT}"
    RESULT_VARIABLE editor_result
    OUTPUT_VARIABLE editor_stdout
    ERROR_VARIABLE editor_stderr
)
if(NOT editor_result EQUAL 0)
    message(FATAL_ERROR
        "Editor smoke build failed (${editor_result})\n"
        "${editor_stdout}\n${editor_stderr}")
endif()

if(WIN32)
    set(GAME_EXECUTABLE "${PACKAGE_ROOT}/SmokeGame.exe")
else()
    set(GAME_EXECUTABLE "${PACKAGE_ROOT}/SmokeGame")
endif()

foreach(required_path
    "${GAME_EXECUTABLE}"
    "${PACKAGE_ROOT}/game.json"
    "${PACKAGE_ROOT}/Scenes/main.json"
    "${PACKAGE_ROOT}/Assets/Textures/smoke.ppm"
    "${PACKAGE_ROOT}/asset_catalog.json"
    "${PACKAGE_ROOT}/Resources/missing_texture.png"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Missing package output: ${required_path}")
    endif()
endforeach()

if(DEFINED DUMMY_LIB AND EXISTS "${DUMMY_LIB}")
    if(WIN32)
        set(EXPECTED_LIB "${PACKAGE_ROOT}/Scripts/UserScripts.dll")
    elseif(APPLE)
        set(EXPECTED_LIB "${PACKAGE_ROOT}/Scripts/libUserScripts.dylib")
    else()
        set(EXPECTED_LIB "${PACKAGE_ROOT}/Scripts/libUserScripts.so")
    endif()
    if(NOT EXISTS "${EXPECTED_LIB}")
        message(FATAL_ERROR "Missing package output script library: ${EXPECTED_LIB}")
    endif()
endif()

file(READ "${PACKAGE_ROOT}/game.json" game_config)
set(EXPECTED_CONFIGS
    [["gameName": "SmokeGame"]]
    [["productVersion": "0.1.0"]]
    [["developmentBuild": true]]
    [["mainScene": "Scenes/main.json"]]
)
if(DEFINED DUMMY_LIB AND EXISTS "${DUMMY_LIB}")
    if(WIN32)
        list(APPEND EXPECTED_CONFIGS [["library": "Scripts/UserScripts.dll"]])
    elseif(APPLE)
        list(APPEND EXPECTED_CONFIGS [["library": "Scripts/libUserScripts.dylib"]])
    else()
        list(APPEND EXPECTED_CONFIGS [["library": "Scripts/libUserScripts.so"]])
    endif()
    list(APPEND EXPECTED_CONFIGS [["enabled": true]])
endif()

foreach(expected ${EXPECTED_CONFIGS})
    string(FIND "${game_config}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "game.json is missing ${expected}\n${game_config}")
    endif()
endforeach()

execute_process(
    COMMAND "${GAME_EXECUTABLE}"
        --smoke --frames 3 --report "${RUNTIME_REPORT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_stderr
    TIMEOUT 20
)
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR
        "Runtime smoke failed (${runtime_result})\n"
        "${runtime_stdout}\n${runtime_stderr}")
endif()

file(READ "${RUNTIME_REPORT}" runtime_report)
set(EXPECTED_REPORTS
    [["status": "ok"]]
    [["objectCount": 1]]
    [["frames": 3]]
    [["assetsResolved": true]]
)
if(DEFINED DUMMY_LIB AND EXISTS "${DUMMY_LIB}")
    list(APPEND EXPECTED_REPORTS "Scripts: loaded")
    list(APPEND EXPECTED_REPORTS "UserScriptComponents: 1")
else()
    list(APPEND EXPECTED_REPORTS "Scripts: none")
    list(APPEND EXPECTED_REPORTS "UserScriptComponents: 0")
endif()

foreach(expected ${EXPECTED_REPORTS})
    string(FIND "${runtime_report}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Runtime report is missing ${expected}\n${runtime_report}")
    endif()
endforeach()
