# Maintainer-only acquisition of the immutable text runtime inputs.
#
# Normal configure/build never runs this script. It is invoked explicitly:
#   cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextRuntimeData.cmake
#
# Every byte is validated against an approved SHA-256 in a unique temporary
# root first; destinations are then replaced by one atomic transaction.

cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT must name the repository root")
endif()
get_filename_component(SOURCE_ROOT "${SOURCE_ROOT}" ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/TextArtifactTransaction.cmake")

# ── Approved immutable inputs ────────────────────────────────────────────────
set(TEXT_ICU_ARCHIVE_URL
    "https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-data-bin-l.zip")
set(TEXT_ICU_ARCHIVE_SHA256
    "982619632b78887f1895b063e96e8c3cc7f99283337c8abbd05aa71635de613c")
set(TEXT_ICU_DATA_SHA256
    "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b")
set(TEXT_ICU_DATA_SIZE 33107232)
set(TEXT_RASTERIZER_SHA256
    "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528")
set(TEXT_STB_LICENSE_SHA256
    "7587efcca32db8f95bf5860dea6dfa4be410ee723e1964db3689c8ddf2a2e4a9")
set(TEXT_HARFBUZZ_LICENSE_SHA256
    "ba8f810f2455c2f08e2d56bb49b72f37fcf68f1f4fade38977cfd7372050ad64")
set(TEXT_ICU_LICENSE_SHA256
    "e55522d81edc687a341a4411e0776e54ca654e90147f354a90458aaced4116af")
set(TEXT_INTER_LICENSE_URL
    "https://raw.githubusercontent.com/rsms/inter/2ce9119398be143fa289c3e180824db1b7ed803e/LICENSE.txt")
set(TEXT_INTER_LICENSE_SHA256
    "262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a")
set(TEXT_RASTERIZER_LICENSE_FIRST_LINE 5045)
set(TEXT_RASTERIZER_LICENSE_LAST_LINE 5085)

set(TEXT_ACQUIRE_TMP "${SOURCE_ROOT}/.text-acquire-tmp")
set(TEXT_JOURNAL "${TEXT_ACQUIRE_TMP}/artifact-journal.json")
set(TEXT_ALLOWED_ROOTS
    "${TEXT_ACQUIRE_TMP}"
    "${SOURCE_ROOT}/resources"
    "${SOURCE_ROOT}/external/text"
    "${SOURCE_ROOT}/assets/fonts")

if(NOT DEFINED INJECT_PUBLISH_FAILURE_AT)
    set(INJECT_PUBLISH_FAILURE_AT 0)
endif()

# Every network read funnels through here so the offline fast path is provable.
function(text_download url output expected_sha)
    if(TEXT_FAIL_ON_NETWORK)
        message(FATAL_ERROR
            "network access attempted while TEXT_FAIL_ON_NETWORK=ON: ${url}")
    endif()
    file(DOWNLOAD "${url}" "${output}"
         EXPECTED_HASH "SHA256=${expected_sha}" STATUS download_status)
    list(GET download_status 0 download_code)
    if(NOT download_code EQUAL 0)
        message(FATAL_ERROR "download failed for ${url}: ${download_status}")
    endif()
endfunction()

function(text_require_file_sha path expected_sha label)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${label} is missing: ${path}")
    endif()
    file(SHA256 "${path}" actual_sha)
    if(NOT actual_sha STREQUAL expected_sha)
        message(FATAL_ERROR "${label} hash mismatch: ${actual_sha}")
    endif()
endfunction()

# ── Offline transaction self-test ────────────────────────────────────────────
# Proves both crash windows on synthetic files without touching real inputs:
#   cmake -DSOURCE_ROOT="$PWD" -DTEXT_TRANSACTION_SELF_TEST=ON \
#         -DINJECT_PUBLISH_FAILURE_AT=3 -P cmake/AcquireTextRuntimeData.cmake
if(TEXT_TRANSACTION_SELF_TEST)
    string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef fixture_id)
    set(fixture_root "${TEXT_ACQUIRE_TMP}/selftest-${fixture_id}")
    set(fixture_journal "${fixture_root}/artifact-journal.json")
    file(MAKE_DIRECTORY "${fixture_root}/staged")
    file(MAKE_DIRECTORY "${fixture_root}/destination")

    set(fixture_pairs "")
    set(fixture_destinations "")
    set(fixture_old_shas "")
    set(fixture_new_shas "")
    foreach(fixture_index RANGE 1 5)
        set(staged "${fixture_root}/staged/artifact-${fixture_index}.bin")
        set(destination "${fixture_root}/destination/artifact-${fixture_index}.bin")
        file(WRITE "${destination}" "old bytes for artifact ${fixture_index}\n")
        file(WRITE "${staged}" "new bytes for artifact ${fixture_index}\n")
        file(SHA256 "${destination}" old_sha)
        file(SHA256 "${staged}" new_sha)
        list(APPEND fixture_pairs "${staged}" "${destination}")
        list(APPEND fixture_destinations "${destination}")
        list(APPEND fixture_old_shas "${old_sha}")
        list(APPEND fixture_new_shas "${new_sha}")
    endforeach()

    function(text_selftest_require_destination_shas expected_shas label)
        set(index 0)
        foreach(destination IN LISTS fixture_destinations)
            list(GET expected_shas ${index} expected_sha)
            text_require_file_sha("${destination}" "${expected_sha}"
                                  "${label} artifact ${index}")
            math(EXPR index "${index} + 1")
        endforeach()
    endfunction()

    function(text_selftest_require_no_transaction_residue label)
        file(GLOB residue LIST_DIRECTORIES true
             "${fixture_root}/destination/.*.molga-new-*"
             "${fixture_root}/destination/.*.molga-backup-*"
             "${fixture_root}/.molga-text-txn-*")
        if(residue)
            message(FATAL_ERROR "${label} left transaction residue: ${residue}")
        endif()
        if(EXISTS "${fixture_journal}")
            message(FATAL_ERROR "${label} left a journal behind")
        endif()
    endfunction()

    if(INJECT_PUBLISH_FAILURE_AT EQUAL 0)
        message(FATAL_ERROR
            "the self-test requires a nonzero INJECT_PUBLISH_FAILURE_AT")
    endif()

    text_publish_artifact_set(rollback_result "${fixture_journal}"
        "${INJECT_PUBLISH_FAILURE_AT}" "${fixture_root}" ${fixture_pairs})
    if(rollback_result)
        message(FATAL_ERROR
            "injected failure ${INJECT_PUBLISH_FAILURE_AT} reported success")
    endif()
    text_selftest_require_destination_shas("${fixture_old_shas}" "rollback")
    text_selftest_require_no_transaction_residue("rollback")

    text_publish_artifact_set(publish_result "${fixture_journal}" 0
        "${fixture_root}" ${fixture_pairs})
    if(NOT publish_result)
        message(FATAL_ERROR "clean rerun after rollback failed")
    endif()
    text_selftest_require_destination_shas("${fixture_new_shas}" "clean publish")
    text_selftest_require_no_transaction_residue("clean publish")

    file(REMOVE_RECURSE "${fixture_root}")
    message(STATUS "rollback verified; clean publish verified")
    return()
