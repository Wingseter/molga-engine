# Verification barrier for the vendored text dependencies.
#
# MODE=PUBLISH            recompute all provenance and atomically publish the
#                         machine-local build lock and the portable contract.
# MODE=HARFBUZZ_READ_ONLY recompute only the HarfBuzz composite provenance into
#                         a caller-owned result file. It publishes nothing,
#                         mutates no archive, and exists so a test can prove the
#                         recorded provenance still matches the files on disk.
#
# The build lock is machine-local and may carry canonical absolute paths.
# The portable contract is built from an explicit allowlist and must never
# contain a checkout/build path or any composite-internal field name.

cmake_minimum_required(VERSION 3.27)

include("${CMAKE_CURRENT_LIST_DIR}/TextArchiveTools.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/TextArtifactTransaction.cmake")

if(NOT DEFINED MODE)
    message(FATAL_ERROR "VerifyTextDependencies requires MODE")
endif()

set(adapter_symbol " [TDS] _?hb_icu_get_unicode_funcs$")
set(stub_symbol " [DRS] _?icudt78_dat$")

# Recompute every HarfBuzz composite value from the files themselves.
function(molga_text_harfbuzz_composite_provenance out_prefix
         raw_core raw_adapter final_composite ar ranlib nm result_root)
    text_archive_regular_members(adapter_members adapter_filtered "${ar}" "${raw_adapter}")
    list(LENGTH adapter_members adapter_count)
    if(NOT adapter_count EQUAL 1)
        message(FATAL_ERROR "raw harfbuzz-icu must hold exactly one object")
    endif()
    list(GET adapter_members 0 adapter_object_name)

    set(extract_root "${result_root}/.adapter-extract")
    file(MAKE_DIRECTORY "${extract_root}")
    text_archive_extract_member("${ar}" "${raw_adapter}" "${adapter_object_name}" "${extract_root}")
    file(SHA256 "${extract_root}/${adapter_object_name}" adapter_object_sha)
    file(REMOVE_RECURSE "${extract_root}")

    file(SHA256 "${raw_core}" raw_core_sha)
    file(SHA256 "${raw_adapter}" raw_adapter_sha)
    file(SHA256 "${final_composite}" final_sha)

    text_archive_regular_members(composite_members composite_filtered "${ar}" "${final_composite}")
    string(REPLACE ";" "\n" member_text "${composite_members}")
    string(SHA256 member_digest "${member_text}\n")

    text_archive_defined_symbol_count(raw_core_defs "${nm}" "${raw_core}" "${adapter_symbol}")
    text_archive_defined_symbol_count(raw_adapter_defs "${nm}" "${raw_adapter}" "${adapter_symbol}")
    text_archive_defined_symbol_count(composite_defs "${nm}" "${final_composite}" "${adapter_symbol}")
    if(NOT raw_core_defs EQUAL 0 OR NOT raw_adapter_defs EQUAL 1 OR NOT composite_defs EQUAL 1)
        message(FATAL_ERROR
            "harfbuzz adapter symbol counts changed: core=${raw_core_defs} adapter=${raw_adapter_defs} composite=${composite_defs}")
    endif()

    text_archive_family(family "${ar}")
    if(family STREQUAL "apple")
        set(append_flags "qcs")
    else()
        set(append_flags "qcsD")
    endif()

    set(${out_prefix}_rawCoreSha256 "${raw_core_sha}" PARENT_SCOPE)
    set(${out_prefix}_rawAdapterSha256 "${raw_adapter_sha}" PARENT_SCOPE)
    set(${out_prefix}_adapterObjectSha256 "${adapter_object_sha}" PARENT_SCOPE)
    set(${out_prefix}_finalCompositeSha256 "${final_sha}" PARENT_SCOPE)
    set(${out_prefix}_filteredMemberDigest "${member_digest}" PARENT_SCOPE)
    set(${out_prefix}_archiverFamily "${family}" PARENT_SCOPE)
    set(${out_prefix}_arAppendFlags "${append_flags}" PARENT_SCOPE)
    set(${out_prefix}_rawCoreDefined ${raw_core_defs} PARENT_SCOPE)
    set(${out_prefix}_rawAdapterDefined ${raw_adapter_defs} PARENT_SCOPE)
    set(${out_prefix}_compositeDefined ${composite_defs} PARENT_SCOPE)
