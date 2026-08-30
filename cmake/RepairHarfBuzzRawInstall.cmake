# Repair boundary for the nested HarfBuzz raw build and install.
#
# MODE=GENERATED_HEADER_PREBUILD
#     Runs as an ExternalProject step wedged between configure and build. It
#     owns exactly one declared output — build-tree src/hb-features.h — so a
#     file-level generator has a real producer for a header that is a configure
#     output rather than a build-command byproduct. It returns as soon as that
#     header and the nested cache matrix are right, because its own build step
#     follows immediately.
#
# MODE=RAW_INSTALL_WRAPPER
#     Runs as an always-checked target after the whole ExternalProject. Make
#     does not uniformly rerun an ExternalProject configure/build/install step
#     when only a byproduct is deleted, so this revalidates and repairs all 36
#     consumed outputs on every build.
#
# Every check answers to an authority that is not a copy of the thing being
# checked:
#     the 33 installed source headers -> the clean pinned submodule
#     build-tree hb-features.h        -> the SHA-256 pinned below
#     installed hb-features.h         -> that verified build-tree copy
#     the two installed archives      -> the build-tree archives
# Upstream's install also emits CMake export and pkg-config files. No Molga
# step reads, links through, stages, or packages them, so they are deliberately
# unconsumed incidental outputs and not members of this 36-file repair set.

cmake_minimum_required(VERSION 3.27)

include("${CMAKE_CURRENT_LIST_DIR}/TextArchiveTools.cmake")

# ── Argument contract ─────────────────────────────────────────────────────────
set(harfbuzz_modes GENERATED_HEADER_PREBUILD RAW_INSTALL_WRAPPER)
if(NOT DEFINED MODE OR NOT MODE IN_LIST harfbuzz_modes)
    message(FATAL_ERROR
        "RepairHarfBuzzRawInstall requires MODE to be one of: ${harfbuzz_modes}")
endif()

# Shared by both modes: the two containment roots, the three canonical paths,
# the generator, and the counts of the two numbered channels.
# HARFBUZZ_CONFIGURE_ARG_<n> carries the complete configure argument vector and
# HARFBUZZ_CACHE_ENTRY_<n> the typed Step 4 cache matrix, one entry per
# argument — see the unpacking below. No toolchain value is named here: the
# replay splices the vector verbatim instead of rebuilding it from parts, so
# there is no second description of the configure command to drift.
set(harfbuzz_shared_arguments
    HARFBUZZ_SOURCE_ROOT
    HARFBUZZ_DEPS_ROOT
    HARFBUZZ_SOURCE
    HARFBUZZ_BUILD
    HARFBUZZ_RAW_PREFIX
    HARFBUZZ_GENERATOR
    HARFBUZZ_CONFIGURE_ARG_COUNT
    HARFBUZZ_CACHE_ENTRY_COUNT)

# The installed allowlist describes an install that does not exist yet when the
# pre-build step runs, so handing it over there would only invite a check that
# cannot mean anything. Passing it is a caller error, not a no-op.
set(harfbuzz_wrapper_arguments HARFBUZZ_PUBLIC_HEADERS)

if(MODE STREQUAL "RAW_INSTALL_WRAPPER")
    set(harfbuzz_required
        ${harfbuzz_shared_arguments} ${harfbuzz_wrapper_arguments})
    set(harfbuzz_forbidden "")
else()
    set(harfbuzz_required ${harfbuzz_shared_arguments})
    set(harfbuzz_forbidden ${harfbuzz_wrapper_arguments})
endif()

foreach(required IN LISTS harfbuzz_required)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${MODE} requires ${required}")
    endif()
endforeach()
foreach(forbidden IN LISTS harfbuzz_forbidden)
    if(DEFINED ${forbidden})
        message(FATAL_ERROR "${MODE} rejects ${forbidden}")
    endif()
endforeach()

