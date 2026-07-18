if(NOT DEFINED FIXTURE_ROOT)
    message(FATAL_ERROR "FIXTURE_ROOT is required")
endif()

file(REMOVE_RECURSE "${FIXTURE_ROOT}")
file(MAKE_DIRECTORY
    "${FIXTURE_ROOT}/Assets/Textures"
    "${FIXTURE_ROOT}/Assets/Fonts"
    "${FIXTURE_ROOT}/Assets/Prefabs"
    "${FIXTURE_ROOT}/Assets/Animations"
    "${FIXTURE_ROOT}/Assets/Tilemaps"
    "${FIXTURE_ROOT}/Assets/Audio"
    "${FIXTURE_ROOT}/Scenes"
)

file(COPY
    "${CMAKE_CURRENT_LIST_DIR}/../fixtures/fonts/NotoSansKR-Regular.ttf"
    "${CMAKE_CURRENT_LIST_DIR}/../fixtures/fonts/OFL.txt"
    DESTINATION "${FIXTURE_ROOT}/Assets/Fonts")
file(WRITE "${FIXTURE_ROOT}/Assets/Fonts/NotoSansKR-Regular.ttf.meta" [=[
{
  "guid": "22222222222222222222222222222222",
  "importer": "FontImporter",
  "importerVersion": 1
}
]=])

file(WRITE "${FIXTURE_ROOT}/project.molga" [=[
{
  "name": "SmokeProject",
  "version": "1.0",
  "mainScene": "Scenes/main.json"
}
]=])

# Copy dummy user script library if DUMMY_LIB is defined and exists
if(DEFINED DUMMY_LIB AND EXISTS "${DUMMY_LIB}")
    file(MAKE_DIRECTORY "${FIXTURE_ROOT}/Scripts/build")
    if(WIN32)
        set(LIB_NAME "UserScripts.dll")
    elseif(APPLE)
        set(LIB_NAME "libUserScripts.dylib")
    else()
        set(LIB_NAME "libUserScripts.so")
    endif()
    file(COPY "${DUMMY_LIB}" DESTINATION "${FIXTURE_ROOT}/Scripts/build")
    get_filename_component(DUMMY_NAME "${DUMMY_LIB}" NAME)
    file(RENAME "${FIXTURE_ROOT}/Scripts/build/${DUMMY_NAME}" "${FIXTURE_ROOT}/Scripts/build/${LIB_NAME}")
endif()

# stb_image's PNM loader only supports binary P6.
# Three characters after header are RGB bytes 64, 32, 96.
file(WRITE "${FIXTURE_ROOT}/Assets/Textures/smoke.ppm"
    "P6\n1 1\n255\n@ `")
file(WRITE "${FIXTURE_ROOT}/Assets/Textures/smoke.ppm.meta" [=[
{
  "guid": "11111111111111111111111111111111",
  "importer": "TextureImporter",
  "importerVersion": 1
}
]=])

# P1 character-stage asset graph. The six-pixel P6 image is deliberately tiny,
# but each source pixel is a stable sprite slice. This keeps the process E2E
# fixture deterministic while still exercising nearest filtering, SRGB import,
# PPU/native sizing and GUID -> slice resolution in the packaged runtime.
file(WRITE "${FIXTURE_ROOT}/Assets/Textures/p1_character.ppm"
    "P6\n6 1\n255\nAa1Bb2Cc3Dd4Ee5Ff6")
file(WRITE "${FIXTURE_ROOT}/Assets/Textures/p1_character.ppm.meta" [=[
{
  "guid": "44444444444444444444444444444444",
  "importer": "TextureImporter",
  "importerVersion": 2,
  "settings": {
    "filter": "Nearest",
    "wrapU": "Clamp",
    "wrapV": "Clamp",
    "mipmaps": false,
    "colorSpace": "SRGB",
    "pixelsPerUnit": 0.03125,
    "spriteMode": "Multiple",
    "defaultPivot": [0.5, 1.0],
    "slices": [
      {"id": "d1111111111111111111111111111111", "name": "idle_0", "rect": [0, 0, 1, 1], "pivot": [0.5, 1.0]},
      {"id": "d2222222222222222222222222222222", "name": "idle_1", "rect": [1, 0, 1, 1], "pivot": [0.5, 1.0]},
      {"id": "d3333333333333333333333333333333", "name": "run_0",  "rect": [2, 0, 1, 1], "pivot": [0.5, 1.0]},
      {"id": "d4444444444444444444444444444444", "name": "run_1",  "rect": [3, 0, 1, 1], "pivot": [0.5, 1.0]},
      {"id": "d5555555555555555555555555555555", "name": "jump_0", "rect": [4, 0, 1, 1], "pivot": [0.5, 1.0]},
      {"id": "d6666666666666666666666666666666", "name": "jump_1", "rect": [5, 0, 1, 1], "pivot": [0.5, 1.0]}
    ]
  }
}
]=])