endfunction()

# ── Closed read-only entrypoint ───────────────────────────────────────────────
if(MODE STREQUAL "HARFBUZZ_READ_ONLY")
    foreach(required BUILD_LOCK SOURCE_ROOT BINARY_ROOT RESULT_ROOT RESULT_FILE)
        if(NOT DEFINED ${required})
            message(FATAL_ERROR "HARFBUZZ_READ_ONLY requires ${required}")
        endif()
    endforeach()
    foreach(forbidden PORTABLE_CONTRACT CONTRACT_INPUT INJECT_PUBLISH_FAILURE_AT
                      JOURNAL)
        if(DEFINED ${forbidden})
            message(FATAL_ERROR "HARFBUZZ_READ_ONLY rejects ${forbidden}")
        endif()
    endforeach()
    if(NOT IS_DIRECTORY "${RESULT_ROOT}")
        message(FATAL_ERROR "RESULT_ROOT must be an existing directory")
    endif()
    if(EXISTS "${RESULT_FILE}")
        message(FATAL_ERROR "RESULT_FILE must not already exist")
    endif()
    get_filename_component(result_parent "${RESULT_FILE}" DIRECTORY)
    file(REAL_PATH "${result_parent}" result_parent_canonical)
    file(REAL_PATH "${RESULT_ROOT}" result_root_canonical)
    if(NOT result_parent_canonical STREQUAL result_root_canonical)
        message(FATAL_ERROR "RESULT_FILE must be a direct child of RESULT_ROOT")
    endif()

    file(READ "${BUILD_LOCK}" lock_json)
    string(JSON raw_core_path GET "${lock_json}" harfbuzz icuComposite rawCorePath)
    string(JSON raw_adapter_path GET "${lock_json}" harfbuzz icuComposite rawAdapterPath)
    string(JSON final_path GET "${lock_json}" harfbuzz icuComposite finalCompositePath)
    string(JSON ar_path GET "${lock_json}" harfbuzz icuComposite arPath)
    string(JSON ranlib_path GET "${lock_json}" harfbuzz icuComposite ranlibPath)
    string(JSON nm_path GET "${lock_json}" harfbuzz icuComposite nmPath)

    set(nested_root "${BINARY_ROOT}/text-dependencies")
    foreach(candidate "${raw_core_path}" "${raw_adapter_path}" "${final_path}")
        text_require_canonical_under(ignored "${candidate}" "${nested_root}")
    endforeach()

    molga_text_harfbuzz_composite_provenance(hb
        "${raw_core_path}" "${raw_adapter_path}" "${final_path}"
        "${ar_path}" "${ranlib_path}" "${nm_path}" "${RESULT_ROOT}")

    file(WRITE "${RESULT_FILE}" "{
  \"schemaVersion\": 1,
  \"mode\": \"HARFBUZZ_READ_ONLY\",
  \"rawCoreSha256\": \"${hb_rawCoreSha256}\",
  \"rawAdapterSha256\": \"${hb_rawAdapterSha256}\",
  \"adapterObjectSha256\": \"${hb_adapterObjectSha256}\",
  \"finalCompositeSha256\": \"${hb_finalCompositeSha256}\",
  \"filteredMemberDigest\": \"${hb_filteredMemberDigest}\",
  \"archiverFamily\": \"${hb_archiverFamily}\",
  \"arPath\": \"${ar_path}\",
  \"ranlibPath\": \"${ranlib_path}\",
  \"nmPath\": \"${nm_path}\",
  \"arAppendFlags\": \"${hb_arAppendFlags}\",
  \"ranlibFlags\": \"-D\",
  \"zeroArDate\": \"1\",
  \"rawCoreDefinedAdapterSymbols\": ${hb_rawCoreDefined},
  \"rawIcuAdapterDefinedAdapterSymbols\": ${hb_rawAdapterDefined},
  \"compositeDefinedAdapterSymbols\": ${hb_compositeDefined}
}
")
    message(STATUS "harfbuzz read-only provenance result written")
    return()
endif()

# ── Publication mode ──────────────────────────────────────────────────────────
if(NOT MODE STREQUAL "PUBLISH")
    message(FATAL_ERROR "unknown VerifyTextDependencies MODE: ${MODE}")
