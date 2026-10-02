# Vendored static text dependencies: pinned ICU4C and HarfBuzz.
#
# Nothing here resolves Homebrew, a system installation, or network
# FetchContent. Every archive is built from the pinned submodules into
# ${CMAKE_BINARY_DIR}/text-dependencies and consumed only from there.
#
# Public surface: molga_text_icuuc, molga_text_icui18n, molga_text_harfbuzz,
# molga_text_rasterizer, and the molga_attach_text_* helpers.

include_guard(GLOBAL)
include(ExternalProject)

set(MOLGA_TEXT_HARFBUZZ_COMMIT "ab5ecbb83985034a76214ac0b2b833dcd590d774")
set(MOLGA_TEXT_ICU_COMMIT "21d1eb0f306e1141c10931e914dfc038c06121da")

set(molga_text_deps_root "${CMAKE_BINARY_DIR}/text-dependencies")
set(molga_text_icu_source "${CMAKE_SOURCE_DIR}/external/icu/icu4c/source")
set(molga_text_icu_build "${molga_text_deps_root}/icu-build")
set(molga_text_icu_raw "${molga_text_deps_root}/icu-raw")
set(molga_text_icu_final "${molga_text_deps_root}/icu")
set(molga_text_hb_source "${CMAKE_SOURCE_DIR}/external/harfbuzz")
set(molga_text_hb_build "${molga_text_deps_root}/harfbuzz-build")
set(molga_text_hb_raw "${molga_text_deps_root}/harfbuzz-raw")
set(molga_text_hb_final "${molga_text_deps_root}/harfbuzz")

set(MOLGA_TEXT_DEPENDENCY_BUILD_LOCK
    "${CMAKE_BINARY_DIR}/generated/text_dependency_build_lock.json")
set(MOLGA_TEXT_DEPENDENCY_CONTRACT
    "${CMAKE_BINARY_DIR}/generated/text_dependency_contract.json")

set(molga_text_icu_composite_archive "${molga_text_icu_final}/lib/libicuuc.a")
set(molga_text_icu_i18n_archive "${molga_text_icu_final}/lib/libicui18n.a")
set(molga_text_hb_composite_archive "${molga_text_hb_final}/lib/libharfbuzz.a")

# ── Pinned submodule authority ────────────────────────────────────────────────
# The superproject gitlink, the submodule HEAD, and the approved commit must all
# agree, and the submodule must be clean. A dirty checkout under a pinned HEAD
# would otherwise record different bytes than the recorded provenance claims.
function(molga_text_require_pinned_submodule relative_path expected_commit)
    find_package(Git QUIET REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse "HEAD:${relative_path}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE gitlink_oid OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE gitlink_result ERROR_QUIET)
    if(NOT gitlink_result EQUAL 0)
        message(FATAL_ERROR "cannot read the ${relative_path} gitlink")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${relative_path}"
        OUTPUT_VARIABLE submodule_head OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE head_result ERROR_QUIET)
    if(NOT head_result EQUAL 0)
        message(FATAL_ERROR "cannot read ${relative_path} HEAD; is the submodule initialized?")
    endif()
    if(NOT gitlink_oid STREQUAL expected_commit OR
       NOT submodule_head STREQUAL expected_commit)
        message(FATAL_ERROR
            "${relative_path} is not at the approved commit ${expected_commit} "
            "(gitlink ${gitlink_oid}, HEAD ${submodule_head})")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain=v1 --untracked-files=all
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/${relative_path}"
        OUTPUT_VARIABLE submodule_status OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE status_result ERROR_QUIET)
    if(NOT status_result EQUAL 0)
        message(FATAL_ERROR "cannot read ${relative_path} status")
    endif()
    if(NOT submodule_status STREQUAL "")
        message(FATAL_ERROR
            "${relative_path} has tracked or untracked changes under a pinned HEAD:\n${submodule_status}")
    endif()
endfunction()

molga_text_require_pinned_submodule("external/icu" "${MOLGA_TEXT_ICU_COMMIT}")
molga_text_require_pinned_submodule("external/harfbuzz" "${MOLGA_TEXT_HARFBUZZ_COMMIT}")

# ICU is autoconf-driven, so it needs a real make regardless of our generator.
find_program(MOLGA_TEXT_MAKE_PROGRAM NAMES gnumake gmake make REQUIRED)
set(molga_text_zero_ar ${CMAKE_COMMAND} -E env ZERO_AR_DATE=1)
set(molga_text_icu_env ${CMAKE_COMMAND} -E env ZERO_AR_DATE=1 PKG_CONFIG=false
                       ac_cv_prog_PYTHON=)

# ── Pinned ICU public header manifest ─────────────────────────────────────────
# The nested install flattens common/unicode and i18n/unicode into one
# include/unicode directory, and byproducts are file-level, so every installed
# header has to be named to the build graph individually. The list is derived
# from the clean pinned source rather than from the not-yet-produced install,
# and the pinned count and manifest digest make a submodule bump fail here
# instead of silently shrinking the declared byproduct set.
set(MOLGA_TEXT_ICU_HEADER_COUNT 203)
set(MOLGA_TEXT_ICU_HEADER_MANIFEST_SHA256
    "fe6c48d6b56a735a1434c0aabd47c4dbbb2a1f71df26c7fccc98cf74dec8947b")

