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

# stb_image의 PNM loader는 binary P6만 지원한다.
# header 뒤의 세 문자는 RGB bytes 64, 32, 96이다.
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
    }]
  }]
}
]=])