endif()
foreach(required SOURCE_ROOT BINARY_ROOT DEPS_ROOT ICU_SOURCE ICU_BUILD
                 ICU_RAW_PREFIX HARFBUZZ_SOURCE HARFBUZZ_BUILD
                 HARFBUZZ_RAW_PREFIX ICU_COMPOSITE ICU_I18N HARFBUZZ_COMPOSITE
                 ICU_COMMIT HARFBUZZ_COMMIT CMAKE_AR_TOOL CMAKE_RANLIB_TOOL
                 CMAKE_NM_TOOL BUILD_LOCK PORTABLE_CONTRACT CONTRACT_INPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "PUBLISH requires ${required}")
    endif()
endforeach()

find_package(Git QUIET REQUIRED)

# Step 5f: the pinned submodule authority is rechecked here, after the nested
# builds ran, so a checkout that changed mid-build fails before publication.
function(molga_text_recheck_submodule relative_path expected_commit)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse "HEAD:${relative_path}"
        WORKING_DIRECTORY "${SOURCE_ROOT}"
        OUTPUT_VARIABLE gitlink OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE gitlink_result ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${SOURCE_ROOT}/${relative_path}"
        OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE head_result ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain=v1 --untracked-files=all
        WORKING_DIRECTORY "${SOURCE_ROOT}/${relative_path}"
        OUTPUT_VARIABLE status OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE status_result ERROR_QUIET)
    if(NOT gitlink_result EQUAL 0 OR NOT head_result EQUAL 0 OR NOT status_result EQUAL 0)
        message(FATAL_ERROR "cannot re-verify ${relative_path} authority")
    endif()
    if(NOT gitlink STREQUAL expected_commit OR NOT head STREQUAL expected_commit)
        message(FATAL_ERROR "${relative_path} drifted from ${expected_commit}")
    endif()
    if(NOT status STREQUAL "")
        message(FATAL_ERROR "${relative_path} became dirty during the build")
    endif()
endfunction()

molga_text_recheck_submodule("external/icu" "${ICU_COMMIT}")
molga_text_recheck_submodule("external/harfbuzz" "${HARFBUZZ_COMMIT}")

# Canonical containment: every consumed output lives under text-dependencies.
foreach(archive "${ICU_COMPOSITE}" "${ICU_I18N}" "${HARFBUZZ_COMPOSITE}")
    text_require_canonical_under(ignored "${archive}" "${DEPS_ROOT}")
endforeach()
file(REAL_PATH "${SOURCE_ROOT}/external/icu" icu_source_root)
file(REAL_PATH "${SOURCE_ROOT}/external/harfbuzz" harfbuzz_source_root)

# Final composite symbol probes, re-run rather than trusted.
text_archive_defined_symbol_count(icuuc_defs "${CMAKE_NM_TOOL}" "${ICU_COMPOSITE}" "${stub_symbol}")
if(NOT icuuc_defs EQUAL 1)
    message(FATAL_ERROR "logical icuuc must define icudt78_dat exactly once, found ${icuuc_defs}")
endif()

set(verify_scratch "${DEPS_ROOT}/.verify-scratch")
file(REMOVE_RECURSE "${verify_scratch}")
file(MAKE_DIRECTORY "${verify_scratch}")
molga_text_harfbuzz_composite_provenance(hb
    "${HARFBUZZ_BUILD}/libharfbuzz.a" "${HARFBUZZ_BUILD}/libharfbuzz-icu.a"
    "${HARFBUZZ_COMPOSITE}" "${CMAKE_AR_TOOL}" "${CMAKE_RANLIB_TOOL}"
    "${CMAKE_NM_TOOL}" "${verify_scratch}")

# ICU common composite provenance.
text_archive_regular_members(stub_members stub_filtered "${CMAKE_AR_TOOL}"
                             "${ICU_BUILD}/stubdata/libicudata.a")
list(GET stub_members 0 stub_object_name)
set(stub_extract "${verify_scratch}/stub")
file(MAKE_DIRECTORY "${stub_extract}")
text_archive_extract_member("${CMAKE_AR_TOOL}" "${ICU_BUILD}/stubdata/libicudata.a"
                            "${stub_object_name}" "${stub_extract}")
