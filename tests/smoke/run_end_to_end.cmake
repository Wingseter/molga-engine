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
set(SMOKE_STORAGE_ENV
    "HOME=${TEST_HOME}"
    "XDG_DATA_HOME=${TEST_HOME}"
    "LOCALAPPDATA=${TEST_HOME}"
)

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
    COMMAND "${CMAKE_COMMAND}" -E env ${SMOKE_STORAGE_ENV}
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

file(READ "${EDITOR_REPORT}" editor_report)
string(FIND "${editor_report}" [["objectCount": 3]] editor_object_count_position)
if(editor_object_count_position EQUAL -1)
    message(FATAL_ERROR
        "Editor smoke did not instantiate the packaged-scene prefab\n${editor_report}")
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
    "${PACKAGE_ROOT}/Scenes/stage1.json"
    "${PACKAGE_ROOT}/Scenes/stage2.json"
    "${PACKAGE_ROOT}/Assets/Textures/smoke.ppm"
    "${PACKAGE_ROOT}/Assets/Fonts/NotoSansKR-Regular.ttf"
    "${PACKAGE_ROOT}/Assets/Fonts/OFL.txt"
    "${PACKAGE_ROOT}/Assets/Prefabs/TitleBadge.prefab"
    "${PACKAGE_ROOT}/asset_catalog.json"
    "${PACKAGE_ROOT}/Resources/missing_texture.png"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Missing package output: ${required_path}")
    endif()
endforeach()

# These are all public scene/prefab serialization envelopes produced by the
# editor component contract; keep the fixture reproducible without a private
# runtime-only hook.
file(READ "${PACKAGE_ROOT}/Scenes/main.json" packaged_main_scene)
file(READ "${PACKAGE_ROOT}/Scenes/stage1.json" packaged_stage1_scene)
file(READ "${PACKAGE_ROOT}/Scenes/stage2.json" packaged_stage2_scene)
file(READ "${PACKAGE_ROOT}/Assets/Prefabs/TitleBadge.prefab" packaged_prefab)
file(READ "${PACKAGE_ROOT}/asset_catalog.json" asset_catalog)
foreach(expected_contract
    [["type": "PlayerPrefsButton"]]
    [["type": "SceneLoadButton"]]
    [["name": "한글 타이틀"]]
    [["text": "한글 타이틀 - 시작"]]
    [["Scene Path": "Scenes/stage1.json"]]
)
    string(FIND "${packaged_main_scene}" "${expected_contract}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Packaged title scene is missing ${expected_contract}")
    endif()
endforeach()
foreach(stage_scene packaged_stage1_scene packaged_stage2_scene)
    foreach(expected_contract
        [["type": "Rigidbody2D"]]
        [["bodyType": 2]]
        [["type": "PlatformerController"]]
    )
        string(FIND "${${stage_scene}}" "${expected_contract}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "${stage_scene} is missing ${expected_contract}")
        endif()
    endforeach()
endforeach()
foreach(expected_stage1_contract
    [["rotation": 14.0]]
    [["friction": 0.85]]
    [["restitution": 0.05]]
    [["Scene Path": "Scenes/stage2.json"]]
)
    string(FIND "${packaged_stage1_scene}" "${expected_stage1_contract}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Packaged stage 1 is missing ${expected_stage1_contract}")
    endif()
endforeach()
foreach(expected_stage2_contract
    [["rotation": -11.0]]
    [["friction": 0.25]]
    [["restitution": 0.85]]
    [["text": "스테이지 2 클리어"]]
    [["Title": "한글 타이틀"]]
)
    string(FIND "${packaged_stage2_scene}" "${expected_stage2_contract}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Packaged stage 2 is missing ${expected_stage2_contract}")
    endif()
endforeach()
string(FIND "${packaged_stage2_scene}" [["type": "SaveSlotButton"]] position)
if(position EQUAL -1)
    message(FATAL_ERROR "Packaged stage 2 is missing SaveSlotButton")
endif()
foreach(expected_prefab_contract
    [["guid": "33333333333333333333333333333333"]]
    [["type": "UILabel"]]
    [["fontGuid": "22222222222222222222222222222222"]]
)
    string(FIND "${packaged_prefab}" "${expected_prefab_contract}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Packaged prefab is missing ${expected_prefab_contract}")
    endif()
endforeach()
foreach(expected_catalog_guid
    [[22222222222222222222222222222222]]
    [[33333333333333333333333333333333]]
)
    string(FIND "${asset_catalog}" "${expected_catalog_guid}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Asset catalog is missing ${expected_catalog_guid}")
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
    [["startupSceneId": "Scenes/main.json"]]
    [["sceneCatalog":]]
    [["id": "Scenes/stage1.json"]]
    [["id": "Scenes/stage2.json"]]
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
    COMMAND "${CMAKE_COMMAND}" -E env ${SMOKE_STORAGE_ENV}
        "${GAME_EXECUTABLE}"
        --smoke --frames 8 --report "${RUNTIME_REPORT}"
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
    [["scenePath": "Scenes/stage2.json"]]
    [["objectCount": 3]]
    [["frames": 8]]
    [["assetsResolved": true]]
    [["assetCatalogLoaded": true]]
    [["spriteAssetsResolved": 2]]
    [["spriteAssetsMissing": 0]]
    [["sceneTransitions": 2]]
    [["uiDrivenSceneTransitions": 2]]
    [["fontAssetsResolved": true]]
    [["fontAssetsResolvedCount": 1]]
    [["fontAssetsMissing": 0]]
    [["koreanTitlePreserved": true]]
    [["koreanFontGlyphsPresent": true]]
    [["koreanGlyphAtlasReady": true]]
    [["koreanGlyphQuads": 8]]
    [["uiComponentsLoaded": 5]]
    [["platformerPlayersLoaded": 1]]
    [["physicsBodiesLoaded": 2]]
    [["physicsShapesLoaded": 2]]
    [["rotatedTerrainVerified": true]]
    [["physicsContactObserved": true]]
    [["restitutionResponseObserved": true]]
    [["frictionResponseObserved": true]]
    [["saveRoundtrip": true]]
    [["scriptDrivenPrefsSaved": true]]
    [["scriptDrivenSlotSaved": true]]
    [["scriptDrivenPersistence": true]]
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
