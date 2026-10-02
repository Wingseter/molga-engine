# Shared static-archive helpers for the text dependency composites.
#
# Both composite steps (ICU stubdata, HarfBuzz ICU adapter) enumerate archive
# members, filter only known archive-table pseudo-members, extract one exact
# named object, and append it deterministically. That logic lives here so the
# two merge scripts cannot drift apart in what they consider a valid member.

cmake_minimum_required(VERSION 3.27)

# The only archive-table pseudo-members either merge step may filter.
# Anything else that is not a regular object is a hard failure.
set(TEXT_ARCHIVE_PSEUDO_MEMBERS "__.SYMDEF" "__.SYMDEF SORTED" "/" "//")

# Canonicalize ${path} and require it to sit inside one of ${roots}.
# An output path need not exist yet, so resolve the deepest existing ancestor
# and reattach the remainder rather than calling REAL_PATH on a missing file.
function(text_require_canonical_under out_var path roots)
    get_filename_component(absolute "${path}" ABSOLUTE)
    set(tail "")
    set(probe "${absolute}")
    while(NOT EXISTS "${probe}")
        get_filename_component(leaf "${probe}" NAME)
        get_filename_component(parent "${probe}" DIRECTORY)
        if(parent STREQUAL probe OR parent STREQUAL "")
            message(FATAL_ERROR "cannot resolve any existing ancestor of ${path}")
        endif()
        if(tail STREQUAL "")
            set(tail "${leaf}")
        else()
            set(tail "${leaf}/${tail}")
        endif()
        set(probe "${parent}")
    endwhile()
    file(REAL_PATH "${probe}" canonical)
    if(NOT tail STREQUAL "")
        set(canonical "${canonical}/${tail}")
    endif()
    set(allowed FALSE)
    foreach(root IN LISTS roots)
        file(REAL_PATH "${root}" canonical_root)
        if(canonical STREQUAL canonical_root)
            set(allowed TRUE)
            break()
        endif()
        string(LENGTH "${canonical_root}" root_length)
        string(SUBSTRING "${canonical}" 0 ${root_length} prefix)
        if(prefix STREQUAL canonical_root)
            string(SUBSTRING "${canonical}" ${root_length} -1 suffix)
            if(suffix MATCHES "^/")
                set(allowed TRUE)
                break()
            endif()
        endif()
    endforeach()
    if(NOT allowed)
        message(FATAL_ERROR "path escapes the approved roots: ${path}")
    endif()
    set(${out_var} "${canonical}" PARENT_SCOPE)
endfunction()

# Ordered raw member listing exactly as the archiver reports it.
function(text_archive_member_listing out_var ar archive)
    execute_process(COMMAND "${ar}" -t "${archive}"
                    OUTPUT_VARIABLE listing
                    ERROR_VARIABLE listing_error
                    RESULT_VARIABLE listing_result)
    if(NOT listing_result EQUAL 0)
        message(FATAL_ERROR "listing ${archive} failed: ${listing_error}")
    endif()
    set(${out_var} "${listing}" PARENT_SCOPE)
endfunction()

# Split an archive into regular object members and filtered pseudo-members,
# rejecting duplicates and any unknown non-object entry.
function(text_archive_regular_members out_members out_filtered ar archive)
    text_archive_member_listing(listing "${ar}" "${archive}")
    string(REPLACE "\n" ";" listing_lines "${listing}")
    set(members "")
    set(filtered "")
    foreach(line IN LISTS listing_lines)
        string(STRIP "${line}" entry)
        if(entry STREQUAL "")
            continue()
        endif()
        if(entry IN_LIST TEXT_ARCHIVE_PSEUDO_MEMBERS)
            list(APPEND filtered "${entry}")
            continue()
        endif()
        if(NOT entry MATCHES "\\.(o|ao|obj)$")
            message(FATAL_ERROR
                "unknown non-object member '${entry}' in ${archive}")
        endif()
        if(entry IN_LIST members)
            message(FATAL_ERROR
                "duplicate regular member '${entry}' in ${archive}")
        endif()
        list(APPEND members "${entry}")
    endforeach()
    set(${out_members} "${members}" PARENT_SCOPE)
    set(${out_filtered} "${filtered}" PARENT_SCOPE)