file(SHA256 "${stub_extract}/${stub_object_name}" icu_stub_object_sha)
file(SHA256 "${ICU_SOURCE}/stubdata/stubdata.cpp" icu_stub_source_sha)
file(SHA256 "${ICU_BUILD}/lib/libicuuc.a" icu_raw_common_sha)
file(SHA256 "${ICU_COMPOSITE}" icu_composite_sha)
file(SHA256 "${ICU_I18N}" icu_i18n_sha)
text_archive_regular_members(icu_members icu_filtered "${CMAKE_AR_TOOL}" "${ICU_COMPOSITE}")
string(REPLACE ";" "\n" icu_member_text "${icu_members}")
string(SHA256 icu_member_digest "${icu_member_text}\n")
file(REMOVE_RECURSE "${verify_scratch}")

file(SHA256 "${SOURCE_ROOT}/resources/text/icudt78l.dat" icu_data_sha)
file(READ "${CONTRACT_INPUT}" input_json)
string(JSON input_harfbuzz GET "${input_json}" harfbuzz)
string(JSON input_icu GET "${input_json}" icu)
string(JSON input_hb_commit GET "${input_json}" harfbuzz commit)
string(JSON input_icu_commit GET "${input_json}" icu commit)
string(JSON input_data_sha GET "${input_json}" icuData sha256)
if(NOT input_hb_commit STREQUAL HARFBUZZ_COMMIT OR NOT input_icu_commit STREQUAL ICU_COMMIT)
    message(FATAL_ERROR "the committed input contract disagrees with the pinned commits")
endif()
if(NOT input_data_sha STREQUAL icu_data_sha)
    message(FATAL_ERROR "packaged ICU data does not match the committed input contract")
endif()

# ── Stage the machine-local build lock ────────────────────────────────────────
set(generated_root "${BINARY_ROOT}/generated")
file(MAKE_DIRECTORY "${generated_root}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef publish_id)
set(staged_lock "${generated_root}/.build-lock.molga-stage-${publish_id}")
set(staged_contract "${generated_root}/.contract.molga-stage-${publish_id}")

file(WRITE "${staged_lock}" "{
  \"schema\": 1,
  \"harfbuzz\": {
    \"commit\": \"${HARFBUZZ_COMMIT}\",
    \"sourcePath\": \"${harfbuzz_source_root}\",
    \"libraries\": [\"harfbuzz\"],
    \"compositeSha256\": \"${hb_finalCompositeSha256}\",
    \"options\": {
      \"icu\": true,
      \"coretext\": false,
      \"cairo\": false,
      \"freetype\": false,
      \"graphite2\": false,
      \"glib\": false,
      \"gobject\": false,
      \"introspection\": false,
      \"utils\": false,
      \"subset\": false,
      \"raster\": false,
      \"vector\": false,
      \"gpu\": false,
      \"sharedLibs\": false
    },
    \"icuComposite\": {
      \"rawCorePath\": \"${HARFBUZZ_BUILD}/libharfbuzz.a\",
      \"rawAdapterPath\": \"${HARFBUZZ_BUILD}/libharfbuzz-icu.a\",
      \"finalCompositePath\": \"${HARFBUZZ_COMPOSITE}\",
      \"rawCoreSha256\": \"${hb_rawCoreSha256}\",
      \"rawAdapterSha256\": \"${hb_rawAdapterSha256}\",
      \"adapterObjectSha256\": \"${hb_adapterObjectSha256}\",
      \"finalCompositeSha256\": \"${hb_finalCompositeSha256}\",
      \"filteredMemberDigest\": \"${hb_filteredMemberDigest}\",
      \"archiverFamily\": \"${hb_archiverFamily}\",
      \"arPath\": \"${CMAKE_AR_TOOL}\",
      \"ranlibPath\": \"${CMAKE_RANLIB_TOOL}\",
      \"nmPath\": \"${CMAKE_NM_TOOL}\",
      \"arAppendFlags\": \"${hb_arAppendFlags}\",
      \"ranlibFlags\": \"-D\",
      \"zeroArDate\": \"1\",
      \"rawCoreDefinedAdapterSymbols\": ${hb_rawCoreDefined},
      \"rawIcuAdapterDefinedAdapterSymbols\": ${hb_rawAdapterDefined},
      \"compositeDefinedAdapterSymbols\": ${hb_compositeDefined}
    }
  },
  \"icu\": {
    \"commit\": \"${ICU_COMMIT}\",
    \"sourcePath\": \"${icu_source_root}\",
    \"libraries\": [\"icui18n\", \"icuuc\"],
    \"archiveSha256\": {
      \"icui18n\": \"${icu_i18n_sha}\",
      \"icuuc\": \"${icu_composite_sha}\"
    },
    \"commonComposite\": {
      \"rawCommonPath\": \"${ICU_BUILD}/lib/libicuuc.a\",
      \"finalCompositePath\": \"${ICU_COMPOSITE}\",
      \"rawCommonSha256\": \"${icu_raw_common_sha}\",
      \"stubSourceSha256\": \"${icu_stub_source_sha}\",
      \"stubObjectSha256\": \"${icu_stub_object_sha}\",
      \"finalCompositeSha256\": \"${icu_composite_sha}\",
      \"filteredMemberDigest\": \"${icu_member_digest}\",
      \"archiverFamily\": \"${hb_archiverFamily}\",
      \"arPath\": \"${CMAKE_AR_TOOL}\",
      \"ranlibPath\": \"${CMAKE_RANLIB_TOOL}\",
      \"nmPath\": \"${CMAKE_NM_TOOL}\",
      \"arAppendFlags\": \"${hb_arAppendFlags}\",
      \"ranlibFlags\": \"-D\",
      \"zeroArDate\": \"1\",
      \"compositeDefinedStubSymbols\": ${icuuc_defs}
    },
    \"includeRoot\": \"${ICU_RAW_PREFIX}/include\"
  },
  \"icuData\": {
    \"sha256\": \"${icu_data_sha}\"
  }
}
")

