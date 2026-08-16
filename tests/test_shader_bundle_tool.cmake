foreach(required SHADERC FULL_BUNDLE WORK_ROOT SOURCE_SHADERS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}")
set(PACKAGED "${WORK_ROOT}/ShaderBundle")

execute_process(
    COMMAND "${SHADERC}" package-msl --bundle "${FULL_BUNDLE}"
            --output "${PACKAGED}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR
        "MSL packaging failed\n${package_stdout}\n${package_stderr}")
endif()

execute_process(
    COMMAND "${SHADERC}" validate --bundle "${PACKAGED}" --msl-only
    RESULT_VARIABLE validate_result
    OUTPUT_VARIABLE validate_stdout
    ERROR_VARIABLE validate_stderr)
if(NOT validate_result EQUAL 0)
    message(FATAL_ERROR
        "MSL validation failed\n${validate_stdout}\n${validate_stderr}")
endif()

file(GLOB_RECURSE forbidden
    "${PACKAGED}/*.hlsl" "${PACKAGED}/*.spv" "${PACKAGED}/*.dxil"
    "${PACKAGED}/*.vert" "${PACKAGED}/*.frag" "${PACKAGED}/*.json")
list(REMOVE_ITEM forbidden "${PACKAGED}/manifest.json")
if(forbidden)
    message(FATAL_ERROR "forbidden packaged shader payload: ${forbidden}")
endif()

file(GLOB packaged_msl "${PACKAGED}/artifacts/*.msl")
list(LENGTH packaged_msl packaged_msl_count)
if(packaged_msl_count LESS 2)
    message(FATAL_ERROR "MSL package has no stage artifacts")
endif()

list(GET packaged_msl 0 tamper_target)
file(APPEND "${tamper_target}" "tamper")
execute_process(
    COMMAND "${SHADERC}" validate --bundle "${PACKAGED}" --msl-only
    RESULT_VARIABLE tamper_result)
if(tamper_result EQUAL 0)
    message(FATAL_ERROR "tampered MSL artifact was accepted")
endif()

set(FIRST_ROOT "${WORK_ROOT}/first")
set(SECOND_ROOT "${WORK_ROOT}/second")
file(MAKE_DIRECTORY "${FIRST_ROOT}" "${SECOND_ROOT}")
file(COPY "${SOURCE_SHADERS}/default.hlsl" DESTINATION "${FIRST_ROOT}")
file(COPY "${SOURCE_SHADERS}/default.hlsl" DESTINATION "${SECOND_ROOT}")
file(READ "${SOURCE_SHADERS}/default.shader.json" descriptor)
file(WRITE "${FIRST_ROOT}/default.shader.json" "${descriptor}")
string(REPLACE [["name": "default"]] [["name": "Default"]]
       case_collision "${descriptor}")
file(WRITE "${SECOND_ROOT}/default.shader.json" "${case_collision}")
execute_process(
    COMMAND "${SHADERC}" build --descriptors "${FIRST_ROOT}"
            --descriptors "${SECOND_ROOT}"
            --output "${WORK_ROOT}/case-collision"
    RESULT_VARIABLE collision_result)
if(collision_result EQUAL 0)
    message(FATAL_ERROR "case-colliding shader names were accepted")
endif()