# Yields every pinned public header's installed path in bytewise manifest
# order. The source root is canonicalized alongside each entry, so the
# containment check rejects exactly what it says: anything whose canonical path
# leaves the canonical source root.
function(molga_text_collect_icu_installed_headers out_var)
    file(REAL_PATH "${molga_text_icu_source}" icu_source_canonical)
    set(header_relatives "")
    set(installed_headers "")
    set(installed_basenames "")
    foreach(component common i18n)
        # For a clean pinned git checkout, LIST_DIRECTORIES false plus the
        # symlink rejection below is what "regular files only" reduces to.
        file(GLOB component_headers
             LIST_DIRECTORIES false
             "${molga_text_icu_source}/${component}/unicode/*.h")
        set(component_basenames "")
        foreach(header IN LISTS component_headers)
            if(IS_SYMLINK "${header}")
                message(FATAL_ERROR
                    "pinned ICU public header is a symlink: ${header}")
            endif()
            file(REAL_PATH "${header}" header_canonical)
            cmake_path(IS_PREFIX icu_source_canonical "${header_canonical}"
                       NORMALIZE header_contained)
            if(NOT header_contained)
                message(FATAL_ERROR
                    "pinned ICU public header escapes ${icu_source_canonical}: "
                    "${header_canonical}")
            endif()
            get_filename_component(header_name "${header}" NAME)
            # Both components install into one flat directory, so two equal
            # basenames would silently collapse to a single installed file.
            if(header_name IN_LIST installed_basenames)
                message(FATAL_ERROR
                    "pinned ICU public headers collide on one installed name: "
                    "${header_name} (${header})")
            endif()
            list(APPEND installed_basenames "${header_name}")
            list(APPEND component_basenames "${header_name}")
        endforeach()

        # Every name in a component shares the "<component>/unicode/" prefix
        # and "common" sorts before "i18n", so sorting each component's ASCII
        # basenames and visiting common first is the bytewise order of the
        # composed names — which the pinned digest below then confirms.
        # Composing both lists here keeps each installed path paired with the
        # manifest entry it came from instead of recovering it afterwards.
        # The component list itself must therefore stay bytewise sorted: the
        # wrapper in RepairIcuRawInstall.cmake sorts its manifest globally, so
        # a component inserted out of order would reject a digest re-pinned
        # from here while blaming the wrong file.
        list(SORT component_basenames)
        foreach(header_name IN LISTS component_basenames)
            list(APPEND header_relatives "${component}/unicode/${header_name}")
            list(APPEND installed_headers
                 "${molga_text_icu_raw}/include/unicode/${header_name}")
        endforeach()
    endforeach()

    list(LENGTH header_relatives header_count)
    if(NOT header_count EQUAL MOLGA_TEXT_ICU_HEADER_COUNT)
        string(REPLACE ";" "\n" header_listing "${header_relatives}")
        message(FATAL_ERROR
            "expected ${MOLGA_TEXT_ICU_HEADER_COUNT} pinned ICU public headers, "
            "found ${header_count}:\n${header_listing}")
    endif()

    set(header_manifest "")
    foreach(relative IN LISTS header_relatives)
        string(APPEND header_manifest "${relative}\n")
    endforeach()
    string(SHA256 header_manifest_sha "${header_manifest}")
    if(NOT header_manifest_sha STREQUAL MOLGA_TEXT_ICU_HEADER_MANIFEST_SHA256)
        message(FATAL_ERROR
            "pinned ICU public header manifest is ${header_manifest_sha}, "
            "expected ${MOLGA_TEXT_ICU_HEADER_MANIFEST_SHA256}")
    endif()

    set(${out_var} "${installed_headers}" PARENT_SCOPE)
endfunction()

molga_text_collect_icu_installed_headers(molga_text_icu_installed_headers)

# ── Nested static ICU ─────────────────────────────────────────────────────────
# Only common, i18n, and the pinned stubdata are built. ICU's full data archive
# is never built: external icudt78l.dat stays the only runtime data payload.
ExternalProject_Add(molga_text_icu_external
    SOURCE_DIR "${molga_text_icu_source}"
    BINARY_DIR "${molga_text_icu_build}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND ${molga_text_icu_env} "${molga_text_icu_source}/configure"
        --disable-shared --enable-static --disable-tools --disable-tests
        --disable-samples --disable-extras --disable-icuio --disable-layoutex
        "--prefix=${molga_text_icu_raw}"
    BUILD_COMMAND ${molga_text_zero_ar} "${MOLGA_TEXT_MAKE_PROGRAM}" lib bin
          COMMAND ${molga_text_zero_ar} "${MOLGA_TEXT_MAKE_PROGRAM}" -C stubdata
          COMMAND ${molga_text_zero_ar} "${MOLGA_TEXT_MAKE_PROGRAM}" -C common
          COMMAND ${molga_text_zero_ar} "${MOLGA_TEXT_MAKE_PROGRAM}" -C i18n
    INSTALL_COMMAND ${molga_text_icu_env} "${MOLGA_TEXT_MAKE_PROGRAM}" -C common install
            COMMAND ${molga_text_icu_env} "${MOLGA_TEXT_MAKE_PROGRAM}" -C i18n install
    BUILD_IN_SOURCE 0
    # The build command produces only these four; INSTALL_COMMAND is non-empty,
    # so the installed copies belong to the install step, not here.
    BUILD_BYPRODUCTS
        "${molga_text_icu_build}/lib/libicuuc.a"
        "${molga_text_icu_build}/lib/libicui18n.a"
        "${molga_text_icu_build}/stubdata/libicudata.a"
        "${molga_text_icu_build}/stubdata/stubdata.ao"
    # The complete consumed install: two raw archives plus every flattened
    # public header. Nothing consumed downstream is an undeclared side effect.
    INSTALL_BYPRODUCTS
        ${molga_text_icu_installed_headers}
        "${molga_text_icu_raw}/lib/libicuuc.a"
        "${molga_text_icu_raw}/lib/libicui18n.a"
    USES_TERMINAL_BUILD OFF
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON)

# Make does not reliably rerun an ExternalProject step when only a byproduct is
# deleted, so the consumed set is revalidated by an always-checked wrapper.
add_custom_target(molga_text_icu_raw_install ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DMODE=RAW_INSTALL_WRAPPER"
        "-DICU_SOURCE=${molga_text_icu_source}"
        "-DICU_BUILD=${molga_text_icu_build}"
        "-DICU_RAW_PREFIX=${molga_text_icu_raw}"
        "-DMAKE_PROGRAM=${MOLGA_TEXT_MAKE_PROGRAM}"
        # The wrapper re-enumerates the headers at build time. Handing it this
        # manifest digest, not just the count, keeps the definition of "public
        # header" here alone: the two enumerations cannot drift apart without
        # one of them failing loudly.
        "-DEXPECTED_HEADER_COUNT=${MOLGA_TEXT_ICU_HEADER_COUNT}"
        "-DEXPECTED_HEADER_MANIFEST_SHA256=${MOLGA_TEXT_ICU_HEADER_MANIFEST_SHA256}"
        -P "${CMAKE_SOURCE_DIR}/cmake/RepairIcuRawInstall.cmake"
    DEPENDS molga_text_icu_external
    COMMENT "Validating the nested ICU raw install"
    VERBATIM)