endif()

# ── Real runtime acquisition ─────────────────────────────────────────────────
set(TEXT_RUNTIME_DESTINATIONS
    "${SOURCE_ROOT}/resources/text/icudt78l.dat"
    "${SOURCE_ROOT}/external/text/rasterizer/imstb_truetype.h"
    "${SOURCE_ROOT}/external/text/rasterizer/LICENSE.txt"
    "${SOURCE_ROOT}/resources/licenses/StbTrueType.txt"
    "${SOURCE_ROOT}/resources/licenses/HarfBuzz.txt"
    "${SOURCE_ROOT}/resources/licenses/ICU.txt"
    "${SOURCE_ROOT}/assets/fonts/Inter-v4.0-OFL.txt"
    "${SOURCE_ROOT}/resources/licenses/ThirdPartyNotices.md")

# The engine-only notice base is generated from the four verified logical
# license names; package-specific font rows are added by a later packaging task.
set(TEXT_NOTICE_BASE "# Third-Party Notices

This engine build includes the following third-party components. Each entry
lists the license file published beside this notice and the SHA-256 of that
file as verified during acquisition.

## HarfBuzz

- License: `resources/licenses/HarfBuzz.txt`
- SHA-256: `${TEXT_HARFBUZZ_LICENSE_SHA256}`

## ICU4C

- License: `resources/licenses/ICU.txt`
- SHA-256: `${TEXT_ICU_LICENSE_SHA256}`

## stb_truetype

- License: `resources/licenses/StbTrueType.txt`
- SHA-256: `${TEXT_STB_LICENSE_SHA256}`

## Inter

- License: `assets/fonts/Inter-v4.0-OFL.txt`
- SHA-256: `${TEXT_INTER_LICENSE_SHA256}`
")
string(SHA256 TEXT_NOTICE_BASE_SHA256 "${TEXT_NOTICE_BASE}")

set(TEXT_RUNTIME_EXPECTED_SHAS
    "${TEXT_ICU_DATA_SHA256}"
    "${TEXT_RASTERIZER_SHA256}"
    "${TEXT_STB_LICENSE_SHA256}"
    "${TEXT_STB_LICENSE_SHA256}"
    "${TEXT_HARFBUZZ_LICENSE_SHA256}"
    "${TEXT_ICU_LICENSE_SHA256}"
    "${TEXT_INTER_LICENSE_SHA256}"
    "${TEXT_NOTICE_BASE_SHA256}")

# Finish any interrupted transaction before deciding whether work remains.
text_recover_artifact_journal(recovery_ok "${TEXT_JOURNAL}" "${TEXT_ALLOWED_ROOTS}")
if(NOT recovery_ok)
    message(FATAL_ERROR "text artifact journal recovery failed")
endif()

# Step 14: exact-destination fast path, evaluated before any network access.
set(text_runtime_complete TRUE)
set(destination_index 0)
foreach(destination IN LISTS TEXT_RUNTIME_DESTINATIONS)
    list(GET TEXT_RUNTIME_EXPECTED_SHAS ${destination_index} expected_sha)
    if(NOT EXISTS "${destination}")
        set(text_runtime_complete FALSE)
        break()
    endif()
    file(SHA256 "${destination}" actual_sha)
    if(NOT actual_sha STREQUAL expected_sha)
        set(text_runtime_complete FALSE)
        break()
    endif()
    math(EXPR destination_index "${destination_index} + 1")
endforeach()
if(text_runtime_complete)
    file(SIZE "${SOURCE_ROOT}/resources/text/icudt78l.dat" icu_data_size)
    if(icu_data_size EQUAL TEXT_ICU_DATA_SIZE)
        message(STATUS "all runtime text artifacts already verified")
        return()
    endif()
endif()

# Step 6: create the unique acquisition root and snapshot the rasterizer there.
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef acquire_id)
set(acquire_root "${TEXT_ACQUIRE_TMP}/${acquire_id}")
file(MAKE_DIRECTORY "${acquire_root}")
file(COPY_FILE
    "${SOURCE_ROOT}/external/imgui/imstb_truetype.h"
    "${acquire_root}/imstb_truetype.h" ONLY_IF_DIFFERENT)