# Every argument this script accepts lives in the HARFBUZZ_ namespace, and
# CMake defines nothing there itself, so anything else in that namespace is a
# caller typo. Ignoring one silently is the worst outcome available: the check
# would then run against a default the caller believed it had replaced.
#
# The corollary is a rule for this file: every variable it defines for itself is
# lowercase snake_case. A local spelled HARFBUZZ_SOMETHING introduced above this
# sweep would be indistinguishable from a caller's typo and abort the build with
# a baffling "does not accept" naming a variable no caller ever passed.
get_cmake_property(harfbuzz_visible_variables VARIABLES)
foreach(variable IN LISTS harfbuzz_visible_variables)
    if(NOT "${variable}" MATCHES "^HARFBUZZ_")
        continue()
    endif()
    if("${variable}" IN_LIST harfbuzz_shared_arguments OR
       "${variable}" IN_LIST harfbuzz_wrapper_arguments OR
       "${variable}" MATCHES "^HARFBUZZ_(CONFIGURE_ARG|CACHE_ENTRY)_[0-9]+$")
        continue()
    endif()
    message(FATAL_ERROR "${MODE} does not accept ${variable}")
endforeach()

# ── Canonical paths ───────────────────────────────────────────────────────────
# Resolve the three roots once against the two roots the caller is allowed to
# work in. Containment is checked on canonical paths, never on the strings that
# were handed over, so a symlinked hop out of the tree cannot slip through.
text_require_canonical_under(harfbuzz_source
                             "${HARFBUZZ_SOURCE}" "${HARFBUZZ_SOURCE_ROOT}")
text_require_canonical_under(harfbuzz_build
                             "${HARFBUZZ_BUILD}" "${HARFBUZZ_DEPS_ROOT}")
text_require_canonical_under(harfbuzz_raw_prefix
                             "${HARFBUZZ_RAW_PREFIX}" "${HARFBUZZ_DEPS_ROOT}")

# ── Numbered argument channels ────────────────────────────────────────────────
# TextDependencies.cmake owns one definition of each list and sends it here as
# numbered arguments, because ExternalProject_Add_Step collapses a ";"-joined
# list argument down to its first element: a single list argument would arrive
# silently truncated to one entry.
#
# The transported count is checked against the length that actually materializes
# here, which closes the opposite hole. list(APPEND) splits a value containing
# ";" — a realistic CMAKE_OSX_ARCHITECTURES:STRING=arm64;x86_64, say — into
# extra elements that no per-entry check below would ever see, because those
# checks are driven by the index range rather than by the assembled list.
function(harfbuzz_unpack_channel out_var prefix count)
    if(NOT "${count}" MATCHES "^[0-9]+$" OR count EQUAL 0)
        message(FATAL_ERROR "${prefix}COUNT must be a positive integer: ${count}")
    endif()
    set(unpacked "")
    math(EXPR last_index "${count} - 1")
    foreach(index RANGE 0 ${last_index})
        if(NOT DEFINED ${prefix}${index})
            message(FATAL_ERROR "${MODE} requires ${prefix}${index}")
        endif()
        list(APPEND unpacked "${${prefix}${index}}")
    endforeach()
    list(LENGTH unpacked unpacked_count)
    if(NOT unpacked_count EQUAL count)
        message(FATAL_ERROR
            "${prefix}* unpacked to ${unpacked_count} entries, not ${count}: a "
            "value containing ';' cannot cross this boundary")
    endif()
    set(${out_var} "${unpacked}" PARENT_SCOPE)
endfunction()

# The complete configure argument vector, replayed verbatim on repair. Its
# length is deliberately not pinned: pinning it would mean editing this script
# every time a toolchain argument is added over there, which is the second
# description the vector exists to abolish.
harfbuzz_unpack_channel(harfbuzz_configure_args
    "HARFBUZZ_CONFIGURE_ARG_" "${HARFBUZZ_CONFIGURE_ARG_COUNT}")

# The cache matrix is pinned, because "complete" is a claim about it: Step 4
# names exactly these 20 entries, so a shorter one is a shorter assertion.
set(harfbuzz_expected_cache_entries 20)
if(NOT HARFBUZZ_CACHE_ENTRY_COUNT EQUAL harfbuzz_expected_cache_entries)
    message(FATAL_ERROR
        "expected ${harfbuzz_expected_cache_entries} typed cache entries, "
        "got ${HARFBUZZ_CACHE_ENTRY_COUNT}")
endif()
harfbuzz_unpack_channel(harfbuzz_cache_matrix
    "HARFBUZZ_CACHE_ENTRY_" "${HARFBUZZ_CACHE_ENTRY_COUNT}")

