# Always-checked repair boundary for the nested ICU raw install.
#
# BUILD_BYPRODUCTS give Ninja file-level rules, but Make does not uniformly
# rerun an ExternalProject build/install when only a byproduct is deleted.
# This wrapper therefore revalidates the whole consumed set every build and
# repairs it from the pinned source and build-tree authorities.
#
# It never baselines an installed copy: installed bytes are always compared
# against the build tree (archives) or the clean pinned source (headers).

cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED MODE OR NOT MODE STREQUAL "RAW_INSTALL_WRAPPER")
    message(FATAL_ERROR "RepairIcuRawInstall requires MODE=RAW_INSTALL_WRAPPER")
endif()
foreach(required ICU_SOURCE ICU_BUILD ICU_RAW_PREFIX MAKE_PROGRAM
                 EXPECTED_HEADER_COUNT EXPECTED_HEADER_MANIFEST_SHA256)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RepairIcuRawInstall requires ${required}")
    endif()
endforeach()

set(build_outputs
    "${ICU_BUILD}/lib/libicuuc.a"
    "${ICU_BUILD}/lib/libicui18n.a"
    "${ICU_BUILD}/stubdata/libicudata.a"
    "${ICU_BUILD}/stubdata/stubdata.ao")

function(icu_rerun_build)
    message(STATUS "repairing nested ICU build outputs")
    foreach(subdir stubdata common i18n)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                    "${MAKE_PROGRAM}" -C "${subdir}"
            WORKING_DIRECTORY "${ICU_BUILD}"
            RESULT_VARIABLE build_result)
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR "rebuilding ICU ${subdir} failed")
        endif()
    endforeach()
endfunction()

function(icu_rerun_install)
    message(STATUS "repairing nested ICU install")
    foreach(subdir common i18n)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1 PKG_CONFIG=false
                    ac_cv_prog_PYTHON= "${MAKE_PROGRAM}" -C "${subdir}" install
            WORKING_DIRECTORY "${ICU_BUILD}"
            RESULT_VARIABLE install_result)
        if(NOT install_result EQUAL 0)
            message(FATAL_ERROR "reinstalling ICU ${subdir} failed")
        endif()
    endforeach()
endfunction()

# 1. The four build-tree outputs are the archive authorities.
set(missing_build FALSE)
foreach(output IN LISTS build_outputs)
    if(NOT EXISTS "${output}")
        set(missing_build TRUE)
    endif()
endforeach()
if(missing_build)
    icu_rerun_build()
    foreach(output IN LISTS build_outputs)
        if(NOT EXISTS "${output}")
            message(FATAL_ERROR "ICU build output still missing after repair: ${output}")
        endif()
    endforeach()
endif()

# 2. Enumerate the pinned public headers from the clean source.
set(header_relatives "")
foreach(component common i18n)
    file(GLOB component_headers
         LIST_DIRECTORIES false
         "${ICU_SOURCE}/${component}/unicode/*.h")
    foreach(header IN LISTS component_headers)
        get_filename_component(header_name "${header}" NAME)
        list(APPEND header_relatives "${component}/unicode/${header_name}")
    endforeach()
endforeach()
list(SORT header_relatives)
list(LENGTH header_relatives header_count)
if(NOT header_count EQUAL EXPECTED_HEADER_COUNT)
    message(FATAL_ERROR
        "expected ${EXPECTED_HEADER_COUNT} pinned ICU headers, found ${header_count}")
endif()

# TextDependencies.cmake defines what counts as a public header and declares
# the matching install byproducts. Hashing the same LF-joined manifest here
# means a narrower or wider definition there cannot leave this wrapper quietly
# repairing a file the build graph no longer declares just because the two
# still agree on 203.
set(header_manifest "")
foreach(relative IN LISTS header_relatives)
    string(APPEND header_manifest "${relative}\n")
endforeach()
string(SHA256 header_manifest_sha "${header_manifest}")
if(NOT header_manifest_sha STREQUAL EXPECTED_HEADER_MANIFEST_SHA256)
    message(FATAL_ERROR
        "pinned ICU header manifest is ${header_manifest_sha}, "
        "expected ${EXPECTED_HEADER_MANIFEST_SHA256}")
endif()

# 3. Compare every consumed installed file against its authority.
function(icu_installed_set_matches out_var)
    set(matches TRUE)
    foreach(archive_name libicuuc libicui18n)
        set(installed "${ICU_RAW_PREFIX}/lib/${archive_name}.a")
        set(authority "${ICU_BUILD}/lib/${archive_name}.a")
        if(NOT EXISTS "${installed}")
            set(matches FALSE)
            break()
        endif()
        file(SHA256 "${installed}" installed_sha)
        file(SHA256 "${authority}" authority_sha)
        if(NOT installed_sha STREQUAL authority_sha)
            set(matches FALSE)
            break()
        endif()
    endforeach()
    if(matches)
        foreach(relative IN LISTS header_relatives)
            get_filename_component(header_name "${relative}" NAME)
            set(installed "${ICU_RAW_PREFIX}/include/unicode/${header_name}")
            set(authority "${ICU_SOURCE}/${relative}")
            if(NOT EXISTS "${installed}")
                set(matches FALSE)
                break()
            endif()
            file(SHA256 "${installed}" installed_sha)
            file(SHA256 "${authority}" authority_sha)
            if(NOT installed_sha STREQUAL authority_sha)
                set(matches FALSE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_var} ${matches} PARENT_SCOPE)
endfunction()

icu_installed_set_matches(installed_ok)
if(NOT installed_ok)
    icu_rerun_install()
    icu_installed_set_matches(installed_ok)
    if(NOT installed_ok)
        message(FATAL_ERROR
            "the nested ICU install does not match its authorities after repair")
    endif()
endif()

message(STATUS
    "nested ICU raw install verified: 2 archives and ${header_count} headers")
