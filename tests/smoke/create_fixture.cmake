if(NOT DEFINED FIXTURE_ROOT)
    message(FATAL_ERROR "FIXTURE_ROOT is required")
endif()

file(REMOVE_RECURSE "${FIXTURE_ROOT}")
file(MAKE_DIRECTORY
    "${FIXTURE_ROOT}/Assets/Textures"
    "${FIXTURE_ROOT}/Scenes"
)

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

file(WRITE "${FIXTURE_ROOT}/Scenes/main.json" [=[
{
  "version": "1.0",
  "name": "Smoke Scene",
  "gameObjects": [{
    "name": "SmokeSprite",
    "id": 1001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "Transform",
      "enabled": true,
      "position": [32.0, 48.0],
      "rotation": 0.0,
      "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer",
      "enabled": true,
      "texturePath": "Assets/Textures/smoke.ppm",
      "color": [1.0, 1.0, 1.0, 1.0],
      "size": [1.0, 1.0],
      "flipX": false,
      "flipY": false,
      "sortingOrder": 0
    }, {
      "type": "MyUserScript",
      "enabled": true
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
  "scenes": ["Scenes/main.json"],
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