set(harfbuzz_cache_names "")
foreach(entry IN LISTS harfbuzz_cache_matrix)
    # Value-only entries would assert nothing about the type, and the type is
    # half of what Step 4 pins: HB_BUILD_GPU_DEMO is upstream STRING, and the
    # three ICU entries are the PATH/FILEPATH variables FindICU declares.
    if(NOT entry MATCHES "^[A-Za-z_][A-Za-z0-9_]*:[A-Z]+=")
        message(FATAL_ERROR "cache entry is not NAME:TYPE=VALUE: ${entry}")
    endif()
    string(REGEX MATCH "^[^:]+" entry_name "${entry}")
    if("${entry_name}" IN_LIST harfbuzz_cache_names)
        message(FATAL_ERROR "duplicate cache entry for ${entry_name}")
    endif()
    list(APPEND harfbuzz_cache_names "${entry_name}")
endforeach()

# ── Pinned bytes ──────────────────────────────────────────────────────────────
# hb-features.h is generated by configure, so unlike every other public header
# it has no source-tree authority. Pin its expected bytes independently:
# comparing the installed copy against the build-tree copy alone would let an
# identically wrong pair agree its way past this boundary.
set(harfbuzz_generated_header "${harfbuzz_build}/src/hb-features.h")
set(harfbuzz_generated_header_sha256
    "b9f5b0184edfab48fa3953f3b5fe8f72f72db0c08ebf6ccfc2541451c8bbc597")

set(harfbuzz_build_archives
    "${harfbuzz_build}/libharfbuzz.a"
    "${harfbuzz_build}/libharfbuzz-icu.a")

# TextDependencies.cmake holds the one allowlist and derives the install
# byproducts from it. Pinning the count and the digest of the LF-joined
# manifest here keeps that the only editable copy while still making this
# script impossible to hand a different set: a narrower or wider definition
# there fails loudly rather than leaving the boundary quietly repairing files
# the build graph no longer declares.
set(harfbuzz_expected_header_count 34)
set(harfbuzz_expected_header_manifest_sha256
    "530e0f16160280c161fc45b5b400f28b4b6867931a03e648d70e0a9753fb0c8d")

# ── Nested cache assertion ────────────────────────────────────────────────────
# Yields an empty string when every pinned entry appears verbatim in the nested
# cache, and otherwise a report naming each offender and what was found instead.
function(harfbuzz_cache_matrix_mismatch out_var)
    set(cache_file "${harfbuzz_build}/CMakeCache.txt")
    if(NOT EXISTS "${cache_file}")
        set(${out_var} "no nested cache at ${cache_file}" PARENT_SCOPE)
        return()
    endif()
    file(STRINGS "${cache_file}" cache_lines)
    set(mismatch "")
    foreach(entry IN LISTS harfbuzz_cache_matrix)
        if("${entry}" IN_LIST cache_lines)
            continue()
        endif()
        string(REGEX MATCH "^[^:]+" entry_name "${entry}")
        set(actual "absent")
        foreach(line IN LISTS cache_lines)
            if(line MATCHES "^${entry_name}:")
                set(actual "${line}")
                break()
            endif()
        endforeach()
        string(APPEND mismatch "  expected ${entry}, cache has ${actual}\n")
    endforeach()
    set(${out_var} "${mismatch}" PARENT_SCOPE)
endfunction()

# Yields an empty string when the build-tree generated header matches its
# pinned bytes, and otherwise the reason it does not.
function(harfbuzz_generated_header_problem out_var)
    if(NOT EXISTS "${harfbuzz_generated_header}")
        set(${out_var} "${harfbuzz_generated_header} is missing" PARENT_SCOPE)
        return()
    endif()
    file(SHA256 "${harfbuzz_generated_header}" actual_sha)
    if(NOT actual_sha STREQUAL harfbuzz_generated_header_sha256)
        set(${out_var}
            "build-tree hb-features.h is ${actual_sha}, expected ${harfbuzz_generated_header_sha256}"
            PARENT_SCOPE)
        return()
    endif()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# ── Repair primitives ─────────────────────────────────────────────────────────