# ── Logical icuuc composite ───────────────────────────────────────────────────
# The stubdata object is merged into logical icuuc so libicudata.a is never a
# third public consumer archive.
add_custom_command(
    OUTPUT "${molga_text_icu_composite_archive}"
    COMMAND "${CMAKE_COMMAND}"
        "-DRAW_ICUUC=${molga_text_icu_build}/lib/libicuuc.a"
        "-DSTUBDATA_SOURCE=${molga_text_icu_source}/stubdata/stubdata.cpp"
        "-DSTUBDATA_ARCHIVE=${molga_text_icu_build}/stubdata/libicudata.a"
        "-DOUTPUT_ICUUC=${molga_text_icu_composite_archive}"
        "-DCMAKE_AR_TOOL=${CMAKE_AR}"
        "-DCMAKE_RANLIB_TOOL=${CMAKE_RANLIB}"
        "-DCMAKE_NM_TOOL=${CMAKE_NM}"
        "-DALLOWED_ROOTS=${molga_text_deps_root};${CMAKE_SOURCE_DIR}/external/icu"
        -P "${CMAKE_SOURCE_DIR}/cmake/MergeIcuStubdata.cmake"
    DEPENDS molga_text_icu_raw_install
            "${CMAKE_SOURCE_DIR}/cmake/MergeIcuStubdata.cmake"
            "${CMAKE_SOURCE_DIR}/cmake/TextArchiveTools.cmake"
    COMMENT "Merging pinned ICU stubdata into logical icuuc"
    VERBATIM)
add_custom_target(molga_text_icu_composite ALL
    DEPENDS "${molga_text_icu_composite_archive}")

# The final i18n archive is an explicit copied output with its own producer.
add_custom_command(
    OUTPUT "${molga_text_icu_i18n_archive}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${molga_text_icu_final}/lib"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${molga_text_icu_raw}/lib/libicui18n.a"
            "${molga_text_icu_i18n_archive}"
    DEPENDS molga_text_icu_raw_install
    COMMENT "Publishing logical ICU i18n archive"
    VERBATIM)
add_custom_target(molga_text_icu_install ALL
    DEPENDS "${molga_text_icu_composite_archive}" "${molga_text_icu_i18n_archive}")

# ── Target-independent ICU link probes ────────────────────────────────────────
# These use archives directly, never the imported targets, so a broken imported
# target definition cannot mask a broken archive.
#
# The probes invoke the compiler outside CMake's own driver setup. When the
# project leaves CMAKE_OSX_SYSROOT empty, CMAKE_CXX_COMPILER may be the
# CommandLineTools binary, which — unlike /usr/bin/c++ — does not inject an SDK
# and so cannot find even <cstdint>. Resolve the SDK explicitly in that case.
set(molga_text_probe_sysroot "${CMAKE_OSX_SYSROOT}")
if(APPLE AND molga_text_probe_sysroot STREQUAL "")
    execute_process(COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE molga_text_probe_sysroot
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE molga_text_sdk_result
                    ERROR_QUIET)
    if(NOT molga_text_sdk_result EQUAL 0 OR molga_text_probe_sysroot STREQUAL "")
        message(FATAL_ERROR "cannot resolve a macOS SDK path for the text link probes")
    endif()
endif()

set(molga_text_probe_common
    "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DPROBE_SYSROOT=${molga_text_probe_sysroot}"
    "-DICU_INCLUDE_ROOT=${molga_text_icu_raw}/include")

add_custom_target(molga_text_icu_raw_link_probe
    COMMAND "${CMAKE_COMMAND}" ${molga_text_probe_common}
        "-DPROBE_SOURCE=${CMAKE_SOURCE_DIR}/tests/probes/icu_stub_link.cpp"
        "-DPROBE_ARCHIVES=${molga_text_icu_build}/lib/libicuuc.a"
        "-DOUTPUT_BINARY=${molga_text_deps_root}/probe-icu-raw"
        "-DEXPECT_SUCCESS=OFF" "-DEXPECTED_SYMBOL=icudt78_dat"
        -P "${CMAKE_SOURCE_DIR}/cmake/ProbeIcuStubdataLink.cmake"
    DEPENDS molga_text_icu_raw_install
            "${CMAKE_SOURCE_DIR}/tests/probes/icu_stub_link.cpp"
            "${CMAKE_SOURCE_DIR}/cmake/ProbeIcuStubdataLink.cmake"
    COMMENT "Probing that raw ICU common lacks icudt78_dat"
    VERBATIM)

add_custom_target(molga_text_icu_composite_link_probe
    COMMAND "${CMAKE_COMMAND}" ${molga_text_probe_common}
        "-DPROBE_SOURCE=${CMAKE_SOURCE_DIR}/tests/probes/icu_stub_link.cpp"
        "-DPROBE_ARCHIVES=${molga_text_icu_composite_archive}"
        "-DOUTPUT_BINARY=${molga_text_deps_root}/probe-icu-composite"
        "-DEXPECT_SUCCESS=ON" "-DEXPECTED_SYMBOL=icudt78_dat"
        -P "${CMAKE_SOURCE_DIR}/cmake/ProbeIcuStubdataLink.cmake"
    DEPENDS "${molga_text_icu_composite_archive}"
            "${CMAKE_SOURCE_DIR}/tests/probes/icu_stub_link.cpp"
            "${CMAKE_SOURCE_DIR}/cmake/ProbeIcuStubdataLink.cmake"
    COMMENT "Probing that composite ICU common resolves icudt78_dat"
    VERBATIM)

