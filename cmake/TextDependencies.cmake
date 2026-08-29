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
    BUILD_BYPRODUCTS
        "${molga_text_icu_build}/lib/libicuuc.a"
        "${molga_text_icu_build}/lib/libicui18n.a"
        "${molga_text_icu_build}/stubdata/libicudata.a"
        "${molga_text_icu_build}/stubdata/stubdata.ao"
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
        "-DEXPECTED_HEADER_COUNT=203"
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
    CMAKE_ARGS
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_INSTALL_PREFIX=${molga_text_hb_raw}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "-DBUILD_SHARED_LIBS:BOOL=OFF"
        "-DBUILD_FRAMEWORK:BOOL=OFF"
        "-DHB_HAVE_ICU:BOOL=ON"
        "-DHB_HAVE_CORETEXT:BOOL=OFF"
        "-DHB_HAVE_CAIRO:BOOL=OFF"
        "-DHB_HAVE_FREETYPE:BOOL=OFF"
        "-DHB_HAVE_GRAPHITE2:BOOL=OFF"
        "-DHB_HAVE_GLIB:BOOL=OFF"
        "-DHB_HAVE_GOBJECT:BOOL=OFF"
        "-DHB_HAVE_INTROSPECTION:BOOL=OFF"
        "-DHB_BUILD_UTILS:BOOL=OFF"
        "-DHB_BUILD_SUBSET:BOOL=OFF"
        "-DHB_BUILD_RASTER:BOOL=OFF"
        "-DHB_BUILD_VECTOR:BOOL=OFF"
        "-DHB_BUILD_GPU:BOOL=OFF"
        "-DHB_BUILD_GPU_DEMO:STRING=OFF"
        "-DCMAKE_DISABLE_FIND_PACKAGE_Python3:BOOL=ON"
        "-DICU_INCLUDE_DIR=${molga_text_icu_raw}/include"
        "-DICU_UC_LIBRARY_RELEASE=${molga_text_icu_composite_archive}"
        "-DICU_UC_LIBRARY_DEBUG=${molga_text_icu_composite_archive}"
        # Pinned HarfBuzz does not propagate these to the harfbuzz-icu target.
        "-DCMAKE_CXX_FLAGS=-I${molga_text_icu_raw}/include -DU_STATIC_IMPLEMENTATION"
    BUILD_COMMAND ${molga_text_zero_ar} "${CMAKE_COMMAND}" --build <BINARY_DIR>
    INSTALL_COMMAND ${molga_text_zero_ar} "${CMAKE_COMMAND}" --install <BINARY_DIR>
    DEPENDS molga_text_icu_composite
    BUILD_BYPRODUCTS
        "${molga_text_hb_build}/libharfbuzz.a"
        "${molga_text_hb_build}/libharfbuzz-icu.a"
        "${molga_text_hb_raw}/lib/libharfbuzz.a"
        "${molga_text_hb_raw}/lib/libharfbuzz-icu.a"
    USES_TERMINAL_BUILD OFF
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON)

add_custom_target(molga_text_harfbuzz_raw_install ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DMODE=RAW_INSTALL_WRAPPER"
        "-DHARFBUZZ_SOURCE=${molga_text_hb_source}"
        "-DHARFBUZZ_BUILD=${molga_text_hb_build}"
        "-DHARFBUZZ_RAW_PREFIX=${molga_text_hb_raw}"
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