# The exact pinned Step 4 configure command. The argument vector is spliced in
# verbatim rather than rebuilt from named parts, so this is the same command the
# ExternalProject configures with by construction rather than by review: an
# argument added over there arrives here without a second edit, and no toolchain
# entry can drift, which matters because the cache assertion covers only the 20
# pinned matrix lines and would not notice.
#
# ZERO_AR_DATE=1 wraps it because upstream runs ar and ranlib itself;
# deterministic flags on the later merge alone would not make these archives
# reproducible. Only -G/-S/-B are added here: ExternalProject appends those
# itself and so does not carry them in CMAKE_ARGS.
function(harfbuzz_rerun_configure reason)
    message(STATUS "reconfiguring the nested HarfBuzz build: ${reason}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${CMAKE_COMMAND}" ${harfbuzz_configure_args}
                "-G${HARFBUZZ_GENERATOR}"
                -S "${harfbuzz_source}"
                -B "${harfbuzz_build}"
        RESULT_VARIABLE configure_result)
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR "reconfiguring the nested HarfBuzz build failed")
    endif()
endfunction()

# Naming the two archive targets is a narrowing of the ExternalProject's own
# `cmake --build <BINARY_DIR>`, and it is equivalent only because every
# HB_BUILD_* entry in the pinned matrix is OFF: at this configuration `all`
# holds exactly harfbuzz and harfbuzz-icu. Turning on HB_BUILD_SUBSET or
# HB_BUILD_UTILS would add targets this repair would then quietly skip, so that
# matrix change has to come here too.
function(harfbuzz_rerun_build reason)
    message(STATUS "rebuilding the nested HarfBuzz archives: ${reason}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${CMAKE_COMMAND}" --build "${harfbuzz_build}"
                --target harfbuzz harfbuzz-icu
        RESULT_VARIABLE build_result)
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "rebuilding the nested HarfBuzz archives failed")
    endif()
endfunction()

function(harfbuzz_rerun_install reason)
    message(STATUS "repairing the nested HarfBuzz install: ${reason}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${CMAKE_COMMAND}" --install "${harfbuzz_build}"
        RESULT_VARIABLE install_result)
    if(NOT install_result EQUAL 0)
        message(FATAL_ERROR "reinstalling the nested HarfBuzz build failed")
    endif()
endfunction()

# ── Step 1: a correct configuration ───────────────────────────────────────────
# Both modes start here. A wrong cache and a wrong generated header are the same
# defect seen from two sides, and configure is the only thing that fixes either.
harfbuzz_cache_matrix_mismatch(cache_mismatch)
harfbuzz_generated_header_problem(header_problem)
set(harfbuzz_reconfigured FALSE)
if(NOT cache_mismatch STREQUAL "" OR NOT header_problem STREQUAL "")
    set(configure_reason "${header_problem}")
    if(NOT cache_mismatch STREQUAL "")
        set(configure_reason "cache matrix mismatch\n${cache_mismatch}${header_problem}")
    endif()
    harfbuzz_rerun_configure("${configure_reason}")
    harfbuzz_cache_matrix_mismatch(cache_mismatch)
    if(NOT cache_mismatch STREQUAL "")
        message(FATAL_ERROR
            "the nested HarfBuzz cache still mismatches after reconfigure:\n${cache_mismatch}")
    endif()
    harfbuzz_generated_header_problem(header_problem)
    if(NOT header_problem STREQUAL "")
        message(FATAL_ERROR
            "the generated HarfBuzz feature header is still wrong after reconfigure: ${header_problem}")
    endif()
    set(harfbuzz_reconfigured TRUE)
endif()

if(MODE STREQUAL "GENERATED_HEADER_PREBUILD")
    # The declared build step runs next and is what turns this header into
    # objects, so there is nothing further to own here.
    message(STATUS
        "nested HarfBuzz cache matrix and generated hb-features.h verified")
    return()
endif()

# ── Step 2: the build-tree archives ───────────────────────────────────────────
# A reconfigure can invalidate objects that were compiled against the previous
# configuration, so the two nested targets run before either archive is trusted.
if(harfbuzz_reconfigured)
    harfbuzz_rerun_build("the nested configuration was repaired")
endif()

