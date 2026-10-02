# Maintainer-only acquisition of the locked multilingual font fixture corpus.
#
#   cmake -DSOURCE_ROOT="$PWD" -P cmake/AcquireTextFixtures.cmake
#
# The complete table is downloaded into one unique root and validated before a
# single transaction publishes it; a partial table is never published.

cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT must name the repository root")
endif()
get_filename_component(SOURCE_ROOT "${SOURCE_ROOT}" ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/TextArtifactTransaction.cmake")

set(TEXT_ACQUIRE_TMP "${SOURCE_ROOT}/.text-acquire-tmp")
set(TEXT_FIXTURE_JOURNAL "${TEXT_ACQUIRE_TMP}/fixture-journal.json")
set(TEXT_FIXTURE_ROOT "${SOURCE_ROOT}/tests/fixtures/text")
set(TEXT_FIXTURE_ALLOWED_ROOTS "${TEXT_ACQUIRE_TMP}" "${TEXT_FIXTURE_ROOT}")

set(NOTO_FONTS_COMMIT "ffebf8c1ee449e544955a7e813c54f9b73848eac")
set(NOTO_CJK_COMMIT "523d033d6cb47f4a80c58a35753646f5c3608a78")
set(NOTO_FONTS_RAW
    "https://raw.githubusercontent.com/notofonts/noto-fonts/${NOTO_FONTS_COMMIT}")
set(NOTO_CJK_RAW
    "https://raw.githubusercontent.com/notofonts/noto-cjk/${NOTO_CJK_COMMIT}")

if(NOT DEFINED INJECT_PUBLISH_FAILURE_AT)
    set(INJECT_PUBLISH_FAILURE_AT 0)
endif()

# ── Locked fixture table: destination | url | bytes | sha256 ─────────────────
# "manifest" in the size column means the manifest records the observed size
# and only the hash is asserted here.
set(TEXT_FIXTURE_DESTINATIONS
    "fonts/NotoSans-Regular.ttf"
    "fonts/NotoSansArabic-Regular.ttf"
    "fonts/NotoSansHebrew-Regular.ttf"
    "fonts/NotoSansDevanagari-Regular.ttf"
    "fonts/NotoSansThai-Regular.ttf"
    "fonts/NotoSansKR-Regular.otf"
    "licenses/NotoFonts-ffebf8c1-OFL.txt"
    "licenses/NotoCJK-Sans2.004-OFL.txt")
set(TEXT_FIXTURE_URLS
    "${NOTO_FONTS_RAW}/hinted/ttf/NotoSans/NotoSans-Regular.ttf"
    "${NOTO_FONTS_RAW}/hinted/ttf/NotoSansArabic/NotoSansArabic-Regular.ttf"
    "${NOTO_FONTS_RAW}/hinted/ttf/NotoSansHebrew/NotoSansHebrew-Regular.ttf"
    "${NOTO_FONTS_RAW}/hinted/ttf/NotoSansDevanagari/NotoSansDevanagari-Regular.ttf"
    "${NOTO_FONTS_RAW}/hinted/ttf/NotoSansThai/NotoSansThai-Regular.ttf"
    "${NOTO_CJK_RAW}/Sans/SubsetOTF/KR/NotoSansKR-Regular.otf"
    "${NOTO_FONTS_RAW}/LICENSE"
    "${NOTO_CJK_RAW}/LICENSE")
set(TEXT_FIXTURE_SIZES
    569208 240456 26900 219212 37752 4644748 manifest manifest)
set(TEXT_FIXTURE_SHAS
    "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5"
    "ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b"
    "a7fa16fffb27bedb060a0866267c29e9859aeb9c21cc33f5b3aaf6eb062eca85"
    "385e78e6359a9d88a0f243d53b1209d7548361ba2194e2b9ec779bcaa7e8949d"
    "404ddfb5ed0aaa6b6ec8a85700d682978992062d67da93903967b56cbd9a4acc"
    "69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68"
    "0dab92d0544f7b233403f14b84a663bdbfa746982eda629e7f4f9ffe1b036feb"
    "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2")

list(LENGTH TEXT_FIXTURE_DESTINATIONS fixture_row_count)
math(EXPR fixture_last_row "${fixture_row_count} - 1")

text_recover_artifact_journal(recovery_ok "${TEXT_FIXTURE_JOURNAL}"
                              "${TEXT_FIXTURE_ALLOWED_ROOTS}")
if(NOT recovery_ok)
    message(FATAL_ERROR "text fixture journal recovery failed")
endif()

# Offline fast path: publish nothing when every row is already exact.
set(fixture_complete TRUE)
foreach(row RANGE 0 ${fixture_last_row})
    list(GET TEXT_FIXTURE_DESTINATIONS ${row} relative_destination)
    list(GET TEXT_FIXTURE_SHAS ${row} expected_sha)
    set(destination "${TEXT_FIXTURE_ROOT}/${relative_destination}")
    if(NOT EXISTS "${destination}")
        set(fixture_complete FALSE)
        break()
    endif()
    file(SHA256 "${destination}" actual_sha)
    if(NOT actual_sha STREQUAL expected_sha)
        set(fixture_complete FALSE)
        break()
    endif()
endforeach()
if(fixture_complete)
    message(STATUS "all text fixture artifacts already verified")
    return()
endif()

# Step 15c: download the complete set into one unique root.
string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef fixture_acquire_id)
set(acquire_root "${TEXT_ACQUIRE_TMP}/fixtures-${fixture_acquire_id}")
file(MAKE_DIRECTORY "${acquire_root}")

