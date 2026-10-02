# Publish the verified Engine/Text pair beside one executable.
#
# Every development executable reads only
# ${TARGET_FILE_DIR}/Engine/Text/{text_dependency_contract.json,icudt78l.dat}
# and never falls back to a source-tree or working-directory path, so this
# script is the only thing that puts those two files there. It runs on every
# build, which is what repairs a deleted or tampered copy even when the
# consuming target does not relink.
#
# Publication is transactional: a crash at any point leaves each destination
# holding either its complete old bytes or its complete new bytes.

# ── Validate the inputs before touching the development root ─────────────────
foreach(required CONTRACT_SOURCE ICU_DATA_SOURCE DESTINATION_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

foreach(source "${CONTRACT_SOURCE}" "${ICU_DATA_SOURCE}")
    if(NOT EXISTS "${source}" OR IS_DIRECTORY "${source}")
        message(FATAL_ERROR "text runtime source is not a regular file: ${source}")
    endif()
endforeach()

set(expected_icu_bytes 33107232)
set(expected_icu_sha
    "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b")

file(SIZE "${ICU_DATA_SOURCE}" icu_source_bytes)
if(NOT icu_source_bytes EQUAL expected_icu_bytes)
    message(FATAL_ERROR
        "ICU data is ${icu_source_bytes} bytes, expected ${expected_icu_bytes}: "
        "${ICU_DATA_SOURCE}")
endif()
file(SHA256 "${ICU_DATA_SOURCE}" icu_source_sha)
if(NOT icu_source_sha STREQUAL expected_icu_sha)
    message(FATAL_ERROR
        "ICU data SHA-256 is ${icu_source_sha}, expected ${expected_icu_sha}: "
        "${ICU_DATA_SOURCE}")
endif()

# Only now, with the pinned input proven, is the contract's own hash worth
# computing: it is the pair that gets published, never one half of it.
file(SHA256 "${CONTRACT_SOURCE}" contract_source_sha)

# ── Take the root lock and finish any interrupted publication ────────────────
file(MAKE_DIRECTORY "${DESTINATION_ROOT}")
file(LOCK "${DESTINATION_ROOT}.molga-text-runtime.lock"
     GUARD PROCESS TIMEOUT 60 RESULT_VARIABLE lock_result)
if(NOT lock_result STREQUAL "0")
  message(FATAL_ERROR "could not lock text runtime root: ${lock_result}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/TextArtifactTransaction.cmake")
set(journal "${DESTINATION_ROOT}/.molga-text-runtime-journal.json")
text_recover_artifact_journal(
  recovered "${journal}" "${DESTINATION_ROOT}")
if(NOT recovered)
  message(FATAL_ERROR "could not recover text runtime staging journal")
endif()
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef stage_id)
set(stage_root "${DESTINATION_ROOT}/.molga-stage-${stage_id}")

set(contract_destination "${DESTINATION_ROOT}/text_dependency_contract.json")
set(icu_destination "${DESTINATION_ROOT}/icudt78l.dat")

# ── Fast path: the destination pair already is the verified pair ─────────────
if(EXISTS "${contract_destination}" AND EXISTS "${icu_destination}")
    file(SHA256 "${contract_destination}" contract_destination_sha)
    file(SHA256 "${icu_destination}" icu_destination_sha)
    if(contract_destination_sha STREQUAL contract_source_sha AND
       icu_destination_sha STREQUAL expected_icu_sha)
        return()
    endif()
endif()

# ── Stage both files together, then publish them together ────────────────────
file(MAKE_DIRECTORY "${stage_root}")
file(COPY_FILE "${CONTRACT_SOURCE}" "${stage_root}/text_dependency_contract.json")
file(COPY_FILE "${ICU_DATA_SOURCE}" "${stage_root}/icudt78l.dat")
file(SHA256 "${stage_root}/text_dependency_contract.json" staged_contract_sha)
file(SHA256 "${stage_root}/icudt78l.dat" staged_icu_sha)
if(NOT staged_contract_sha STREQUAL contract_source_sha OR
   NOT staged_icu_sha STREQUAL expected_icu_sha)
    message(FATAL_ERROR "staged text runtime copy does not match its source")
endif()

text_publish_artifact_set(published
  "${journal}" 0
  "${DESTINATION_ROOT}"
  "${stage_root}/text_dependency_contract.json"
  "${DESTINATION_ROOT}/text_dependency_contract.json"
  "${stage_root}/icudt78l.dat"
  "${DESTINATION_ROOT}/icudt78l.dat")
if(NOT published)
    # The journal and its rollback siblings are deliberately left in place: a
    # later run finishes what this one started, and until then the build stays
    # red rather than shipping a half-published pair.
    message(FATAL_ERROR "could not publish the text runtime pair into ${DESTINATION_ROOT}")
endif()

file(SHA256 "${contract_destination}" published_contract_sha)
file(SHA256 "${icu_destination}" published_icu_sha)
if(NOT published_contract_sha STREQUAL contract_source_sha OR
   NOT published_icu_sha STREQUAL expected_icu_sha)
    message(FATAL_ERROR
        "published text runtime pair does not match its source in ${DESTINATION_ROOT}")
endif()

file(REMOVE_RECURSE "${stage_root}")
message(STATUS "staged verified text runtime pair into ${DESTINATION_ROOT}")