set(missing_build "")
foreach(archive IN LISTS harfbuzz_build_archives)
    if(NOT EXISTS "${archive}")
        set(missing_build "${archive}")
        break()
    endif()
endforeach()
if(NOT missing_build STREQUAL "")
    harfbuzz_rerun_build("${missing_build} is missing")
    foreach(archive IN LISTS harfbuzz_build_archives)
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR
                "the nested HarfBuzz archive is still missing after repair: ${archive}")
        endif()
    endforeach()
endif()

# ── Step 3: the pinned installed allowlist ────────────────────────────────────
function(harfbuzz_require_pinned_allowlist)
    set(seen "")
    set(manifest "")
    foreach(header IN LISTS HARFBUZZ_PUBLIC_HEADERS)
        if("${header}" IN_LIST seen)
            message(FATAL_ERROR
                "duplicate entry in the HarfBuzz public header allowlist: ${header}")
        endif()
        list(APPEND seen "${header}")
        string(APPEND manifest "${header}\n")
    endforeach()
    # A submodule bump is the likeliest way to get here, so both failures print
    # the manifest they were handed. A digest on its own says only that
    # something moved; the listing is what says which header did.
    list(LENGTH seen header_count)
    if(NOT header_count EQUAL harfbuzz_expected_header_count)
        message(FATAL_ERROR
            "expected ${harfbuzz_expected_header_count} pinned HarfBuzz public "
            "headers, found ${header_count}:\n${manifest}")
    endif()
    string(SHA256 manifest_sha "${manifest}")
    if(NOT manifest_sha STREQUAL harfbuzz_expected_header_manifest_sha256)
        message(FATAL_ERROR
            "the HarfBuzz public header manifest is ${manifest_sha}, expected "
            "${harfbuzz_expected_header_manifest_sha256}; received:\n${manifest}")
    endif()
endfunction()

harfbuzz_require_pinned_allowlist()

