# Build the logical `icuuc` archive: raw ICU common plus the pinned stubdata
# object that defines `icudt78_dat`.
#
# The stub symbol must live inside logical `icuuc` so that `libicudata.a` is
# never a third public consumer archive. The raw nested archives are inputs
# only and are never modified in place.

cmake_minimum_required(VERSION 3.27)

foreach(required RAW_ICUUC STUBDATA_SOURCE STUBDATA_ARCHIVE OUTPUT_ICUUC
                 CMAKE_AR_TOOL CMAKE_RANLIB_TOOL CMAKE_NM_TOOL ALLOWED_ROOTS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "MergeIcuStubdata requires ${required}")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/TextArchiveTools.cmake")

text_require_canonical_under(RAW_ICUUC       "${RAW_ICUUC}"       "${ALLOWED_ROOTS}")
text_require_canonical_under(STUBDATA_SOURCE "${STUBDATA_SOURCE}" "${ALLOWED_ROOTS}")
text_require_canonical_under(STUBDATA_ARCHIVE "${STUBDATA_ARCHIVE}" "${ALLOWED_ROOTS}")
text_require_canonical_under(OUTPUT_ICUUC    "${OUTPUT_ICUUC}"    "${ALLOWED_ROOTS}")

# Step 3a: enumerate the stub archive and isolate its single regular object.
text_archive_regular_members(stub_members stub_filtered
                             "${CMAKE_AR_TOOL}" "${STUBDATA_ARCHIVE}")
list(LENGTH stub_members stub_member_count)
if(NOT stub_member_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one regular member in ${STUBDATA_ARCHIVE}, found ${stub_member_count}: ${stub_members}")
endif()
list(GET stub_members 0 stub_object_name)
message(STATUS "icu stubdata filtered table members: ${stub_filtered}")
message(STATUS "icu stubdata object member: ${stub_object_name}")

get_filename_component(output_parent "${OUTPUT_ICUUC}" DIRECTORY)
file(MAKE_DIRECTORY "${output_parent}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef merge_id)
set(extract_root "${output_parent}/.icu-stub-extract-${merge_id}")
file(MAKE_DIRECTORY "${extract_root}")

# Extract that exact named member rather than blanket-running `ar -x`.
text_archive_extract_member("${CMAKE_AR_TOOL}" "${STUBDATA_ARCHIVE}"
                            "${stub_object_name}" "${extract_root}")
set(stub_object "${extract_root}/${stub_object_name}")
if(NOT EXISTS "${stub_object}")
    file(REMOVE_RECURSE "${extract_root}")
    message(FATAL_ERROR "extraction did not produce ${stub_object}")
endif()

# Step 3b: stage a copy of raw common and append the stub object after it.
text_archive_regular_members(raw_members raw_filtered
                             "${CMAKE_AR_TOOL}" "${RAW_ICUUC}")
if(stub_object_name IN_LIST raw_members)
    file(REMOVE_RECURSE "${extract_root}")
    message(FATAL_ERROR
        "stub object ${stub_object_name} collides with a raw icuuc member")
endif()

set(staged "${output_parent}/.libicuuc.a.molga-stage-${merge_id}")
file(COPY_FILE "${RAW_ICUUC}" "${staged}")
text_archive_append_object("${CMAKE_AR_TOOL}" "${CMAKE_RANLIB_TOOL}"
                           "${staged}" "${stub_object}" archiver_family)

# Step 3c: verify the composite before it is published.
file(SHA256 "${STUBDATA_SOURCE}" stub_source_sha)
file(SHA256 "${stub_object}" stub_object_sha)
file(SHA256 "${RAW_ICUUC}" raw_icuuc_sha)
file(SHA256 "${staged}" composite_sha)

text_archive_member_listing(listing_first "${CMAKE_AR_TOOL}" "${staged}")
text_archive_member_listing(listing_second "${CMAKE_AR_TOOL}" "${staged}")
if(NOT listing_first STREQUAL listing_second)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR "composite icuuc member order is not stable")
endif()

text_archive_defined_symbol_count(composite_defs "${CMAKE_NM_TOOL}" "${staged}"
                                  " [DRS] _?icudt78_dat$")
if(NOT composite_defs EQUAL 1)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR
        "composite icuuc must define icudt78_dat exactly once, found ${composite_defs}")
endif()
# The raw common archive's undefined reference is expected and recorded only.
text_archive_defined_symbol_count(raw_defs "${CMAKE_NM_TOOL}" "${RAW_ICUUC}"
                                  " [DRS] _?icudt78_dat$")
if(NOT raw_defs EQUAL 0)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR "raw icuuc unexpectedly defines icudt78_dat")
endif()

file(RENAME "${staged}" "${OUTPUT_ICUUC}" RESULT rename_result)
if(NOT rename_result EQUAL 0)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR "publishing ${OUTPUT_ICUUC} failed: ${rename_result}")
endif()
file(REMOVE_RECURSE "${extract_root}")

message(STATUS "icu composite archiverFamily=${archiver_family}")
message(STATUS "icu composite rawCommonSha256=${raw_icuuc_sha}")
message(STATUS "icu composite stubSourceSha256=${stub_source_sha}")
message(STATUS "icu composite stubObjectSha256=${stub_object_sha}")
message(STATUS "icu composite finalCompositeSha256=${composite_sha}")