file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player_idle.animclip" [=[
{
  "schemaVersion": 1,
  "textureGuid": "44444444444444444444444444444444",
  "loop": true,
  "frames": [
    {"sliceId": "d1111111111111111111111111111111", "durationSeconds": 0.25},
    {"sliceId": "d2222222222222222222222222222222", "durationSeconds": 0.25}
  ]
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player_idle.animclip.meta" [=[
{
  "guid": "55555555555555555555555555555555",
  "importer": "AnimationClipImporter",
  "importerVersion": 1
}
]=])

file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player_run.animclip" [=[
{
  "schemaVersion": 1,
  "textureGuid": "44444444444444444444444444444444",
  "loop": true,
  "frames": [
    {"sliceId": "d3333333333333333333333333333333", "durationSeconds": 0.08},
    {"sliceId": "d4444444444444444444444444444444", "durationSeconds": 0.08}
  ]
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player_run.animclip.meta" [=[
{
  "guid": "66666666666666666666666666666666",
  "importer": "AnimationClipImporter",
  "importerVersion": 1
}
]=])

file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player_jump.animclip" [=[
{
  "schemaVersion": 1,
  "textureGuid": "44444444444444444444444444444444",
  "loop": false,
  "frames": [
    {"sliceId": "d5555555555555555555555555555555", "durationSeconds": 0.12},
    {"sliceId": "d6666666666666666666666666666666", "durationSeconds": 0.18}
  ]
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player_jump.animclip.meta" [=[
{
  "guid": "77777777777777777777777777777777",
  "importer": "AnimationClipImporter",
  "importerVersion": 1
}
]=])

file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player.animator" [=[
{
  "schemaVersion": 1,
  "parameters": [
    {"name": "Grounded", "type": "Bool", "defaultValue": true},
    {"name": "Speed", "type": "Float", "defaultValue": 0.0},
    {"name": "Jump", "type": "Trigger", "defaultValue": false}
  ],
  "states": [
    {"id": "idle-state", "name": "Idle", "clipGuid": "55555555555555555555555555555555", "speed": 1.0},
    {"id": "run-state", "name": "Run", "clipGuid": "66666666666666666666666666666666", "speed": 1.0},
    {"id": "jump-state", "name": "Jump", "clipGuid": "77777777777777777777777777777777", "speed": 1.0}
  ],
  "defaultStateId": "idle-state",
  "transitions": [
    {"fromStateId": "idle-state", "toStateId": "jump-state", "hasExitTime": false, "exitTime": 0.0,
     "conditions": [{"parameter": "Jump", "operator": "IsTrue", "value": true}]},
    {"fromStateId": "idle-state", "toStateId": "run-state", "hasExitTime": false, "exitTime": 0.0,
     "conditions": [{"parameter": "Speed", "operator": "Greater", "value": 0.1}]},
    {"fromStateId": "run-state", "toStateId": "jump-state", "hasExitTime": false, "exitTime": 0.0,
     "conditions": [{"parameter": "Jump", "operator": "IsTrue", "value": true}]},
    {"fromStateId": "run-state", "toStateId": "idle-state", "hasExitTime": false, "exitTime": 0.0,
     "conditions": [{"parameter": "Speed", "operator": "LessOrEqual", "value": 0.1}]},
    {"fromStateId": "jump-state", "toStateId": "idle-state", "hasExitTime": true, "exitTime": 0.9,
     "conditions": [{"parameter": "Grounded", "operator": "IsTrue", "value": true}]}
  ]
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Animations/player.animator.meta" [=[
{
  "guid": "88888888888888888888888888888888",
  "importer": "AnimatorControllerImporter",
  "importerVersion": 1
}
]=])