text_require_file_sha("${acquire_root}/imstb_truetype.h"
                      "${TEXT_RASTERIZER_SHA256}" "text rasterizer snapshot")

# Step 7: download the ICU data archive and extract exactly one member.
text_download("${TEXT_ICU_ARCHIVE_URL}" "${acquire_root}/icu.zip"
              "${TEXT_ICU_ARCHIVE_SHA256}")
file(ARCHIVE_EXTRACT INPUT "${acquire_root}/icu.zip"
     DESTINATION "${acquire_root}/icu")
file(GLOB_RECURSE data_candidates
     LIST_DIRECTORIES false
     "${acquire_root}/icu/*icudt78l.dat")
list(LENGTH data_candidates data_count)
if(NOT data_count EQUAL 1)
    message(FATAL_ERROR "expected exactly one icudt78l.dat, found ${data_count}")
endif()
get_filename_component(data_name "${data_candidates}" NAME)
if(NOT data_name STREQUAL "icudt78l.dat")
    message(FATAL_ERROR "unexpected ICU data member name: ${data_name}")
endif()
file(SIZE "${data_candidates}" data_size)
if(NOT data_size EQUAL TEXT_ICU_DATA_SIZE)
    message(FATAL_ERROR "ICU data member size mismatch: ${data_size}")
