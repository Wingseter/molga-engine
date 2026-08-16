foreach(required EDITOR FIXTURE_SCRIPT WORK_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(PROJECT_ROOT "${WORK_ROOT}/project")
set(PACKAGE_ROOT "${WORK_ROOT}/package")
set(EDITOR_REPORT "${WORK_ROOT}/editor-report.json")
set(RUNTIME_REPORT "${WORK_ROOT}/runtime-report.json")
set(P1_AUDIO_REPORT "${WORK_ROOT}/p1-audio-report.txt")
set(P2_SCRIPT_FAULT_REPORT "${WORK_ROOT}/p2-script-fault-report.txt")
set(TEST_HOME "${WORK_ROOT}/home")
set(SMOKE_STORAGE_ENV
    "HOME=${TEST_HOME}"
    "XDG_DATA_HOME=${TEST_HOME}"
    "LOCALAPPDATA=${TEST_HOME}"
    "MOLGA_P1_AUDIO_REPORT=${P1_AUDIO_REPORT}"
    "MOLGA_P2_SCRIPT_FAULT_REPORT=${P2_SCRIPT_FAULT_REPORT}"
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
    "${PACKAGE_ROOT}/Assets/Textures/smoke_normal.ppm"
    "${PACKAGE_ROOT}/Assets/Textures/p1_character.ppm"
    "${PACKAGE_ROOT}/Assets/Fonts/NotoSansKR-Regular.ttf"
    "${PACKAGE_ROOT}/Assets/Fonts/OFL.txt"
    "${PACKAGE_ROOT}/Assets/Prefabs/TitleBadge.prefab"
    "${PACKAGE_ROOT}/Assets/Animations/player_idle.animclip"
    "${PACKAGE_ROOT}/Assets/Animations/player_run.animclip"
    "${PACKAGE_ROOT}/Assets/Animations/player_jump.animclip"
    "${PACKAGE_ROOT}/Assets/Animations/player.animator"
    "${PACKAGE_ROOT}/Assets/Tilemaps/platform.tileset"
    "${PACKAGE_ROOT}/Assets/Audio/music_intro.mp3"
    "${PACKAGE_ROOT}/Assets/Audio/music_loop.mp3"
    "${PACKAGE_ROOT}/Assets/Audio/jump_sfx.mp3"
    "${PACKAGE_ROOT}/Assets/PostProcessing/runtime.postfx"
    "${PACKAGE_ROOT}/Shaders/postfx_fullscreen.vert"
    "${PACKAGE_ROOT}/Shaders/postfx_bloom_down.frag"
    "${PACKAGE_ROOT}/Shaders/postfx_bloom_up.frag"
    "${PACKAGE_ROOT}/Shaders/postfx_bloom_composite.frag"
    "${PACKAGE_ROOT}/Shaders/postfx_color_adjust.frag"
    "${PACKAGE_ROOT}/Shaders/postfx_vignette.frag"
    "${PACKAGE_ROOT}/Shaders/postfx_resolve.frag"
    "${PACKAGE_ROOT}/Shaders/batch_lit.vert"
    "${PACKAGE_ROOT}/Shaders/batch_lit.frag"
    "${PACKAGE_ROOT}/Shaders/shadow_mask_2d.vert"
    "${PACKAGE_ROOT}/Shaders/shadow_mask_2d.frag"
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

function(find_catalog_record_index guid out_index)
    string(JSON record_count LENGTH "${asset_catalog}" records)
    set(found_index "")
    if(record_count GREATER 0)
        math(EXPR last_record "${record_count} - 1")
        foreach(record_index RANGE 0 ${last_record})
            string(JSON record_guid GET "${asset_catalog}" records ${record_index} guid)
            if(record_guid STREQUAL guid)
                set(found_index "${record_index}")
                break()
            endif()
        endforeach()
    endif()
    set(${out_index} "${found_index}" PARENT_SCOPE)
endfunction()

function(assert_catalog_record guid expected_importer)
    find_catalog_record_index("${guid}" record_index)
    if(record_index STREQUAL "")
        message(FATAL_ERROR "Asset catalog is missing P1 GUID ${guid}")
    endif()
    string(JSON importer GET "${asset_catalog}" records ${record_index} importer)
    if(NOT importer STREQUAL expected_importer)
        message(FATAL_ERROR
            "Catalog GUID ${guid} importer is ${importer}, expected ${expected_importer}")
    endif()
    string(JSON import_failed GET "${asset_catalog}" records ${record_index} importFailed)
    if(import_failed)
        string(JSON import_error GET "${asset_catalog}" records ${record_index} importError)
        message(FATAL_ERROR "Catalog GUID ${guid} failed import: ${import_error}")
    endif()
endfunction()

function(assert_catalog_dependency owner_guid dependency_guid)
    find_catalog_record_index("${owner_guid}" owner_index)
    if(owner_index STREQUAL "")
        message(FATAL_ERROR "Cannot inspect dependencies for missing GUID ${owner_guid}")
    endif()
    string(JSON dependency_count LENGTH
        "${asset_catalog}" records ${owner_index} dependencies)
    set(found_dependency false)
    if(dependency_count GREATER 0)
        math(EXPR last_dependency "${dependency_count} - 1")
        foreach(dependency_index RANGE 0 ${last_dependency})
            string(JSON value GET "${asset_catalog}"
                records ${owner_index} dependencies ${dependency_index})
            if(value STREQUAL dependency_guid)
                set(found_dependency true)
                break()
            endif()
        endforeach()
    endif()
    if(NOT found_dependency)
        message(FATAL_ERROR
            "Catalog GUID ${owner_guid} does not depend on ${dependency_guid}")
    endif()
endfunction()

# Catalog v2 is the package-level proof that the recursive scene -> controller
# -> clips/tileset -> texture/audio graph survived import and staging.
string(JSON catalog_schema GET "${asset_catalog}" schemaVersion)
if(NOT catalog_schema EQUAL 2)
    message(FATAL_ERROR "P1 smoke package requires asset catalog schema v2")
endif()
string(JSON catalog_record_count LENGTH "${asset_catalog}" records)
if(catalog_record_count LESS 14)
    message(FATAL_ERROR
        "P2 smoke catalog has only ${catalog_record_count} records; expected at least 14")
endif()

assert_catalog_record("44444444444444444444444444444444" "TextureImporter")
assert_catalog_record("55555555555555555555555555555555" "AnimationClipImporter")
assert_catalog_record("66666666666666666666666666666666" "AnimationClipImporter")
assert_catalog_record("77777777777777777777777777777777" "AnimationClipImporter")
assert_catalog_record("88888888888888888888888888888888" "AnimatorControllerImporter")
assert_catalog_record("99999999999999999999999999999999" "TileSetImporter")
assert_catalog_record("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" "AudioImporter")
assert_catalog_record("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" "AudioImporter")
assert_catalog_record("cccccccccccccccccccccccccccccccc" "AudioImporter")
assert_catalog_record("dddddddddddddddddddddddddddddddd"
    "PostProcessProfileImporter")
assert_catalog_record("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
    "TextureImporter")

find_catalog_record_index("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee" normal_index)
string(JSON normal_usage GET
    "${asset_catalog}" records ${normal_index} settings usage)
string(JSON normal_color_space GET
    "${asset_catalog}" records ${normal_index} settings colorSpace)
string(JSON normal_width GET
    "${asset_catalog}" records ${normal_index} width)
string(JSON normal_height GET
    "${asset_catalog}" records ${normal_index} height)
if(NOT normal_usage STREQUAL "NormalMap" OR
   NOT normal_color_space STREQUAL "LegacyLinear" OR
   NOT normal_width EQUAL 1 OR NOT normal_height EQUAL 1)
    message(FATAL_ERROR "Packaged smoke normal map contract is incomplete")
endif()

find_catalog_record_index("dddddddddddddddddddddddddddddddd" postfx_index)
string(JSON postfx_effect_count GET
    "${asset_catalog}" records ${postfx_index} metadata effectCount)
string(JSON postfx_active_count GET
    "${asset_catalog}" records ${postfx_index} metadata activeEffectCount)
if(NOT postfx_effect_count EQUAL 3 OR NOT postfx_active_count EQUAL 3)
    message(FATAL_ERROR "Packaged post-process profile metadata is incomplete")
endif()

assert_catalog_dependency("55555555555555555555555555555555"
    "44444444444444444444444444444444")
assert_catalog_dependency("66666666666666666666666666666666"
    "44444444444444444444444444444444")
assert_catalog_dependency("77777777777777777777777777777777"
    "44444444444444444444444444444444")
assert_catalog_dependency("88888888888888888888888888888888"
    "55555555555555555555555555555555")
assert_catalog_dependency("88888888888888888888888888888888"
    "66666666666666666666666666666666")
assert_catalog_dependency("88888888888888888888888888888888"
    "77777777777777777777777777777777")
assert_catalog_dependency("99999999999999999999999999999999"
    "44444444444444444444444444444444")

find_catalog_record_index("44444444444444444444444444444444" p1_texture_index)
string(JSON p1_filter GET "${asset_catalog}" records ${p1_texture_index} settings filter)
string(JSON p1_sprite_mode GET "${asset_catalog}" records ${p1_texture_index} settings spriteMode)
string(JSON p1_color_space GET "${asset_catalog}" records ${p1_texture_index} settings colorSpace)
string(JSON p1_slice_count LENGTH "${asset_catalog}" records ${p1_texture_index} settings slices)
if(NOT p1_filter STREQUAL "Nearest" OR
   NOT p1_sprite_mode STREQUAL "Multiple" OR
   NOT p1_color_space STREQUAL "SRGB" OR
   NOT p1_slice_count EQUAL 6)
    message(FATAL_ERROR "Packaged P1 spritesheet import settings are incomplete")
endif()

foreach(audio_guid
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    "cccccccccccccccccccccccccccccccc")
    find_catalog_record_index("${audio_guid}" audio_index)
    string(JSON audio_channels GET "${asset_catalog}" records ${audio_index} metadata channels)
    string(JSON audio_sample_rate GET "${asset_catalog}" records ${audio_index} metadata sampleRate)
    string(JSON audio_duration GET "${asset_catalog}" records ${audio_index} metadata durationSeconds)
    if(audio_channels LESS 1 OR audio_sample_rate LESS 1 OR audio_duration LESS_EQUAL 0)
        message(FATAL_ERROR "Audio importer metadata is invalid for ${audio_guid}")
    endif()
endforeach()

file(READ "${PACKAGE_ROOT}/Assets/Animations/player_idle.animclip" p1_idle_clip)
file(READ "${PACKAGE_ROOT}/Assets/Animations/player_run.animclip" p1_run_clip)
file(READ "${PACKAGE_ROOT}/Assets/Animations/player_jump.animclip" p1_jump_clip)
file(READ "${PACKAGE_ROOT}/Assets/Animations/player.animator" p1_animator)
file(READ "${PACKAGE_ROOT}/Assets/Tilemaps/platform.tileset" p1_tileset)

foreach(clip_document p1_idle_clip p1_run_clip p1_jump_clip)
    string(JSON clip_texture GET "${${clip_document}}" textureGuid)
    string(JSON clip_frame_count LENGTH "${${clip_document}}" frames)
    if(NOT clip_texture STREQUAL "44444444444444444444444444444444" OR
       clip_frame_count LESS 2)
        message(FATAL_ERROR "${clip_document} is not a sliced two-frame P1 clip")
    endif()
endforeach()
string(JSON jump_loops GET "${p1_jump_clip}" loop)
if(jump_loops)
    message(FATAL_ERROR "P1 jump clip must be non-looping")
endif()
string(JSON animator_state_count LENGTH "${p1_animator}" states)
string(JSON animator_transition_count LENGTH "${p1_animator}" transitions)
string(JSON animator_parameter_count LENGTH "${p1_animator}" parameters)
string(JSON animator_default_state GET "${p1_animator}" defaultStateId)
if(NOT animator_state_count EQUAL 3 OR
   NOT animator_transition_count EQUAL 5 OR
   NOT animator_parameter_count EQUAL 3 OR
   NOT animator_default_state STREQUAL "idle-state")
    message(FATAL_ERROR "P1 idle/run/jump FSM contract is incomplete")
endif()

string(JSON tileset_tile_count LENGTH "${p1_tileset}" tiles)
string(JSON terrain_rule_count LENGTH "${p1_tileset}" terrainRules)
if(tileset_tile_count LESS 18 OR NOT terrain_rule_count EQUAL 16)
    message(FATAL_ERROR "P1 tileset lacks layer tiles or the 16 NESW rules")
endif()
set(actual_terrain_masks "")
foreach(rule_index RANGE 0 15)
    string(JSON terrain_mask GET "${p1_tileset}" terrainRules ${rule_index} mask)
    list(APPEND actual_terrain_masks "${terrain_mask}")
endforeach()
list(SORT actual_terrain_masks COMPARE NATURAL ORDER ASCENDING)
set(expected_terrain_masks 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
if(NOT "${actual_terrain_masks}" STREQUAL "${expected_terrain_masks}")
    message(FATAL_ERROR "P1 tileset NESW masks are not exactly 0..15")
endif()
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
foreach(expected_p1_stage_contract
    [["type": "Animator2D"]]
    [["controllerGuid": "88888888888888888888888888888888"]]
    [["sizeMode": "Native"]]
    [["type": "TilemapRenderer"]]
    [["tileSetGuid": "99999999999999999999999999999999"]]
    [["name": "Background"]]
    [["name": "Collision"]]
    [["name": "Foreground"]]
    [["collision": true]]
    [=["rle": [[24, -1, -1], [1, 2, 1], [6, 10, 1], [1, 8, 1]]]=]
    [["type": "ParticleSystem"]]
    [["presetName": "P1TexturedBurst"]]
    [["frameMode": "Random"]]
    [["seed": 20260716]]
    [["sizeOverLife":]]
    [["colorOverLife":]]
    [["blendMode": "Additive"]]
    [["clipGuid": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"]]
    [["clipGuid": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"]]
    [["clipGuid": "cccccccccccccccccccccccccccccccc"]]
    [["outputBus": "Music"]]
    [["outputBus": "SFX"]]
    [["Crossfade Seconds": 0.25]]
    [["Particle Burst Count": 24]]
)
    string(FIND "${packaged_stage1_scene}" "${expected_p1_stage_contract}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Packaged P1 character stage is missing ${expected_p1_stage_contract}")
    endif()
endforeach()
foreach(expected_stage2_contract
    [["rotation": -11.0]]
    [["friction": 0.25]]
    [["restitution": 0.85]]
    [["text": "스테이지 2 클리어"]]
    [["Title": "한글 타이틀"]]
    [["outputRole": "Primary"]]
    [["outputRole": "Secondary"]]
    [["cullingMask": 32]]
    [["cullingMask": 1]]
    [["lightingEnabled": true]]
    [["lightingMode": "Lit"]]
    [["normalMapGuid": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"]]
    [["type": "PointLight2D"]]
    [["castsShadows": true]]
    [["type": "ShadowOccluder2D"]]
    [["shape": "Polygon"]]
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
    [["schemaVersion": 2]]
    [["gameName": "SmokeGame"]]
    [["productVersion": "0.1.0"]]
    [["developmentBuild": true]]
    [["mainScene": "Scenes/main.json"]]
    [["startupSceneId": "Scenes/main.json"]]
    [["windowWidth": 640]]
    [["windowHeight": 360]]
    [["resizable": false]]
    [["outputScaleMode": "IntegerFit"]]
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

foreach(bus_name Master Music SFX Voice UI)
    string(JSON bus_volume GET "${game_config}"
        projectSettings audio buses ${bus_name} volume)
    string(JSON bus_muted GET "${game_config}"
        projectSettings audio buses ${bus_name} muted)
    if(bus_volume LESS 0 OR bus_volume GREATER 1 OR bus_muted)
        message(FATAL_ERROR
            "Packaged fixed audio bus ${bus_name} has invalid defaults")
    endif()
endforeach()
string(JSON master_bus_volume GET "${game_config}"
    projectSettings audio buses Master volume)
string(JSON music_bus_volume GET "${game_config}"
    projectSettings audio buses Music volume)
string(JSON sfx_bus_volume GET "${game_config}"
    projectSettings audio buses SFX volume)
if(master_bus_volume LESS 0.89 OR master_bus_volume GREATER 0.91 OR
   music_bus_volume LESS 0.54 OR music_bus_volume GREATER 0.56 OR
   sfx_bus_volume LESS 0.74 OR sfx_bus_volume GREATER 0.76)
    message(FATAL_ERROR "Packaged Master/Music/SFX mixer defaults changed")
endif()

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

if(NOT EXISTS "${P1_AUDIO_REPORT}")
    message(FATAL_ERROR
        "Packaged P1 user script did not execute its audio/particle sequence")
endif()
file(READ "${P1_AUDIO_REPORT}" p1_audio_report)
foreach(expected_p1_audio_call
    [[playMusicGuid=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa]]
    [[crossFadeMusicGuid=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb]]
    [[oneShotGuid=cccccccccccccccccccccccccccccccc]]
    [[crossFadeSeconds=0.25]]
    [[oneShotBus=SFX]]
    [[particleBurstCount=24]]
    [[particleBurstEmitted=true]]
    [[animatorSpeedSet=true]]
    [[animatorJumpTriggered=true]]
    [[animatorRunObserved=true]]
    [[animatorJumpObserved=true]]
)
    string(FIND "${p1_audio_report}" "${expected_p1_audio_call}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Packaged P1 script report is missing ${expected_p1_audio_call}\n"
            "${p1_audio_report}")
    endif()
endforeach()

if(NOT EXISTS "${P2_SCRIPT_FAULT_REPORT}")
    message(FATAL_ERROR "Packaged Script fault-isolation report was not created")
endif()
file(READ "${P2_SCRIPT_FAULT_REPORT}" p2_script_fault_report)
foreach(expected_p2_fault_event
    "faultCallbackEntered=true"
    "faultOnDisable=true"
    "peerContinued=true"
)
    string(FIND "${p2_script_fault_report}" "${expected_p2_fault_event}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Packaged Script isolation report is missing ${expected_p2_fault_event}\n"
            "${p2_script_fault_report}")
    endif()
endforeach()

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
    [["postProcessed": true]]
    [["postProcessFallback": false]]
    [["postProcessProfileGuid": "dddddddddddddddddddddddddddddddd"]]
    [["selectedCameraCount": 2]]
    [["renderedCameraCount": 2]]
    [["postProcessedCameraCount": 1]]
    [["postProcessFallbackCameraCount": 0]]
    [["lightingAppliedCameraCount": 1]]
    [["lightingFallbackCameraCount": 0]]
    [["shadowFallbackCameraCount": 0]]
    [["selectedLightCount": 1]]
    [["shadowedLightCount": 1]]
    [["shadowCasterDrawCount": 1]]
    [["outputCameraPasses": 2]]
)
if(DEFINED DUMMY_LIB AND EXISTS "${DUMMY_LIB}")
    list(APPEND EXPECTED_REPORTS "Scripts: loaded")
    list(APPEND EXPECTED_REPORTS "UserScriptComponents: 3")
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

string(JSON runtime_postfx_passes GET "${runtime_report}" postProcessPasses)
if(runtime_postfx_passes LESS 1)
    message(FATAL_ERROR "Runtime did not execute a post-process frame")
endif()
string(JSON runtime_lighting_passes GET "${runtime_report}" lightingPasses)
string(JSON runtime_shadow_passes GET "${runtime_report}" shadowPasses)
if(runtime_lighting_passes LESS 1 OR runtime_shadow_passes LESS 1)
    message(FATAL_ERROR "Runtime did not execute lighting and shadow passes")
endif()
