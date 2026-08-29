# Build the single logical `harfbuzz` archive from upstream's deliberate split.
#
# At the pinned commit, HB_HAVE_ICU=ON produces core `libharfbuzz.a` plus a
# separate `libharfbuzz-icu.a` holding the ICU adapter. Consumers must see one
# logical archive, so the adapter object is appended into a composite here.
# The upstream raw archives are inputs only and are never modified in place.

cmake_minimum_required(VERSION 3.27)

foreach(required RAW_CORE RAW_ADAPTER OUTPUT_HARFBUZZ
                 CMAKE_AR_TOOL CMAKE_RANLIB_TOOL CMAKE_NM_TOOL ALLOWED_ROOTS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "MergeHarfBuzzIcu requires ${required}")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/TextArchiveTools.cmake")

text_require_canonical_under(RAW_CORE        "${RAW_CORE}"        "${ALLOWED_ROOTS}")
text_require_canonical_under(RAW_ADAPTER     "${RAW_ADAPTER}"     "${ALLOWED_ROOTS}")
text_require_canonical_under(OUTPUT_HARFBUZZ "${OUTPUT_HARFBUZZ}" "${ALLOWED_ROOTS}")

set(adapter_symbol " [TDS] _?hb_icu_get_unicode_funcs$")

# Step 4b: the split must match the pinned upstream layout exactly.
text_archive_defined_symbol_count(raw_core_defs "${CMAKE_NM_TOOL}"
                                  "${RAW_CORE}" "${adapter_symbol}")
if(NOT raw_core_defs EQUAL 0)
    message(FATAL_ERROR
        "raw harfbuzz core unexpectedly defines the ICU adapter (${raw_core_defs})")
endif()
text_archive_defined_symbol_count(raw_adapter_defs "${CMAKE_NM_TOOL}"
                                  "${RAW_ADAPTER}" "${adapter_symbol}")
if(NOT raw_adapter_defs EQUAL 1)
    message(FATAL_ERROR
        "raw harfbuzz-icu must define the ICU adapter exactly once, found ${raw_adapter_defs}")
endif()

text_archive_regular_members(adapter_members adapter_filtered
                             "${CMAKE_AR_TOOL}" "${RAW_ADAPTER}")
list(LENGTH adapter_members adapter_member_count)
if(NOT adapter_member_count EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one regular member in ${RAW_ADAPTER}, found ${adapter_member_count}: ${adapter_members}")
endif()
list(GET adapter_members 0 adapter_object_name)
message(STATUS "harfbuzz adapter filtered table members: ${adapter_filtered}")
message(STATUS "harfbuzz adapter object member: ${adapter_object_name}")

text_archive_regular_members(core_members core_filtered
                             "${CMAKE_AR_TOOL}" "${RAW_CORE}")
if(adapter_object_name IN_LIST core_members)
    message(FATAL_ERROR
        "adapter object ${adapter_object_name} collides with a raw core member")
endif()

get_filename_component(output_parent "${OUTPUT_HARFBUZZ}" DIRECTORY)
file(MAKE_DIRECTORY "${output_parent}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef merge_id)
set(extract_root "${output_parent}/.hb-adapter-extract-${merge_id}")
file(MAKE_DIRECTORY "${extract_root}")

# Step 4d: extract that one exact member; never blanket-run `ar -x`.
text_archive_extract_member("${CMAKE_AR_TOOL}" "${RAW_ADAPTER}"
                            "${adapter_object_name}" "${extract_root}")
set(adapter_object "${extract_root}/${adapter_object_name}")
if(NOT EXISTS "${adapter_object}")
    file(REMOVE_RECURSE "${extract_root}")
    message(FATAL_ERROR "extraction did not produce ${adapter_object}")
endif()
if(IS_SYMLINK "${adapter_object}")
    file(REMOVE_RECURSE "${extract_root}")
    message(FATAL_ERROR "extracted adapter object is a symlink")
endif()

set(staged "${output_parent}/.libharfbuzz.a.molga-stage-${merge_id}")
file(COPY_FILE "${RAW_CORE}" "${staged}")
text_archive_append_object("${CMAKE_AR_TOOL}" "${CMAKE_RANLIB_TOOL}"
                           "${staged}" "${adapter_object}" archiver_family)

# Step 4e: verify the composite before publication.
file(SHA256 "${RAW_CORE}" raw_core_sha)
file(SHA256 "${RAW_ADAPTER}" raw_adapter_sha)
file(SHA256 "${adapter_object}" adapter_object_sha)
file(SHA256 "${staged}" composite_sha)

text_archive_member_listing(listing_first "${CMAKE_AR_TOOL}" "${staged}")
text_archive_member_listing(listing_second "${CMAKE_AR_TOOL}" "${staged}")
if(NOT listing_first STREQUAL listing_second)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR "composite harfbuzz member order is not stable")
endif()

text_archive_regular_members(composite_members composite_filtered
                             "${CMAKE_AR_TOOL}" "${staged}")
string(REPLACE ";" "\n" filtered_member_text "${composite_members}")
string(SHA256 filtered_member_digest "${filtered_member_text}\n")

text_archive_defined_symbol_count(composite_defs "${CMAKE_NM_TOOL}"
                                  "${staged}" "${adapter_symbol}")
if(NOT composite_defs EQUAL 1)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR
        "composite harfbuzz must define the ICU adapter exactly once, found ${composite_defs}")
endif()

file(RENAME "${staged}" "${OUTPUT_HARFBUZZ}" RESULT rename_result)
if(NOT rename_result EQUAL 0)
    file(REMOVE_RECURSE "${extract_root}")
    file(REMOVE "${staged}")
    message(FATAL_ERROR "publishing ${OUTPUT_HARFBUZZ} failed: ${rename_result}")
endif()
file(REMOVE_RECURSE "${extract_root}")

message(STATUS "harfbuzz composite archiverFamily=${archiver_family}")
message(STATUS "harfbuzz composite rawCoreSha256=${raw_core_sha}")
message(STATUS "harfbuzz composite rawAdapterSha256=${raw_adapter_sha}")
message(STATUS "harfbuzz composite adapterObjectSha256=${adapter_object_sha}")
message(STATUS "harfbuzz composite filteredMemberDigest=${filtered_member_digest}")
message(STATUS "harfbuzz composite finalCompositeSha256=${composite_sha}")