endfunction()

# Extract exactly one named member into ${destination}.
function(text_archive_extract_member ar archive member destination)
    execute_process(COMMAND "${ar}" -x "${archive}" "${member}"
                    WORKING_DIRECTORY "${destination}"
                    RESULT_VARIABLE extract_result
                    ERROR_VARIABLE extract_error)
    if(NOT extract_result EQUAL 0)
        message(FATAL_ERROR
            "extracting ${member} from ${archive} failed: ${extract_error}")
    endif()
endfunction()

# Classify the archiver so append flags stay deterministic per family.
function(text_archive_family out_var ar)
    execute_process(COMMAND "${ar}" --version
                    OUTPUT_VARIABLE version_output
                    ERROR_VARIABLE version_error
                    RESULT_VARIABLE version_result)
    set(combined "${version_output}${version_error}")
    if(version_result EQUAL 0 AND combined MATCHES "GNU ar")
        set(${out_var} "gnu" PARENT_SCOPE)
        return()
    endif()
    if(version_result EQUAL 0 AND combined MATCHES "LLVM")
        set(${out_var} "llvm" PARENT_SCOPE)
        return()
    endif()
    # Apple ar has no --version; its usage probe advertises no `D` flag.
    execute_process(COMMAND "${ar}"
                    OUTPUT_VARIABLE usage_output
                    ERROR_VARIABLE usage_error
                    RESULT_VARIABLE usage_result)
    set(usage "${usage_output}${usage_error}")
    if(usage MATCHES "usage:[ \t]*ar " AND NOT usage MATCHES "\\[-[A-Za-z]*D")
        set(${out_var} "apple" PARENT_SCOPE)
        return()
    endif()
    message(FATAL_ERROR "unknown archiver family for ${ar}: ${usage}")
endfunction()

# Append one object after every existing member, deterministically.
function(text_archive_append_object ar ranlib staged object out_family)
    text_archive_family(family "${ar}")
    if(family STREQUAL "apple")
        set(append_flags "qcs")
    elseif(family STREQUAL "gnu" OR family STREQUAL "llvm")
        set(append_flags "qcsD")
    else()
        message(FATAL_ERROR "unsupported archiver family: ${family}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${ar}" ${append_flags} "${staged}" "${object}"
        RESULT_VARIABLE append_result
        ERROR_VARIABLE append_error)
    if(NOT append_result EQUAL 0)
        message(FATAL_ERROR "appending ${object} failed: ${append_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${ranlib}" -D "${staged}"
        RESULT_VARIABLE ranlib_result
        ERROR_VARIABLE ranlib_error)
    if(NOT ranlib_result EQUAL 0)
        message(FATAL_ERROR "ranlib on ${staged} failed: ${ranlib_error}")
    endif()
    set(${out_family} "${family}" PARENT_SCOPE)
endfunction()

# Count defined-symbol lines matching ${pattern} in ${archive}.
function(text_archive_defined_symbol_count out_var nm archive pattern)
    execute_process(COMMAND "${nm}" -g "${archive}"
                    OUTPUT_VARIABLE symbol_output
                    ERROR_VARIABLE symbol_error
                    RESULT_VARIABLE symbol_result)
    if(NOT symbol_result EQUAL 0)
        message(FATAL_ERROR "nm on ${archive} failed: ${symbol_error}")
    endif()
    string(REPLACE "\n" ";" symbol_lines "${symbol_output}")
    set(count 0)
    foreach(line IN LISTS symbol_lines)
        if(line MATCHES "${pattern}")
            math(EXPR count "${count} + 1")
        endif()
    endforeach()
    set(${out_var} ${count} PARENT_SCOPE)
endfunction()