set(fixture_pairs "")
foreach(row RANGE 0 ${fixture_last_row})
    list(GET TEXT_FIXTURE_DESTINATIONS ${row} relative_destination)
    list(GET TEXT_FIXTURE_URLS ${row} url)
    list(GET TEXT_FIXTURE_SIZES ${row} expected_size)
    list(GET TEXT_FIXTURE_SHAS ${row} expected_sha)

    get_filename_component(destination_name "${relative_destination}" NAME)
    set(staged "${acquire_root}/${destination_name}")
    if(TEXT_FAIL_ON_NETWORK)
        message(FATAL_ERROR
            "network access attempted while TEXT_FAIL_ON_NETWORK=ON: ${url}")
    endif()
    file(DOWNLOAD "${url}" "${staged}"
         EXPECTED_HASH "SHA256=${expected_sha}" STATUS download_status)
    list(GET download_status 0 download_code)
    if(NOT download_code EQUAL 0)
        message(FATAL_ERROR "fixture download failed for ${url}: ${download_status}")
    endif()

    # Step 15d: revalidate every declared size/hash before publication.
    file(SHA256 "${staged}" staged_sha)
    if(NOT staged_sha STREQUAL expected_sha)
        message(FATAL_ERROR "fixture hash mismatch for ${relative_destination}")
    endif()
    if(NOT expected_size STREQUAL "manifest")
        file(SIZE "${staged}" staged_size)
        if(NOT staged_size EQUAL expected_size)
            message(FATAL_ERROR
                "fixture size mismatch for ${relative_destination}: ${staged_size}")
        endif()
    endif()

    list(APPEND fixture_pairs
         "${staged}" "${TEXT_FIXTURE_ROOT}/${relative_destination}")
endforeach()

text_publish_artifact_set(publish_ok "${TEXT_FIXTURE_JOURNAL}"
    "${INJECT_PUBLISH_FAILURE_AT}" "${TEXT_FIXTURE_ALLOWED_ROOTS}"
    ${fixture_pairs})
if(NOT publish_ok)
    message(FATAL_ERROR "publishing the text fixture set failed")
endif()

file(REMOVE_RECURSE "${acquire_root}")
message(STATUS "published the text fixture set")
