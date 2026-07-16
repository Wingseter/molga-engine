if(NOT DEFINED FIXTURE_ROOT)
    message(FATAL_ERROR "FIXTURE_ROOT is required")
endif()

file(REMOVE_RECURSE "${FIXTURE_ROOT}")
file(MAKE_DIRECTORY
    "${FIXTURE_ROOT}/Assets/Textures"
    "${FIXTURE_ROOT}/Assets/Fonts"
    "${FIXTURE_ROOT}/Assets/Prefabs"
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
      "type": "MyUserScript", "enabled": true
    }]
  }, {
    "name": "Stage1Button", "id": 2002, "active": true, "parentId": -1,
    "components": [{
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
    }]
  }, {
    "name": "Player", "id": 2003, "active": true, "parentId": -1,
    "components": [{
      "type": "Transform", "enabled": true,
      "position": [160.0, 110.0], "rotation": 0.0, "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer", "enabled": true,
      "textureGuid": "11111111111111111111111111111111",
      "color": [0.95, 0.85, 0.3, 1.0], "size": [32.0, 48.0],
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

file(WRITE "${FIXTURE_ROOT}/ProjectSettings/build_profile.json" [=[
{
  "schemaVersion": 1,
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
    "resizable": true
  },
  "developmentBuild": true,
  "showConsole": false,
  "target": "host"
}
]=])