# ── Pinned HarfBuzz Step 4 cache matrix ───────────────────────────────────────
# One definition, three uses: it configures the nested project below, the repair
# boundary asserts it line for line against the nested CMakeCache.txt, and that
# same boundary replays it when it has to re-run configure. A second
# hand-maintained copy would only ever have to agree with this one, and
# eventually would not.
#
# Every entry is a whole typed cache line, so the assertion pins the type as
# well as the value: HB_BUILD_GPU_DEMO is upstream STRING rather than BOOL, and
# the three ICU entries carry the PATH/FILEPATH types FindICU declares for them.
set(MOLGA_TEXT_HARFBUZZ_CACHE_MATRIX
    "BUILD_SHARED_LIBS:BOOL=OFF"
    "BUILD_FRAMEWORK:BOOL=OFF"
    "HB_HAVE_ICU:BOOL=ON"
    "HB_HAVE_CORETEXT:BOOL=OFF"
    "HB_HAVE_CAIRO:BOOL=OFF"
    "HB_HAVE_FREETYPE:BOOL=OFF"
    "HB_HAVE_GRAPHITE2:BOOL=OFF"
    "HB_HAVE_GLIB:BOOL=OFF"
    "HB_HAVE_GOBJECT:BOOL=OFF"
    "HB_HAVE_INTROSPECTION:BOOL=OFF"
    "HB_BUILD_UTILS:BOOL=OFF"
    "HB_BUILD_SUBSET:BOOL=OFF"
    "HB_BUILD_RASTER:BOOL=OFF"
    "HB_BUILD_VECTOR:BOOL=OFF"
    "HB_BUILD_GPU:BOOL=OFF"
    "HB_BUILD_GPU_DEMO:STRING=OFF"
    "CMAKE_DISABLE_FIND_PACKAGE_Python3:BOOL=ON"
    "ICU_INCLUDE_DIR:PATH=${molga_text_icu_raw}/include"
    "ICU_UC_LIBRARY_RELEASE:FILEPATH=${molga_text_icu_composite_archive}"
    "ICU_UC_LIBRARY_DEBUG:FILEPATH=${molga_text_icu_composite_archive}")

# No configure argument may carry a ";" inside its value. Every consumer of
# these lists — CMAKE_ARGS, the numbered channels below — expands them
# unquoted, so a correctly escaped "-DCMAKE_OSX_ARCHITECTURES=arm64\;x86_64",
# which is exactly what an author would reasonably write, silently becomes
# "...=arm64" plus a bare "x86_64". Every length and count computed after that
# point agrees with the split, so nothing downstream can tell that one value
# was cut in half; the nested configure would just receive a stray positional.
# This has to be checked at each definition, by name, because that is the last
# moment the escaping still exists — one unquoted ${list} expansion destroys it.
function(molga_text_hb_require_separator_free list_var)
    foreach(entry IN LISTS ${list_var})
        if(entry MATCHES ";")
            message(FATAL_ERROR
                "${list_var} holds an entry containing ';', which this build's "
                "argument plumbing cannot carry: ${entry}")
        endif()
    endforeach()
endfunction()

molga_text_hb_require_separator_free(MOLGA_TEXT_HARFBUZZ_CACHE_MATRIX)

# Toolchain, prefix, and flags belong to the same exact configure command but
# are not part of the pinned Step 4 matrix. Pinned HarfBuzz does not propagate
# the nested ICU include root or U_STATIC_IMPLEMENTATION to its harfbuzz-icu
# target, which is what CMAKE_CXX_FLAGS is carrying.
set(molga_text_hb_cxx_flags
    "-I${molga_text_icu_raw}/include -DU_STATIC_IMPLEMENTATION")