# Every NESW mask has an explicit terrain rule. Background and foreground tiles
# are non-solid; the collision layer uses the solid terrain variants.
file(WRITE "${FIXTURE_ROOT}/Assets/Tilemaps/platform.tileset" [=[
{
  "schemaVersion": 1,
  "cellSize": [32, 32],
  "tiles": [
    {"id": 0,  "name": "terrain_0",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d1111111111111111111111111111111"}, "solid": true, "terrainId": 1},
    {"id": 1,  "name": "terrain_1",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d2222222222222222222222222222222"}, "solid": true, "terrainId": 1},
    {"id": 2,  "name": "terrain_2",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d3333333333333333333333333333333"}, "solid": true, "terrainId": 1},
    {"id": 3,  "name": "terrain_3",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d4444444444444444444444444444444"}, "solid": true, "terrainId": 1},
    {"id": 4,  "name": "terrain_4",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d5555555555555555555555555555555"}, "solid": true, "terrainId": 1},
    {"id": 5,  "name": "terrain_5",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d6666666666666666666666666666666"}, "solid": true, "terrainId": 1},
    {"id": 6,  "name": "terrain_6",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d1111111111111111111111111111111"}, "solid": true, "terrainId": 1},
    {"id": 7,  "name": "terrain_7",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d2222222222222222222222222222222"}, "solid": true, "terrainId": 1},
    {"id": 8,  "name": "terrain_8",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d3333333333333333333333333333333"}, "solid": true, "terrainId": 1},
    {"id": 9,  "name": "terrain_9",  "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d4444444444444444444444444444444"}, "solid": true, "terrainId": 1},
    {"id": 10, "name": "terrain_10", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d5555555555555555555555555555555"}, "solid": true, "terrainId": 1},
    {"id": 11, "name": "terrain_11", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d6666666666666666666666666666666"}, "solid": true, "terrainId": 1},
    {"id": 12, "name": "terrain_12", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d1111111111111111111111111111111"}, "solid": true, "terrainId": 1},
    {"id": 13, "name": "terrain_13", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d2222222222222222222222222222222"}, "solid": true, "terrainId": 1},
    {"id": 14, "name": "terrain_14", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d3333333333333333333333333333333"}, "solid": true, "terrainId": 1},
    {"id": 15, "name": "terrain_15", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d4444444444444444444444444444444"}, "solid": true, "terrainId": 1},
    {"id": 16, "name": "background", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d5555555555555555555555555555555"}, "solid": false, "terrainId": -1},
    {"id": 17, "name": "foreground", "sprite": {"textureGuid": "44444444444444444444444444444444", "sliceId": "d6666666666666666666666666666666"}, "solid": false, "terrainId": -1}
  ],
  "terrainRules": [
    {"terrainId": 1, "mask": 0,  "tileId": 0},
    {"terrainId": 1, "mask": 1,  "tileId": 1},
    {"terrainId": 1, "mask": 2,  "tileId": 2},
    {"terrainId": 1, "mask": 3,  "tileId": 3},
    {"terrainId": 1, "mask": 4,  "tileId": 4},
    {"terrainId": 1, "mask": 5,  "tileId": 5},
    {"terrainId": 1, "mask": 6,  "tileId": 6},
    {"terrainId": 1, "mask": 7,  "tileId": 7},
    {"terrainId": 1, "mask": 8,  "tileId": 8},
    {"terrainId": 1, "mask": 9,  "tileId": 9},
    {"terrainId": 1, "mask": 10, "tileId": 10},
    {"terrainId": 1, "mask": 11, "tileId": 11},
    {"terrainId": 1, "mask": 12, "tileId": 12},
    {"terrainId": 1, "mask": 13, "tileId": 13},
    {"terrainId": 1, "mask": 14, "tileId": 14},
    {"terrainId": 1, "mask": 15, "tileId": 15}
  ]
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Tilemaps/platform.tileset.meta" [=[
{
  "guid": "99999999999999999999999999999999",
  "importer": "TileSetImporter",
  "importerVersion": 1
}
]=])

# Two valid MPEG-1 Layer III frames avoid platform-specific fixture generators.
# CMake's JSON decoder materializes the few NUL bytes that string(ASCII) cannot,
# so this remains deterministic in script mode without Python/base64 tools. All
# three assets are independently imported/routed even though the payload is shared.
set(P1_MP3_HEX [=[
fffb90c400001428952855a7800b2937e3cb3d4000019013264cca9532e5ccc993325cc98b1a06160c64081a75a6ee49d7c67b789efd277669bb526645828380e01a808e01901501201702707428d0f39cd334d0f51b3c7a4078ac563c794d7cde1c7bdf7ffc5294d7bd29feffa523ddfe1e1801f8000787878786003a00007878787860000000007878787860000000007878787860000000007878787860000000007878787860000000007878787860008ff7fffe8001042040c18c310c394301990882a5af180601d992692b189244f981c02419a6121038920c87aa10d6e44c0c49c4d0d9d4148c260038cc3c558c0c80e40db7103d1649a37c0ea5f031aa00d4a435316e069d6002110324440c817494ebf01708061420185100de506ccaffe000043920b220b0a11c86295fff06d50fd82e182d084da1914314fffe31c20b07c431a28110a8ea1730adbffff1c91408a04750b985cc4345ca39a4d0e77ffffe4f189152e99178d9662ca493b7ffffffead1cc9144c5248c9144ba9245e58caa8980180048100070b00a8606c81e8607b89e860ee879461f78320617b8e8fffb92c41003d7d1e9021dfa000a2ea76041fe21490653891c07d47ab86656c108a0e1ce0c09901f8c10d04d8c0a100b4c06a01640d9b8030664009c93aecca5a0eb4197456b5a996a5d4ab2d27a9155d34966268c9548b2ddde82ba967139818a90d249acfa4bd14abfa2baeea4d999eada7754c99482374d6d551495a2c85d692bd96b5275a1992d3414a5ad7553eb7a4f531f6a4eebad05a08eeeb649cfd94c91b24abbba94e82d97a091bbeece47e761c0fde576623a4a0001817826d81001700800c600e801c603700ae7d2125487220a838dc2c194e26acfd8b8c1a55715c7f55cedeb69573efc3ced2af15e14ebaa7ebfaeb88de6984787ada65fae9bad6e23899b7f89e378b8d6d745f9cbf98be512112e5dd36ea8ae1d99053495ae99ec1a92424ff40776575e947a0e3af6f7632d6c3d4bd9d39debc60c0b92ec2acb7a9982880001303644d000802c6000000e600e0026603780c47ceca9fa609080340a01b44801a4cd73a04944ceb7173f77c47d570d75daa5df6af3171efe81514aaab94dabe2aa26da6a0378eeefefe1ab2ce2a9f69b1ff1cfcf852dd3cd8194
]=])
string(REGEX REPLACE "[ \n\r\t]" "" P1_MP3_HEX "${P1_MP3_HEX}")
string(LENGTH "${P1_MP3_HEX}" P1_MP3_HEX_LENGTH)
math(EXPR P1_MP3_LAST_HEX_OFFSET "${P1_MP3_HEX_LENGTH} - 2")
set(P1_MP3_BYTES "")
foreach(offset RANGE 0 ${P1_MP3_LAST_HEX_OFFSET} 2)
    string(SUBSTRING "${P1_MP3_HEX}" ${offset} 2 hex_byte)
    math(EXPR decimal_byte "0x${hex_byte}")
    if(decimal_byte EQUAL 0)
        string(JSON decoded_byte GET "{\"value\":\"\\u0000\"}" value)
    else()
        string(ASCII ${decimal_byte} decoded_byte)
    endif()
    string(APPEND P1_MP3_BYTES "${decoded_byte}")
endforeach()
file(WRITE "${FIXTURE_ROOT}/Assets/Audio/music_intro.mp3" "${P1_MP3_BYTES}")
file(WRITE "${FIXTURE_ROOT}/Assets/Audio/music_loop.mp3" "${P1_MP3_BYTES}")
file(WRITE "${FIXTURE_ROOT}/Assets/Audio/jump_sfx.mp3" "${P1_MP3_BYTES}")
file(WRITE "${FIXTURE_ROOT}/Assets/Audio/music_intro.mp3.meta" [=[
{
  "guid": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "importer": "AudioImporter",
  "importerVersion": 2,
  "settings": {"loadMode": "Streaming"}
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Audio/music_loop.mp3.meta" [=[
{
  "guid": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "importer": "AudioImporter",
  "importerVersion": 2,
  "settings": {"loadMode": "Streaming"}
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Audio/jump_sfx.mp3.meta" [=[
{
  "guid": "cccccccccccccccccccccccccccccccc",
  "importer": "AudioImporter",
  "importerVersion": 2,
  "settings": {"loadMode": "DecodeOnLoad"}
}
]=])

# A normal editor-authored prefab instance whose UILabel owns another font GUID
# reference. This exercises recursive build validation rather than relying only
# on direct scene components.
file(WRITE "${FIXTURE_ROOT}/Assets/Prefabs/TitleBadge.prefab" [=[
{
  "guid": "33333333333333333333333333333333",
  "version": "1.0",
  "gameObjects": [{
    "name": "TitleBadge",
    "id": 1,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "UICanvas", "enabled": true,
      "referenceResolution": [800.0, 600.0],
      "matchWidthOrHeight": 0.5, "sortingOrder": 0
    }, {
      "type": "RectTransform", "enabled": true,
      "anchorMin": [0.5, 0.16], "anchorMax": [0.5, 0.16],
      "pivot": [0.5, 0.5], "anchoredPosition": [0.0, 0.0],
      "sizeDelta": [420.0, 56.0]
    }, {
      "type": "UILabel", "enabled": true,
      "text": "프리팹 한글 폰트",
      "fontGuid": "22222222222222222222222222222222",
      "fontSizePx": 24.0, "lineSpacing": 1.2,
      "color": [0.8, 0.9, 1.0, 1.0],
      "horizontalAlignment": 1, "verticalAlignment": 1,
      "sortingOrder": 3
    }]
  }]
}
]=])
file(WRITE "${FIXTURE_ROOT}/Assets/Prefabs/TitleBadge.prefab.meta" [=[
{
  "guid": "33333333333333333333333333333333",
  "importer": "PrefabImporter",
  "importerVersion": 1
}
]=])

file(WRITE "${FIXTURE_ROOT}/Scenes/main.json" [=[
{
  "version": "1.0",
  "name": "한글 타이틀",
  "gameObjects": [{
    "name": "TitleButton",
    "id": 1001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "UICanvas",
      "enabled": true,
      "referenceResolution": [800.0, 600.0],
      "matchWidthOrHeight": 0.5,
      "sortingOrder": 0
    }, {
      "type": "RectTransform",
      "enabled": true,
      "anchorMin": [0.5, 0.5],
      "anchorMax": [0.5, 0.5],
      "pivot": [0.5, 0.5],
      "anchoredPosition": [0.0, 0.0],
      "sizeDelta": [360.0, 96.0]
    }, {
      "type": "UIImage",
      "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "tint": [0.18, 0.28, 0.55, 1.0],
      "sortingOrder": 0
    }, {
      "type": "UIButton",
      "enabled": true,
      "interactable": true,
      "normalColor": [0.18, 0.28, 0.55, 0.9],
      "hoverColor": [0.25, 0.42, 0.78, 1.0],
      "pressedColor": [0.12, 0.2, 0.42, 1.0],
      "disabledColor": [0.2, 0.2, 0.2, 0.5],
      "sortingOrder": 1
    }, {
      "type": "UILabel",
      "enabled": true,
      "text": "한글 타이틀 - 시작",
      "fontGuid": "22222222222222222222222222222222",
      "fontSizePx": 34.0,
      "lineSpacing": 1.2,
      "color": [1.0, 1.0, 1.0, 1.0],
      "horizontalAlignment": 1,
      "verticalAlignment": 1,
      "sortingOrder": 2
    }, {
      "type": "SceneLoadButton",
      "enabled": true,
      "fields": {"Scene Path": "Scenes/stage1.json"}
    }]
  }, {
    "name": "OptionButton",
    "id": 1002,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "UICanvas", "enabled": true,
      "referenceResolution": [800.0, 600.0],
      "matchWidthOrHeight": 0.5, "sortingOrder": 1
    }, {
      "type": "RectTransform", "enabled": true,
      "anchorMin": [0.5, 0.72], "anchorMax": [0.5, 0.72],
      "pivot": [0.5, 0.5], "anchoredPosition": [0.0, 0.0],
      "sizeDelta": [300.0, 68.0]
    }, {
      "type": "UIButton", "enabled": true, "interactable": true,
      "normalColor": [0.15, 0.36, 0.38, 0.9],
      "hoverColor": [0.2, 0.5, 0.52, 1.0],
      "pressedColor": [0.1, 0.25, 0.27, 1.0],
      "disabledColor": [0.2, 0.2, 0.2, 0.5],
      "sortingOrder": 1
    }, {
      "type": "UILabel", "enabled": true,
      "text": "옵션: 고대비 켜기",
      "fontGuid": "22222222222222222222222222222222",
      "fontSizePx": 24.0, "lineSpacing": 1.2,
      "color": [1.0, 1.0, 1.0, 1.0],
      "horizontalAlignment": 1, "verticalAlignment": 1,
      "sortingOrder": 2
    }, {
      "type": "PlayerPrefsButton", "enabled": true,
      "fields": {
        "Key": "highContrast",
        "Value": true,
        "Save Immediately": true
      }
    }]
  }, {
    "prefabInstance": {
      "guid": "33333333333333333333333333333333",
      "rootId": 1003,
      "parentId": -1,
      "modifications": []
    }
  }]
}
]=])

file(WRITE "${FIXTURE_ROOT}/Scenes/stage1.json" [=[
{
  "version": "1.0",
  "name": "스테이지 1",
  "gameObjects": [{
    "name": "RotatedGround",
    "id": 2001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "Transform", "enabled": true,
      "position": [160.0, 230.0], "rotation": 14.0, "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer", "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "color": [0.3, 0.75, 0.4, 1.0], "size": [320.0, 36.0],
      "flipX": false, "flipY": false, "sortingOrder": 0
    }, {
      "type": "BoxCollider2D", "enabled": true,
      "offset": [0.0, 0.0], "size": [320.0, 36.0],
      "isTrigger": false, "friction": 0.85, "restitution": 0.05
    }, {
      "type": "ParticleSystem", "enabled": true,
      "schemaVersion": 2, "playOnAwake": true, "looping": false,
      "durationSeconds": 0.35, "presetName": "P1TexturedBurst",
      "blendMode": "Additive", "useAdditiveBlending": true,
      "sortingOrder": 5, "simulationSpace": "World",
      "seed": 20260716, "frameMode": "Random",
      "sprites": [
        {"textureGuid": "44444444444444444444444444444444", "sliceId": "d5555555555555555555555555555555"},
        {"textureGuid": "44444444444444444444444444444444", "sliceId": "d6666666666666666666666666666666"}
      ],
      "config": {
        "spawnRate": 0.0, "maxParticles": 64, "spawnRadius": 8.0,
        "minSpeed": 80.0, "maxSpeed": 140.0,
        "minAngle": 3.6, "maxAngle": 5.8,
        "gravityX": 0.0, "gravityY": 120.0,
        "startSize": 14.0, "endSize": 2.0, "sizeVariance": 2.0,
        "sizeOverLife": {"keys": [
          {"time": 0.0, "value": 14.0},
          {"time": 0.35, "value": 10.0},
          {"time": 1.0, "value": 2.0}
        ]},
        "minRotationSpeed": -3.0, "maxRotationSpeed": 3.0,
        "minLife": 0.4, "maxLife": 0.75,
        "startColor": [1.0, 0.85, 0.25, 1.0],
        "endColor": [1.0, 0.2, 0.05, 0.0],
        "colorOverLife": {"keys": [
          {"time": 0.0, "color": [1.0, 0.85, 0.25, 1.0]},
          {"time": 0.5, "color": [1.0, 0.45, 0.1, 0.8]},
          {"time": 1.0, "color": [1.0, 0.2, 0.05, 0.0]}
        ]}
      }
    }, {
      "type": "AudioSource", "enabled": true,
      "clipGuid": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "clipPath": "",
      "volume": 0.65, "pitch": 1.0, "loop": true,
      "playOnAwake": false, "spatial": false,
      "minDistance": 1.0, "maxDistance": 500.0, "outputBus": "Music"
    }, {
      "type": "MyUserScript", "enabled": true,
      "fields": {
        "Music Intro Guid": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "Music Loop Guid": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "Jump SFX Guid": "cccccccccccccccccccccccccccccccc",
        "Crossfade Seconds": 0.25,
        "Particle Burst Count": 24
      }
    }]
  }, {
    "name": "Stage1Button", "id": 2002, "active": true, "parentId": -1,
    "components": [{
      "type": "Transform", "enabled": true,
      "position": [0.0, 180.0], "rotation": 0.0, "scale": [1.0, 1.0]
    }, {
      "type": "UICanvas", "enabled": true,
      "referenceResolution": [800.0, 600.0], "matchWidthOrHeight": 0.5, "sortingOrder": 0
    }, {
      "type": "RectTransform", "enabled": true,
      "anchorMin": [0.5, 0.1], "anchorMax": [0.5, 0.1], "pivot": [0.5, 0.5],
      "anchoredPosition": [0.0, 0.0], "sizeDelta": [280.0, 72.0]
    }, {
      "type": "UIImage", "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "tint": [0.15, 0.35, 0.2, 1.0], "sortingOrder": 0
    }, {
      "type": "UIButton", "enabled": true, "interactable": true, "sortingOrder": 1
    }, {
      "type": "UILabel", "enabled": true, "text": "스테이지 1 완료",
      "fontGuid": "22222222222222222222222222222222", "fontSizePx": 28.0,
      "lineSpacing": 1.2, "color": [1.0, 1.0, 1.0, 1.0],
      "horizontalAlignment": 1, "verticalAlignment": 1, "sortingOrder": 2
    }, {
      "type": "SceneLoadButton", "enabled": true,
      "fields": {"Scene Path": "Scenes/stage2.json"}
    }, {
      "type": "TilemapRenderer", "enabled": true,
      "schemaVersion": 2, "width": 8, "height": 4, "tileSize": 32,
      "tileSetGuid": "99999999999999999999999999999999",
      "activeLayerId": "e2222222222222222222222222222222",
      "sortingOrder": -20,
      "layers": [
        {
          "id": "e1111111111111111111111111111111", "name": "Background",
          "visible": true, "locked": true, "collision": false,
          "opacity": 0.65, "sortingOffset": -20,
          "rle": [[32, 16, -1]]
        }, {
          "id": "e2222222222222222222222222222222", "name": "Collision",
          "visible": true, "locked": false, "collision": true,
          "opacity": 1.0, "sortingOffset": 0,
          "rle": [[24, -1, -1], [1, 2, 1], [6, 10, 1], [1, 8, 1]]
        }, {
          "id": "e3333333333333333333333333333333", "name": "Foreground",
          "visible": true, "locked": false, "collision": false,
          "opacity": 0.8, "sortingOffset": 20,
          "rle": [[3, 17, -1], [29, -1, -1]]
        }
      ]
    }, {
      "type": "AudioSource", "enabled": true,
      "clipGuid": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "clipPath": "",
      "volume": 0.65, "pitch": 1.0, "loop": true,
      "playOnAwake": false, "spatial": false,
      "minDistance": 1.0, "maxDistance": 500.0, "outputBus": "Music"
    }]
  }, {
    "name": "Player", "id": 2003, "active": true, "parentId": -1,
    "components": [{
      "type": "Transform", "enabled": true,
      "position": [160.0, 110.0], "rotation": 0.0, "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer", "enabled": true,
      "textureGuid": "44444444444444444444444444444444",
      "texturePath": "Assets/Textures/p1_character.ppm",
      "spriteRef": {
        "textureGuid": "44444444444444444444444444444444",
        "sliceId": "d1111111111111111111111111111111"
      },
      "sizeMode": "Native",
      "color": [0.95, 0.85, 0.3, 1.0], "size": [32.0, 48.0],
      "flipX": false, "flipY": false, "sortingOrder": 2
    }, {
      "type": "Animator2D", "enabled": true,
      "controllerGuid": "88888888888888888888888888888888",
      "speed": 1.0, "autoPlay": true
    }, {
      "type": "AudioSource", "enabled": true,
      "clipGuid": "cccccccccccccccccccccccccccccccc", "clipPath": "",
      "volume": 0.8, "pitch": 1.0, "loop": false,
      "playOnAwake": false, "spatial": false,
      "minDistance": 1.0, "maxDistance": 500.0, "outputBus": "SFX"
    }, {
      "type": "BoxCollider2D", "enabled": true,
      "offset": [0.0, 0.0], "size": [32.0, 48.0],
      "isTrigger": false, "friction": 0.4, "restitution": 0.0
    }, {
      "type": "Rigidbody2D", "enabled": true,
      "bodyType": 2, "gravityScale": 1.0, "mass": 1.0,
      "linearDamping": 0.0, "angularDamping": 0.0,
      "freezeRotation": true, "velocity": [0.0, 0.0],
      "angularVelocity": 0.0
    }, {
      "type": "PlatformerController", "enabled": true,
      "fields": {
        "Move Speed": 220.0, "Jump Speed": 420.0,
        "Ground Check Distance": 8.0, "Ground Layer Mask": -1
      }
    }]
  }]
}
]=])

file(WRITE "${FIXTURE_ROOT}/Scenes/stage2.json" [=[
{
  "version": "1.0",
  "name": "스테이지 2",
  "gameObjects": [{
    "name": "BouncySlope",
    "id": 3001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "Transform", "enabled": true,
      "position": [180.0, 235.0], "rotation": -11.0, "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer", "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "color": [0.75, 0.35, 0.45, 1.0], "size": [300.0, 40.0],
      "flipX": false, "flipY": false, "sortingOrder": 0
    }, {
      "type": "BoxCollider2D", "enabled": true,
      "offset": [0.0, 0.0], "size": [300.0, 40.0],
      "isTrigger": false, "friction": 0.25, "restitution": 0.85
    }, {
      "type": "MyUserScript", "enabled": true
    }, {
      "type": "FaultIsolationProbeScript", "enabled": true
    }, {
      "type": "FaultIsolationPeerScript", "enabled": true
    }]
  }, {
    "name": "Stage2Button", "id": 3002, "active": true, "parentId": -1,
    "components": [{
      "type": "UICanvas", "enabled": true,
      "referenceResolution": [800.0, 600.0], "matchWidthOrHeight": 0.5, "sortingOrder": 0
    }, {
      "type": "RectTransform", "enabled": true,
      "anchorMin": [0.5, 0.1], "anchorMax": [0.5, 0.1], "pivot": [0.5, 0.5],
      "anchoredPosition": [0.0, 0.0], "sizeDelta": [320.0, 72.0]
    }, {
      "type": "UIImage", "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "tint": [0.38, 0.16, 0.22, 1.0], "sortingOrder": 0
    }, {
      "type": "UIButton", "enabled": true, "interactable": true, "sortingOrder": 1
    }, {
      "type": "UILabel", "enabled": true, "text": "스테이지 2 클리어",
      "fontGuid": "22222222222222222222222222222222", "fontSizePx": 28.0,
      "lineSpacing": 1.2, "color": [1.0, 1.0, 1.0, 1.0],
      "horizontalAlignment": 1, "verticalAlignment": 1, "sortingOrder": 2
    }, {
      "type": "SaveSlotButton", "enabled": true,
      "fields": {
        "Slot Name": "clear-slot",
        "Completed Stage": 2,
        "Completed": true,
        "Title": "한글 타이틀"
      }
    }]
  }, {
    "name": "Player", "id": 3003, "active": true, "parentId": -1,
    "components": [{
      "type": "Transform", "enabled": true,
      "position": [180.0, 105.0], "rotation": 0.0, "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer", "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "color": [0.35, 0.75, 1.0, 1.0], "size": [32.0, 48.0],
      "flipX": false, "flipY": false, "sortingOrder": 2
    }, {
      "type": "BoxCollider2D", "enabled": true,
      "offset": [0.0, 0.0], "size": [32.0, 48.0],
      "isTrigger": false, "friction": 0.4, "restitution": 0.0
    }, {
      "type": "Rigidbody2D", "enabled": true,
      "bodyType": 2, "gravityScale": 1.0, "mass": 1.0,
      "linearDamping": 0.0, "angularDamping": 0.0,
      "freezeRotation": true, "velocity": [0.0, 0.0],
      "angularVelocity": 0.0
    }, {
      "type": "PlatformerController", "enabled": true,
      "fields": {
        "Move Speed": 240.0, "Jump Speed": 440.0,
        "Ground Check Distance": 8.0, "Ground Layer Mask": -1
      }
    }]
  }]
}
]=])

file(WRITE "${FIXTURE_ROOT}/ProjectSettings/project_settings.json" [=[
{
  "audio": {
    "buses": {
      "Master": {"volume": 0.9, "muted": false},
      "Music": {"volume": 0.55, "muted": false},
      "SFX": {"volume": 0.75, "muted": false},
      "Voice": {"volume": 1.0, "muted": false},
      "UI": {"volume": 0.8, "muted": false}
    }
  }
}
]=])

file(WRITE "${FIXTURE_ROOT}/ProjectSettings/build_profile.json" [=[
{
  "schemaVersion": 2,
  "gameName": "SmokeGame",
  "productVersion": "0.1.0",
  "companyName": "Molga",
  "outputPath": "Builds/SmokeGame",
  "startupScene": "Scenes/main.json",
  "scenes": ["Scenes/main.json", "Scenes/stage1.json", "Scenes/stage2.json"],
  "window": {
    "width": 640,
    "height": 360,
    "fullscreen": false,
    "resizable": false,
    "outputScaleMode": "IntegerFit"
  },
  "developmentBuild": true,
  "showConsole": false,
  "target": "host"
}
]=])