endif()
text_require_file_sha("${data_candidates}" "${TEXT_ICU_DATA_SHA256}" "ICU data")

# Step 8: stage the vendored dependency licenses.
file(COPY_FILE "${SOURCE_ROOT}/external/harfbuzz/COPYING"
     "${acquire_root}/HarfBuzz.txt" ONLY_IF_DIFFERENT)
text_require_file_sha("${acquire_root}/HarfBuzz.txt"
                      "${TEXT_HARFBUZZ_LICENSE_SHA256}" "HarfBuzz license")
file(COPY_FILE "${SOURCE_ROOT}/external/icu/LICENSE"
     "${acquire_root}/ICU.txt" ONLY_IF_DIFFERENT)
text_require_file_sha("${acquire_root}/ICU.txt"
                      "${TEXT_ICU_LICENSE_SHA256}" "ICU license")

# Extract the rasterizer's own license block, preserving LF line endings.
file(READ "${acquire_root}/imstb_truetype.h" rasterizer_text)
string(REPLACE ";" "\;" rasterizer_text "${rasterizer_text}")
string(REPLACE "\n" ";" rasterizer_lines "${rasterizer_text}")
set(stb_license_text "")
math(EXPR license_first_offset "${TEXT_RASTERIZER_LICENSE_FIRST_LINE} - 1")
math(EXPR license_last_offset "${TEXT_RASTERIZER_LICENSE_LAST_LINE} - 1")
foreach(line_offset RANGE ${license_first_offset} ${license_last_offset})
    list(GET rasterizer_lines ${line_offset} license_line)
    string(APPEND stb_license_text "${license_line}\n")
endforeach()
file(WRITE "${acquire_root}/StbTrueType.txt" "${stb_license_text}")
text_require_file_sha("${acquire_root}/StbTrueType.txt"
                      "${TEXT_STB_LICENSE_SHA256}" "stb_truetype license")

# Step 8a: stage the pinned Inter license.
text_download("${TEXT_INTER_LICENSE_URL}" "${acquire_root}/Inter-v4.0-OFL.txt"
              "${TEXT_INTER_LICENSE_SHA256}")

# Step 8b: stage the engine-only notice base.
file(WRITE "${acquire_root}/ThirdPartyNotices.md" "${TEXT_NOTICE_BASE}")
text_require_file_sha("${acquire_root}/ThirdPartyNotices.md"
                      "${TEXT_NOTICE_BASE_SHA256}" "third-party notice base")

# Step 14a: publish the exact real runtime set in one transaction.
text_publish_artifact_set(publish_ok "${TEXT_JOURNAL}"
    "${INJECT_PUBLISH_FAILURE_AT}" "${TEXT_ALLOWED_ROOTS}"
    "${data_candidates}"                      "${SOURCE_ROOT}/resources/text/icudt78l.dat"
    "${acquire_root}/imstb_truetype.h"        "${SOURCE_ROOT}/external/text/rasterizer/imstb_truetype.h"
    "${acquire_root}/StbTrueType.txt"         "${SOURCE_ROOT}/external/text/rasterizer/LICENSE.txt"
    "${acquire_root}/StbTrueType.txt"         "${SOURCE_ROOT}/resources/licenses/StbTrueType.txt"
    "${acquire_root}/HarfBuzz.txt"            "${SOURCE_ROOT}/resources/licenses/HarfBuzz.txt"
    "${acquire_root}/ICU.txt"                 "${SOURCE_ROOT}/resources/licenses/ICU.txt"
    "${acquire_root}/Inter-v4.0-OFL.txt"      "${SOURCE_ROOT}/assets/fonts/Inter-v4.0-OFL.txt"
    "${acquire_root}/ThirdPartyNotices.md"    "${SOURCE_ROOT}/resources/licenses/ThirdPartyNotices.md")
if(NOT publish_ok)
    message(FATAL_ERROR "publishing the runtime text artifact set failed")
endif()

# Step 12d: remove only this caller's exact acquisition root.
file(REMOVE_RECURSE "${acquire_root}")
message(STATUS "published the runtime text artifact set")