set(molga_text_hb_toolchain_args
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${molga_text_hb_raw}"
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
molga_text_hb_require_separator_free(molga_text_hb_toolchain_args)
# The flags are one argument, so a separator would make them two.
list(LENGTH molga_text_hb_cxx_flags molga_text_hb_cxx_flags_length)
if(NOT molga_text_hb_cxx_flags_length EQUAL 1)
    message(FATAL_ERROR
        "molga_text_hb_cxx_flags must be a single argument, found "
        "${molga_text_hb_cxx_flags_length}: ${molga_text_hb_cxx_flags}")
endif()

# ── The one configure argument vector ─────────────────────────────────────────
# This is the whole of what the nested configure is given, and the repair
# boundary replays this exact list rather than a second description of it.
# Re-listing the toolchain half on the script side is what would let a new
# argument added here reach the ExternalProject and silently miss the replay —
# nothing asserts a toolchain entry, because the cache assertion only covers the
# 20 pinned matrix lines. Anything appended here therefore reaches both.
set(molga_text_hb_configure_args ${molga_text_hb_toolchain_args})
foreach(entry IN LISTS MOLGA_TEXT_HARFBUZZ_CACHE_MATRIX)
    list(APPEND molga_text_hb_configure_args "-D${entry}")
endforeach()
list(APPEND molga_text_hb_configure_args
     "-DCMAKE_CXX_FLAGS=${molga_text_hb_cxx_flags}")

# Both lists cross into the repair script as one numbered argument per entry
# plus an exact count, because ExternalProject_Add_Step collapses a ";"-joined
# list argument down to its first element: a single list argument would arrive
# silently truncated to one entry.
#
# The list is taken by name rather than through ARGN, because one unquoted
# ${list} expansion flattens an escaped separator before the callee could see
# it; re-checking here then still means something for a future caller that
# builds a channel from a list this file has not already vetted.
function(molga_text_hb_number_channel out_var prefix list_var)
    molga_text_hb_require_separator_free(${list_var})
    set(numbered "")
    set(index 0)
    foreach(entry IN LISTS ${list_var})
        list(APPEND numbered "-D${prefix}${index}=${entry}")
        math(EXPR index "${index} + 1")
    endforeach()
    list(APPEND numbered "-D${prefix}COUNT=${index}")
    set(${out_var} "${numbered}" PARENT_SCOPE)
endfunction()

molga_text_hb_number_channel(molga_text_hb_configure_channel
    "HARFBUZZ_CONFIGURE_ARG_" molga_text_hb_configure_args)
molga_text_hb_number_channel(molga_text_hb_matrix_channel
    "HARFBUZZ_CACHE_ENTRY_" MOLGA_TEXT_HARFBUZZ_CACHE_MATRIX)

# ── Pinned HarfBuzz public header allowlist ───────────────────────────────────
# Byproducts are file-level, so every consumed installed header is named to the
# build graph individually. 33 of these install verbatim from the pinned source;
# hb-features.h is a configure output with no source-tree authority, which is
# why RepairHarfBuzzRawInstall.cmake pins its bytes independently instead of
# comparing two possibly-identical-and-wrong copies to each other.
#
# This is the only editable copy of the list. The repair script pins the count
# and the digest of the LF-joined manifest, so a narrower or wider definition
# here fails loudly rather than leaving the boundary quietly repairing files the
# build graph no longer declares.
set(MOLGA_TEXT_HARFBUZZ_PUBLIC_HEADERS
    hb-aat-layout.h hb-aat.h hb-blob.h hb-buffer.h hb-common.h hb-cplusplus.hh
    hb-deprecated.h hb-draw.h hb-face.h hb-features.h hb-font.h hb-icu.h
    hb-map.h hb-ot-color.h hb-ot-deprecated.h hb-ot-fetch.h hb-ot-font.h
    hb-ot-layout.h hb-ot-math.h hb-ot-meta.h hb-ot-metrics.h hb-ot-name.h
    hb-ot-shape.h hb-ot-var.h hb-ot.h hb-paint.h hb-script-list.h hb-set.h
    hb-shape-plan.h hb-shape.h hb-style.h hb-unicode.h hb-version.h hb.h)

set(molga_text_hb_installed_headers "")
foreach(header IN LISTS MOLGA_TEXT_HARFBUZZ_PUBLIC_HEADERS)
    list(APPEND molga_text_hb_installed_headers
         "${molga_text_hb_raw}/include/harfbuzz/${header}")
endforeach()

# Everything both repair modes need. The generated header is produced by the
# nested configure, so the boundary has to be able to replay that exact command
# rather than only report that the header is wrong.
set(molga_text_hb_repair_common
    "-DHARFBUZZ_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
    "-DHARFBUZZ_DEPS_ROOT=${molga_text_deps_root}"
    "-DHARFBUZZ_SOURCE=${molga_text_hb_source}"
    "-DHARFBUZZ_BUILD=${molga_text_hb_build}"
    "-DHARFBUZZ_RAW_PREFIX=${molga_text_hb_raw}"
    # -G is not part of CMAKE_ARGS: ExternalProject appends it itself, so the
    # replay has to be told the generator separately.
    "-DHARFBUZZ_GENERATOR=${CMAKE_GENERATOR}"
    ${molga_text_hb_configure_channel}
    ${molga_text_hb_matrix_channel})

# ── Nested static HarfBuzz ────────────────────────────────────────────────────
# At the pinned commit HB_HAVE_ICU=ON deliberately splits the ICU adapter into a
# separate libharfbuzz-icu.a; that split is merged below, never assumed away.
ExternalProject_Add(molga_text_harfbuzz_external
    SOURCE_DIR "${molga_text_hb_source}"
    BINARY_DIR "${molga_text_hb_build}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CMAKE_COMMAND ${molga_text_zero_ar} "${CMAKE_COMMAND}"
    CMAKE_ARGS ${molga_text_hb_configure_args}
    BUILD_COMMAND ${molga_text_zero_ar} "${CMAKE_COMMAND}" --build <BINARY_DIR>
    INSTALL_COMMAND ${molga_text_zero_ar} "${CMAKE_COMMAND}" --install <BINARY_DIR>
    DEPENDS molga_text_icu_composite
    # The build command produces exactly these two. The installed copies belong
    # to the install step and hb-features.h to configure, so neither is listed
    # here: a misdeclared byproduct teaches the generator the wrong producer.
    BUILD_BYPRODUCTS
        "${molga_text_hb_build}/libharfbuzz.a"
        "${molga_text_hb_build}/libharfbuzz-icu.a"
    # The complete consumed install: two raw archives plus the 34 pinned public
    # headers. Upstream's installed CMake export and pkg-config files are
    # deliberately absent — nothing consumes them.
    INSTALL_BYPRODUCTS
        ${molga_text_hb_installed_headers}
        "${molga_text_hb_raw}/lib/libharfbuzz.a"
        "${molga_text_hb_raw}/lib/libharfbuzz-icu.a"
    USES_TERMINAL_BUILD OFF
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON)

# src/hb-features.h is written by the nested configure, not by its build
# command, so it cannot honestly be a BUILD_BYPRODUCTS entry. This step is its
# real producer: it sits between configure and build, owns that one output, and
# gives a file-level generator something to schedule when the header goes
# missing. The always-checked wrapper below covers Make, which only touches a
# deleted byproduct rather than remaking it.
ExternalProject_Add_Step(molga_text_harfbuzz_external molga_harfbuzz_generated_header
    COMMAND "${CMAKE_COMMAND}"
        "-DMODE=GENERATED_HEADER_PREBUILD"
        ${molga_text_hb_repair_common}
        -P "${CMAKE_SOURCE_DIR}/cmake/RepairHarfBuzzRawInstall.cmake"
    DEPENDEES configure
    DEPENDERS build
    DEPENDS "${molga_text_hb_source}/src/hb-features.h.in"
            "${CMAKE_SOURCE_DIR}/cmake/RepairHarfBuzzRawInstall.cmake"
            "${CMAKE_SOURCE_DIR}/cmake/TextArchiveTools.cmake"
    BYPRODUCTS "${molga_text_hb_build}/src/hb-features.h"
    COMMENT "Checking the generated HarfBuzz feature header")

# Make does not reliably rerun an ExternalProject step when only a byproduct is
# deleted, so the consumed set is revalidated by an always-checked wrapper.
add_custom_target(molga_text_harfbuzz_raw_install ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DMODE=RAW_INSTALL_WRAPPER"
        ${molga_text_hb_repair_common}
        # The wrapper is the only mode with an install to check, so it is the
        # only one handed the allowlist; the pre-build step rejects it.
        "-DHARFBUZZ_PUBLIC_HEADERS=${MOLGA_TEXT_HARFBUZZ_PUBLIC_HEADERS}"
        -P "${CMAKE_SOURCE_DIR}/cmake/RepairHarfBuzzRawInstall.cmake"
    DEPENDS molga_text_harfbuzz_external
    COMMENT "Validating the nested HarfBuzz raw install"
    VERBATIM)

# ── Logical harfbuzz composite ────────────────────────────────────────────────
add_custom_command(
    OUTPUT "${molga_text_hb_composite_archive}"
    COMMAND "${CMAKE_COMMAND}"
        "-DRAW_CORE=${molga_text_hb_build}/libharfbuzz.a"
        "-DRAW_ADAPTER=${molga_text_hb_build}/libharfbuzz-icu.a"
        "-DOUTPUT_HARFBUZZ=${molga_text_hb_composite_archive}"
        "-DCMAKE_AR_TOOL=${CMAKE_AR}"
        "-DCMAKE_RANLIB_TOOL=${CMAKE_RANLIB}"
        "-DCMAKE_NM_TOOL=${CMAKE_NM}"
        "-DALLOWED_ROOTS=${molga_text_deps_root}"
        -P "${CMAKE_SOURCE_DIR}/cmake/MergeHarfBuzzIcu.cmake"
    DEPENDS molga_text_harfbuzz_raw_install
            "${CMAKE_SOURCE_DIR}/cmake/MergeHarfBuzzIcu.cmake"
            "${CMAKE_SOURCE_DIR}/cmake/TextArchiveTools.cmake"
    COMMENT "Merging the HarfBuzz ICU adapter into one logical archive"
    VERBATIM)
add_custom_target(molga_text_harfbuzz_composite ALL
    DEPENDS "${molga_text_hb_composite_archive}")

set(molga_text_hb_probe_common
    "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DPROBE_SYSROOT=${molga_text_probe_sysroot}"
    "-DHARFBUZZ_INCLUDE_ROOT=${molga_text_hb_raw}/include/harfbuzz"
    "-DICU_INCLUDE_ROOT=${molga_text_icu_raw}/include"
    "-DPROBE_SOURCE=${CMAKE_SOURCE_DIR}/tests/probes/harfbuzz_icu_link.cpp"
    "-DEXPECTED_SYMBOL=hb_icu_get_unicode_funcs")

add_custom_target(molga_text_harfbuzz_raw_link_probe
    COMMAND "${CMAKE_COMMAND}" ${molga_text_hb_probe_common}
        "-DPROBE_ARCHIVES=${molga_text_hb_build}/libharfbuzz.a;${molga_text_icu_i18n_archive};${molga_text_icu_composite_archive}"
        "-DOUTPUT_BINARY=${molga_text_deps_root}/probe-harfbuzz-raw"
        "-DEXPECT_SUCCESS=OFF"
        -P "${CMAKE_SOURCE_DIR}/cmake/ProbeHarfBuzzIcuLink.cmake"
    DEPENDS molga_text_harfbuzz_raw_install "${molga_text_icu_i18n_archive}"
            "${CMAKE_SOURCE_DIR}/tests/probes/harfbuzz_icu_link.cpp"
            "${CMAKE_SOURCE_DIR}/cmake/ProbeHarfBuzzIcuLink.cmake"
    COMMENT "Probing that raw HarfBuzz core lacks the ICU adapter"
    VERBATIM)

add_custom_target(molga_text_harfbuzz_composite_link_probe
    COMMAND "${CMAKE_COMMAND}" ${molga_text_hb_probe_common}
        "-DPROBE_ARCHIVES=${molga_text_hb_composite_archive};${molga_text_icu_i18n_archive};${molga_text_icu_composite_archive}"
        "-DOUTPUT_BINARY=${molga_text_deps_root}/probe-harfbuzz-composite"
        "-DEXPECT_SUCCESS=ON"
        -P "${CMAKE_SOURCE_DIR}/cmake/ProbeHarfBuzzIcuLink.cmake"
    DEPENDS "${molga_text_hb_composite_archive}" "${molga_text_icu_i18n_archive}"
            "${CMAKE_SOURCE_DIR}/tests/probes/harfbuzz_icu_link.cpp"
            "${CMAKE_SOURCE_DIR}/cmake/ProbeHarfBuzzIcuLink.cmake"
    COMMENT "Probing that composite HarfBuzz resolves the ICU adapter"
    VERBATIM)

add_custom_target(molga_text_harfbuzz_install ALL
    DEPENDS molga_text_harfbuzz_composite_link_probe molga_text_harfbuzz_raw_install)

# ── Imported public targets ───────────────────────────────────────────────────
# Bound to anticipated nested paths only; no find_path/find_library/REAL_PATH
# and no archive hashing happens during configure.
file(MAKE_DIRECTORY "${molga_text_icu_raw}/include")
file(MAKE_DIRECTORY "${molga_text_hb_raw}/include/harfbuzz")

add_library(molga_text_icuuc STATIC IMPORTED GLOBAL)
add_library(molga_text_icui18n STATIC IMPORTED GLOBAL)
add_library(molga_text_harfbuzz STATIC IMPORTED GLOBAL)
add_library(molga_text_rasterizer INTERFACE)

set_target_properties(molga_text_icuuc PROPERTIES
    IMPORTED_LOCATION "${molga_text_icu_composite_archive}"
    INTERFACE_INCLUDE_DIRECTORIES "${molga_text_icu_raw}/include")
set_target_properties(molga_text_icui18n PROPERTIES
    IMPORTED_LOCATION "${molga_text_icu_i18n_archive}"
    INTERFACE_INCLUDE_DIRECTORIES "${molga_text_icu_raw}/include")
set_target_properties(molga_text_harfbuzz PROPERTIES
    IMPORTED_LOCATION "${molga_text_hb_composite_archive}"
    INTERFACE_INCLUDE_DIRECTORIES "${molga_text_hb_raw}/include/harfbuzz")
target_include_directories(molga_text_rasterizer INTERFACE "${CMAKE_SOURCE_DIR}/external")

# One-pass static archive order: harfbuzz -> icui18n -> icuuc.
set_property(TARGET molga_text_icui18n PROPERTY
    INTERFACE_LINK_LIBRARIES "molga_text_icuuc")
set_property(TARGET molga_text_harfbuzz PROPERTY
    INTERFACE_LINK_LIBRARIES "molga_text_icui18n;molga_text_icuuc")

add_dependencies(molga_text_icuuc molga_text_icu_composite)
add_dependencies(molga_text_icui18n molga_text_icu_install)
add_dependencies(molga_text_harfbuzz molga_text_harfbuzz_composite)

# ── Verification barrier ──────────────────────────────────────────────────────
add_custom_target(molga_text_dependencies_ready ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DMODE=PUBLISH"
        "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DBINARY_ROOT=${CMAKE_BINARY_DIR}"
        "-DDEPS_ROOT=${molga_text_deps_root}"
        "-DICU_SOURCE=${molga_text_icu_source}"
        "-DICU_BUILD=${molga_text_icu_build}"
        "-DICU_RAW_PREFIX=${molga_text_icu_raw}"
        "-DHARFBUZZ_SOURCE=${molga_text_hb_source}"
        "-DHARFBUZZ_BUILD=${molga_text_hb_build}"
        "-DHARFBUZZ_RAW_PREFIX=${molga_text_hb_raw}"
        "-DICU_COMPOSITE=${molga_text_icu_composite_archive}"
        "-DICU_I18N=${molga_text_icu_i18n_archive}"
        "-DHARFBUZZ_COMPOSITE=${molga_text_hb_composite_archive}"
        "-DICU_COMMIT=${MOLGA_TEXT_ICU_COMMIT}"
        "-DHARFBUZZ_COMMIT=${MOLGA_TEXT_HARFBUZZ_COMMIT}"
        "-DCMAKE_AR_TOOL=${CMAKE_AR}"
        "-DCMAKE_RANLIB_TOOL=${CMAKE_RANLIB}"
        "-DCMAKE_NM_TOOL=${CMAKE_NM}"
        "-DBUILD_LOCK=${MOLGA_TEXT_DEPENDENCY_BUILD_LOCK}"
        "-DPORTABLE_CONTRACT=${MOLGA_TEXT_DEPENDENCY_CONTRACT}"
        "-DCONTRACT_INPUT=${CMAKE_SOURCE_DIR}/resources/text/dependency-contract.input.json"
        -P "${CMAKE_SOURCE_DIR}/cmake/VerifyTextDependencies.cmake"
    BYPRODUCTS "${MOLGA_TEXT_DEPENDENCY_BUILD_LOCK}"
               "${MOLGA_TEXT_DEPENDENCY_CONTRACT}"
    DEPENDS molga_text_icu_install molga_text_harfbuzz_install
            molga_text_icu_composite_link_probe
            molga_text_harfbuzz_composite_link_probe
            "${CMAKE_SOURCE_DIR}/cmake/VerifyTextDependencies.cmake"
    COMMENT "Verifying text dependencies and publishing provenance records"
    VERBATIM)

# ── Attach helpers ────────────────────────────────────────────────────────────
# Only this helper adds the build-order edge, so composing the helpers below
# can never duplicate it.
function(molga_attach_text_verification_barrier target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "text verification consumer target does not exist: ${target_name}")
    endif()
    get_property(text_barrier_attached TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_VERIFICATION_BARRIER_ATTACHED)
    if(text_barrier_attached)
        return()
    endif()
    add_dependencies("${target_name}" molga_text_dependencies_ready)
    set_property(TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_VERIFICATION_BARRIER_ATTACHED TRUE)
endfunction()

# Exposes only the portable contract path, never the machine-local build lock.
function(molga_attach_text_portable_contract_consumer target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "portable text contract target does not exist: ${target_name}")
    endif()
    get_property(portable_contract_attached TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_PORTABLE_CONTRACT_ATTACHED)
    if(portable_contract_attached)
        return()
    endif()
    target_compile_definitions("${target_name}" PRIVATE
        MOLGA_TEXT_DEPENDENCY_CONTRACT="${MOLGA_TEXT_DEPENDENCY_CONTRACT}")
    molga_attach_text_verification_barrier("${target_name}")
    set_property(TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_PORTABLE_CONTRACT_ATTACHED TRUE)
endfunction()

# The sole compile-definition path for machine-local provenance macros.
function(molga_attach_text_provenance_test target_name)
    if(NOT target_name STREQUAL "test_text_dependencies" OR
       NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "text provenance macros are test_text_dependencies-only: ${target_name}")
    endif()
    get_property(provenance_test_attached TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_PROVENANCE_TEST_ATTACHED)
    if(provenance_test_attached)
        return()
    endif()
    molga_attach_text_portable_contract_consumer("${target_name}")
    target_compile_definitions("${target_name}" PRIVATE
        MOLGA_TEXT_DEPENDENCY_BUILD_LOCK="${MOLGA_TEXT_DEPENDENCY_BUILD_LOCK}"
        MOLGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        MOLGA_BINARY_DIR="${CMAKE_BINARY_DIR}"
        MOLGA_CMAKE_COMMAND="${CMAKE_COMMAND}"
        MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT="${CMAKE_SOURCE_DIR}/cmake/VerifyTextDependencies.cmake")
    set_property(TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_PROVENANCE_TEST_ATTACHED TRUE)
endfunction()

# Normal consumer: HarfBuzz supplies its ICU transitives in archive-safe order.
function(molga_attach_text_dependencies target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "text dependency consumer target does not exist: ${target_name}")
    endif()
    get_property(text_dependencies_attached TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_DEPENDENCIES_ATTACHED)
    if(text_dependencies_attached)
        return()
    endif()
    target_link_libraries("${target_name}" PRIVATE
        molga_text_harfbuzz molga_text_rasterizer)
    target_compile_definitions("${target_name}" PRIVATE U_STATIC_IMPLEMENTATION)
    molga_attach_text_verification_barrier("${target_name}")
    set_property(TARGET "${target_name}"
        PROPERTY MOLGA_TEXT_DEPENDENCIES_ATTACHED TRUE)
endfunction()

# Direct ICU consumer: i18n before common, no HarfBuzz, no rasterizer.
function(molga_attach_direct_icu_consumer target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "direct ICU consumer target does not exist: ${target_name}")
    endif()
    target_link_libraries("${target_name}" PRIVATE
        molga_text_icui18n molga_text_icuuc)
    target_compile_definitions("${target_name}" PRIVATE U_STATIC_IMPLEMENTATION)
    molga_attach_text_verification_barrier("${target_name}")
endfunction()

# ── Macro scope audit ─────────────────────────────────────────────────────────
# The machine-local provenance macros must never reach a product target. This
# runs at configure time so a stray attach call fails the build, not a review.
function(molga_text_count_macro out_var target_name macro_name)
    get_property(definitions TARGET "${target_name}" PROPERTY COMPILE_DEFINITIONS)
    set(count 0)
    foreach(definition IN LISTS definitions)
        if(definition MATCHES "^${macro_name}(=|$)")
            math(EXPR count "${count} + 1")
        endif()
    endforeach()
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()

set(MOLGA_TEXT_MACHINE_LOCAL_MACROS
    MOLGA_TEXT_DEPENDENCY_BUILD_LOCK
    MOLGA_SOURCE_DIR
    MOLGA_BINARY_DIR
    MOLGA_CMAKE_COMMAND
    MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT)

function(molga_text_audit_macro_scope)
    # The provenance test carries all six test-private macros exactly once.
    foreach(macro_name ${MOLGA_TEXT_MACHINE_LOCAL_MACROS} MOLGA_TEXT_DEPENDENCY_CONTRACT)
        molga_text_count_macro(count test_text_dependencies "${macro_name}")
        if(NOT count EQUAL 1)
            message(FATAL_ERROR
                "test_text_dependencies must define ${macro_name} exactly once, found ${count}")
        endif()
    endforeach()

    # No product or non-provenance target may see a machine-local macro.
    foreach(target_name ${ARGN})
        if(NOT TARGET "${target_name}")
            continue()
        endif()
        foreach(macro_name ${MOLGA_TEXT_MACHINE_LOCAL_MACROS})
            molga_text_count_macro(count "${target_name}" "${macro_name}")
            if(NOT count EQUAL 0)
                message(FATAL_ERROR
                    "${target_name} must not define the machine-local macro ${macro_name}")
            endif()
        endforeach()
    endforeach()
    message(STATUS "text dependency macro scope audit passed")
endfunction()

# ── Development Engine/Text staging ───────────────────────────────────────────
# Editor and development runtime read only
# $<TARGET_FILE_DIR>/Engine/Text/{text_dependency_contract.json,icudt78l.dat}.
# These helpers put the barrier-verified pair there and keep it there.
#
# Packaging handoff: Milestone 16 must call molga_stage_text_runtime_resources
# for its molga_runtime_dev target and copy the same verified Engine/Text pair
# into the final runtime bundle before switching packaged initialization to
# packagedRuntime=true. Package staging may not bypass
# StageTextRuntimeResources.cmake or substitute a source-tree path. The
# TextRuntimeManifest it writes must contain exactly one logical harfbuzz
# record whose SHA is the portable harfbuzz.compositeSha256, exactly one
# icui18n record, and exactly one logical icuuc record whose SHA is the
# composite common SHA. Raw harfbuzz-icu, raw core/adapter/object provenance
# and ICU stub source/object provenance stay build-lock-only and may not become
# additional manifest libraries.
#
# Until then GameBuilder::CopyTextRuntimeResources copies the editor's own
# staged pair — the output of this script, re-verified through
# molga::text::VerifyPackagedIcuDataFile — into the flat development package,
# because the packaged runtime verifies that pair before anything else and
# returns 4 without it. That is the same Engine/Text pair the approved bundle
# layout already lists, arriving early in the development-compatible flat root.
# Task 16.2 extends that seam into Contents/Resources staging plus the
# TextRuntimeManifest; it must not become a second staging path beside it.
function(molga_define_text_runtime_resource_stage stage_name destination_root)
  if(TARGET "${stage_name}")
    message(FATAL_ERROR "text runtime stage already exists: ${stage_name}")
  endif()
  if("${destination_root}" STREQUAL "")
    message(FATAL_ERROR "text runtime destination is empty: ${stage_name}")
  endif()
  add_custom_target("${stage_name}"
    COMMAND "${CMAKE_COMMAND}"
      "-DCONTRACT_SOURCE=${MOLGA_TEXT_DEPENDENCY_CONTRACT}"
      "-DICU_DATA_SOURCE=${CMAKE_SOURCE_DIR}/resources/text/icudt78l.dat"
      "-DDESTINATION_ROOT=${destination_root}"
      -P "${CMAKE_SOURCE_DIR}/cmake/StageTextRuntimeResources.cmake"
    DEPENDS molga_text_dependencies_ready
            "${CMAKE_SOURCE_DIR}/resources/text/icudt78l.dat"
    VERBATIM)
endfunction()

# Note for whoever adds the third consumer: molga_engine and molga_runtime both
# land in the same output directory, so their two always-run stage targets
# resolve to one destination root and each hashes ~66 MB (source verify plus
# fast-path check) on every build, serialized on the file(LOCK). The lock keeps
# that correct; the duplicated work is pure waste. It is not deduplicated here
# because the destination is a generator expression and cannot be compared at
# configure time. Milestone 16's molga_runtime_dev makes it 3x.
function(molga_stage_text_runtime_resources target_name)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "text runtime resource target does not exist: ${target_name}")
  endif()
  set(stage_target "${target_name}_text_runtime_resources")
  if(TARGET "${stage_target}")
    message(FATAL_ERROR "text runtime resources already attached: ${target_name}")
  endif()
  molga_define_text_runtime_resource_stage("${stage_target}"
    "$<TARGET_FILE_DIR:${target_name}>/Engine/Text")
  add_dependencies("${target_name}" "${stage_target}")
endfunction()