# ── Step 4: the 36 consumed installed outputs ─────────────────────────────────
# The authority for a header is never another installed copy of it, and the
# authority for an archive is never the installed archive. Yields TRUE with an
# empty reason, or FALSE naming the first output that has to be repaired.
function(harfbuzz_installed_set_matches out_var out_reason)
    foreach(archive_name libharfbuzz libharfbuzz-icu)
        set(installed "${harfbuzz_raw_prefix}/lib/${archive_name}.a")
        set(authority "${harfbuzz_build}/${archive_name}.a")
        if(NOT EXISTS "${installed}")
            set(${out_var} FALSE PARENT_SCOPE)
            set(${out_reason} "installed ${installed} is missing" PARENT_SCOPE)
            return()
        endif()
        file(SHA256 "${installed}" installed_sha)
        file(SHA256 "${authority}" authority_sha)
        if(NOT installed_sha STREQUAL authority_sha)
            set(${out_var} FALSE PARENT_SCOPE)
            set(${out_reason}
                "installed ${archive_name}.a does not match the build tree"
                PARENT_SCOPE)
            return()
        endif()
    endforeach()
    foreach(header IN LISTS HARFBUZZ_PUBLIC_HEADERS)
        set(installed "${harfbuzz_raw_prefix}/include/harfbuzz/${header}")
        if(header STREQUAL "hb-features.h")
            set(authority "${harfbuzz_generated_header}")
        else()
            set(authority "${harfbuzz_source}/src/${header}")
            text_require_canonical_under(ignored "${authority}" "${harfbuzz_source}")
        endif()
        if(NOT EXISTS "${authority}")
            message(FATAL_ERROR "missing HarfBuzz header authority: ${authority}")
        endif()
        if(NOT EXISTS "${installed}")
            set(${out_var} FALSE PARENT_SCOPE)
            set(${out_reason} "installed ${installed} is missing" PARENT_SCOPE)
            return()
        endif()
        file(SHA256 "${installed}" installed_sha)
        file(SHA256 "${authority}" authority_sha)
        if(NOT installed_sha STREQUAL authority_sha)
            set(${out_var} FALSE PARENT_SCOPE)
            set(${out_reason}
                "installed ${header} does not match ${authority}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()

# `cmake --install` runs file(INSTALL), which decides whether to copy by
# comparing second-resolution mtimes and never looks at the bytes. A wrong
# installed copy written within the same second as its authority — exactly what
# happens when a repair regenerates the authority a moment after the bad copy
# appeared — therefore survives its own reinstall, and the boundary would fail
# closed forever instead of converging. Discarding the consumed set first makes
# the install materialize all 36 outputs again rather than skip them.
#
# Every path below is the canonical, contained raw prefix joined with one name
# from the allowlist the manifest digest above already pins, so none of them can
# name a file outside the nested install; file(REMOVE) also unlinks a symlink
# rather than following it.
function(harfbuzz_discard_installed_set)
    set(doomed
        "${harfbuzz_raw_prefix}/lib/libharfbuzz.a"
        "${harfbuzz_raw_prefix}/lib/libharfbuzz-icu.a")
    foreach(header IN LISTS HARFBUZZ_PUBLIC_HEADERS)
        list(APPEND doomed "${harfbuzz_raw_prefix}/include/harfbuzz/${header}")
    endforeach()
    file(REMOVE ${doomed})
endfunction()

harfbuzz_installed_set_matches(installed_ok installed_reason)
if(NOT installed_ok)
    harfbuzz_discard_installed_set()
    harfbuzz_rerun_install("${installed_reason}")
    harfbuzz_installed_set_matches(installed_ok installed_reason)
    if(NOT installed_ok)
        message(FATAL_ERROR
            "the nested HarfBuzz install does not match its authorities after "
            "repair: ${installed_reason}")
    endif()
endif()

# ── Step 5: the installed directory holds exactly the allowlist ───────────────
# Bytes matching is not enough. An extra installed header would be a public
# surface nothing declares, and a symlinked one would hash-match its authority
# while pointing consumers at bytes outside the nested prefix. Neither is
# repairable by reinstalling, so both fail here rather than silently widening
# the boundary.
function(harfbuzz_require_exact_installed_headers)
    set(include_dir "${harfbuzz_raw_prefix}/include/harfbuzz")
    if(NOT IS_DIRECTORY "${include_dir}")
        message(FATAL_ERROR
            "the installed HarfBuzz include directory is missing: ${include_dir}")
    endif()
    # LIST_DIRECTORIES true so a stray subdirectory counts as an extra entry
    # instead of being invisible to this check, and ".*" alongside "*" so a
    # dot-prefixed entry does too. Whether "*" alone matches a leading dot is a
    # KWSys detail that has varied across the CMake versions this project
    # supports, and "the directory holds exactly these 34" is not a claim that
    # may quietly depend on it.
    file(GLOB installed_entries LIST_DIRECTORIES true
         "${include_dir}/*" "${include_dir}/.*")
    set(installed_names "")
    foreach(entry IN LISTS installed_entries)
        get_filename_component(entry_name "${entry}" NAME)
        # The directory's own two links are not entries in it.
        if(entry_name STREQUAL "." OR entry_name STREQUAL "..")
            continue()
        endif()
        if(NOT "${entry_name}" IN_LIST HARFBUZZ_PUBLIC_HEADERS)
            message(FATAL_ERROR
                "extra entry in the installed HarfBuzz include directory: ${entry}")
        endif()
        text_require_canonical_under(ignored "${entry}" "${harfbuzz_raw_prefix}")
        list(APPEND installed_names "${entry_name}")
    endforeach()
    foreach(header IN LISTS HARFBUZZ_PUBLIC_HEADERS)
        if(NOT "${header}" IN_LIST installed_names)
            message(FATAL_ERROR
                "missing installed HarfBuzz public header: ${header}")
        endif()
    endforeach()
endfunction()

harfbuzz_require_exact_installed_headers()

foreach(archive_name libharfbuzz libharfbuzz-icu)
    text_require_canonical_under(ignored
        "${harfbuzz_raw_prefix}/lib/${archive_name}.a" "${harfbuzz_raw_prefix}")
endforeach()

list(LENGTH HARFBUZZ_PUBLIC_HEADERS harfbuzz_header_count)
math(EXPR harfbuzz_consumed_count "${harfbuzz_header_count} + 2")
message(STATUS
    "nested HarfBuzz raw install verified: ${harfbuzz_consumed_count} consumed "
    "outputs (2 archives and ${harfbuzz_header_count} headers)")