# ── Stage the portable contract from an explicit allowlist ────────────────────
# Built by adding exactly one generated key to each committed input object, so
# it can never inherit a composite-internal field by omission.
string(JSON portable_harfbuzz SET "${input_harfbuzz}" compositeSha256
       "\"${hb_finalCompositeSha256}\"")
string(JSON portable_icu SET "${input_icu}" archiveSha256
       "{\"icui18n\": \"${icu_i18n_sha}\", \"icuuc\": \"${icu_composite_sha}\"}")
set(portable_json "{}")
string(JSON portable_json SET "${portable_json}" schema "1")
string(JSON portable_json SET "${portable_json}" harfbuzz "${portable_harfbuzz}")
string(JSON portable_json SET "${portable_json}" icu "${portable_icu}")
file(WRITE "${staged_contract}" "${portable_json}\n")

# Refuse to publish a portable record that leaked a local path.
# Literal substring search, not regex: a checkout path may contain regex
# metacharacters, which would silently weaken or break the match.
foreach(forbidden "${SOURCE_ROOT}" "${BINARY_ROOT}" "harfbuzz-icu" "icuComposite"
                  "rawCore" "rawAdapter" "adapterObject" "commonComposite"
                  "rawCommon" "stubSource" "stubObject" "libicudata" "icudt78_dat")
    string(FIND "${portable_json}" "${forbidden}" forbidden_at)
    if(NOT forbidden_at EQUAL -1)
        message(FATAL_ERROR "portable contract leaked '${forbidden}'")
    endif()
endforeach()

# ── Publish the mutually consistent pair ──────────────────────────────────────
if(NOT DEFINED INJECT_PUBLISH_FAILURE_AT)
    set(INJECT_PUBLISH_FAILURE_AT 0)
endif()
text_publish_artifact_set(publish_ok "${generated_root}/.text-record-journal.json"
    "${INJECT_PUBLISH_FAILURE_AT}" "${generated_root}"
    "${staged_lock}" "${BUILD_LOCK}"
    "${staged_contract}" "${PORTABLE_CONTRACT}")
file(REMOVE "${staged_lock}" "${staged_contract}")
if(NOT publish_ok)
    if(INJECT_PUBLISH_FAILURE_AT EQUAL 0)
        message(FATAL_ERROR "publishing the text dependency records failed")
    endif()
    message(STATUS "text dependency record publication rolled back as injected")
    return()
endif()

message(STATUS "text dependency records published and verified")
