# Transactional publication of immutable text artifacts.
#
# Callers stage verified bytes in a temporary root, then hand this module
# alternating staged/destination pairs. Publication is all-or-nothing: a crash
# or injected failure at any point leaves every destination holding either its
# complete old bytes or its complete new bytes, never a partial write.
#
# Journal schema 1 records every exact path, SHA-256, and per-entry state so a
# later run can finish the rollback that an interrupted run started.

cmake_minimum_required(VERSION 3.27)

# Atomically replace ${path} with ${content} via a same-directory sibling.
function(_text_atomic_write path content)
    get_filename_component(parent "${path}" DIRECTORY)
    get_filename_component(name "${path}" NAME)
    file(MAKE_DIRECTORY "${parent}")
    string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef write_id)
    set(temporary "${parent}/.${name}.molga-write-${write_id}")
    file(WRITE "${temporary}" "${content}")
    file(RENAME "${temporary}" "${path}" RESULT rename_result)
    if(NOT rename_result EQUAL 0)
        file(REMOVE "${temporary}")
        message(FATAL_ERROR "atomic write failed for ${path}: ${rename_result}")
    endif()
endfunction()

# TRUE when ${path} equals or lives under one of ${roots}.
function(_text_path_is_allowed result_var path roots)
    get_filename_component(candidate "${path}" ABSOLUTE)
    set(allowed FALSE)
    foreach(root IN LISTS roots)
        get_filename_component(canonical_root "${root}" ABSOLUTE)
        if(candidate STREQUAL canonical_root)
            set(allowed TRUE)
            break()
        endif()
        string(LENGTH "${canonical_root}" root_length)
        string(SUBSTRING "${candidate}" 0 ${root_length} candidate_prefix)
        if(candidate_prefix STREQUAL canonical_root)
            string(SUBSTRING "${candidate}" ${root_length} -1 candidate_suffix)
            if(candidate_suffix MATCHES "^/")
                set(allowed TRUE)
                break()
            endif()
        endif()
    endforeach()
    set(${result_var} ${allowed} PARENT_SCOPE)
endfunction()

# Remove siblings staged by a prepare that never published a journal.
# Only prepare may call this: once a journal exists, recovery owns cleanup.
function(_text_discard_prepared_siblings created_siblings transaction_directory)
    foreach(sibling IN LISTS created_siblings)
        file(REMOVE "${sibling}")
    endforeach()
    if(NOT transaction_directory STREQUAL "")
        file(REMOVE_RECURSE "${transaction_directory}")
    endif()
endfunction()

# Rewrite one journal entry's state and atomically republish the journal.
function(_text_journal_set_state journal_path index state)
    file(READ "${journal_path}" journal_json)
    string(JSON updated_json SET "${journal_json}" entries ${index} state
           "\"${state}\"")
    _text_atomic_write("${journal_path}" "${updated_json}")
endfunction()

# Validate the caller's staged/destination pairs, stage every new and rollback
# sibling, and persist a prepared schema-1 journal. Sets ${result_var} FALSE
# without touching a destination when any input is rejected.
function(text_prepare_artifact_transaction result_var journal_path
         allowed_destination_roots pair_args)
    set(${result_var} FALSE PARENT_SCOPE)

    list(LENGTH pair_args pair_arg_count)
    math(EXPR entry_count "${pair_arg_count} / 2")

    set(staged_paths "")
    set(destination_paths "")
    set(staged_shas "")
    set(destination_existed "")
    set(destination_shas "")

    math(EXPR last_pair "${entry_count} - 1")
    foreach(pair_index RANGE 0 ${last_pair})
        math(EXPR staged_arg "${pair_index} * 2")
        math(EXPR destination_arg "${staged_arg} + 1")
        list(GET pair_args ${staged_arg} staged)
        list(GET pair_args ${destination_arg} destination)

        if(staged MATCHES ";" OR destination MATCHES ";")
            message(WARNING "text transaction rejects a path containing ';'")
            return()
        endif()
        get_filename_component(staged "${staged}" ABSOLUTE)
        get_filename_component(destination "${destination}" ABSOLUTE)
        if(staged STREQUAL destination)
            message(WARNING "text transaction rejects staged == destination: ${staged}")
            return()
        endif()
        if(destination IN_LIST destination_paths)
            message(WARNING "text transaction rejects duplicate destination: ${destination}")
            return()
        endif()
        if(NOT EXISTS "${staged}")
            message(WARNING "text transaction rejects a missing staged file: ${staged}")
            return()
        endif()
        _text_path_is_allowed(destination_allowed "${destination}"
                              "${allowed_destination_roots}")
        if(NOT destination_allowed)
            message(WARNING "text transaction rejects an out-of-root destination: ${destination}")
            return()
        endif()

        file(SHA256 "${staged}" staged_sha)
        # `list(APPEND var "")` on an empty list appends no element at all, so an
        # absent destination records the sentinel and is unpacked when written.
        if(EXISTS "${destination}")
            file(SHA256 "${destination}" destination_sha)
            list(APPEND destination_existed "true")
            list(APPEND destination_shas "${destination_sha}")
        else()
            list(APPEND destination_existed "false")
            list(APPEND destination_shas "NONE")
        endif()
        list(APPEND staged_paths "${staged}")
        list(APPEND destination_paths "${destination}")
        list(APPEND staged_shas "${staged_sha}")
    endforeach()

    string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef transaction_id)
    get_filename_component(journal_parent "${journal_path}" DIRECTORY)
    set(transaction_directory "${journal_parent}/.molga-text-txn-${transaction_id}")
    _text_path_is_allowed(transaction_allowed "${transaction_directory}"
                          "${allowed_destination_roots}")
    if(NOT transaction_allowed)
        message(WARNING "text transaction directory is outside the approved roots")
        return()
    endif()
    file(MAKE_DIRECTORY "${transaction_directory}")

    # Stage every new and rollback sibling before any destination is replaced.
    # Siblings live beside their destinations, which are tracked repository
    # paths, so a failure here must remove its own partial work: no journal
    # exists yet, and journal-driven recovery could never find them.
    set(created_siblings "")
    set(prepare_failure "")
    set(entries_json "")
    foreach(pair_index RANGE 0 ${last_pair})
        math(EXPR entry_index "${pair_index} + 1")
        list(GET staged_paths ${pair_index} staged)
        list(GET destination_paths ${pair_index} destination)
        list(GET staged_shas ${pair_index} staged_sha)
        list(GET destination_existed ${pair_index} existed)
        list(GET destination_shas ${pair_index} destination_sha)
        if(destination_sha STREQUAL "NONE")
            set(destination_sha "")
        endif()

        get_filename_component(destination_parent "${destination}" DIRECTORY)
        get_filename_component(destination_name "${destination}" NAME)
        file(MAKE_DIRECTORY "${destination_parent}")

        set(new_sibling
            "${destination_parent}/.${destination_name}.molga-new-${transaction_id}-${entry_index}")
        file(COPY_FILE "${staged}" "${new_sibling}")
        list(APPEND created_siblings "${new_sibling}")
        file(SHA256 "${new_sibling}" new_sibling_sha)
        if(NOT new_sibling_sha STREQUAL staged_sha)
            set(prepare_failure "staged sibling hash mismatch for ${destination}")
            break()
        endif()

        set(backup_sibling "")
        if(existed STREQUAL "true")
            set(backup_sibling
                "${destination_parent}/.${destination_name}.molga-backup-${transaction_id}-${entry_index}")
            file(COPY_FILE "${destination}" "${backup_sibling}")
            list(APPEND created_siblings "${backup_sibling}")
            file(SHA256 "${backup_sibling}" backup_sibling_sha)
            if(NOT backup_sibling_sha STREQUAL destination_sha)
                set(prepare_failure "backup sibling hash mismatch for ${destination}")
                break()
            endif()
        endif()

        string(APPEND entries_json "
    {
      \"index\": ${entry_index},
      \"staged\": \"${staged}\",
      \"destination\": \"${destination}\",
      \"stagedSha256\": \"${staged_sha}\",
      \"destinationExisted\": ${existed},
      \"destinationSha256\": \"${destination_sha}\",
      \"newSibling\": \"${new_sibling}\",
      \"backupSibling\": \"${backup_sibling}\",
      \"state\": \"prepared\"
    }")
        if(NOT entry_index EQUAL entry_count)
            string(APPEND entries_json ",")
        endif()
    endforeach()

    if(prepare_failure)
        _text_discard_prepared_siblings("${created_siblings}" "${transaction_directory}")
        message(WARNING "${prepare_failure}")
        return()
    endif()

    _text_atomic_write("${journal_path}" "{
  \"schema\": 1,
  \"transactionId\": \"${transaction_id}\",
  \"transactionDirectory\": \"${transaction_directory}\",
  \"entryCount\": ${entry_count},
  \"entries\": [${entries_json}
  ]
}
")

    # Read back and validate the complete entry count before publishing index 1.
    file(READ "${journal_path}" journal_json)
    string(JSON persisted_schema GET "${journal_json}" schema)
    string(JSON persisted_count GET "${journal_json}" entryCount)
    string(JSON persisted_entries LENGTH "${journal_json}" entries)
    if(NOT persisted_schema EQUAL 1 OR NOT persisted_count EQUAL entry_count
       OR NOT persisted_entries EQUAL entry_count)
        file(REMOVE "${journal_path}")
        _text_discard_prepared_siblings("${created_siblings}" "${transaction_directory}")
        message(WARNING "prepared text journal failed read-back validation")
        return()
    endif()

    set(${result_var} TRUE PARENT_SCOPE)
endfunction()

# Publish a prepared journal one durable entry at a time.
# ${inject_failure_at} is 0 for normal operation, +N to fail before entry N's
# rename, or -N to fail after entry N's verified rename but before its state
# update. Both windows must leave the destination set fully recoverable.
function(text_publish_prepared_artifact_transaction result_var journal_path
         inject_failure_at)
    set(${result_var} FALSE PARENT_SCOPE)

    file(READ "${journal_path}" journal_json)
    string(JSON entry_count GET "${journal_json}" entryCount)
    string(JSON transaction_directory GET "${journal_json}" transactionDirectory)

    math(EXPR last_entry "${entry_count} - 1")
    foreach(entry_offset RANGE 0 ${last_entry})
        math(EXPR entry_index "${entry_offset} + 1")
        file(READ "${journal_path}" journal_json)
        string(JSON destination GET "${journal_json}" entries ${entry_offset} destination)
        string(JSON staged_sha GET "${journal_json}" entries ${entry_offset} stagedSha256)
        string(JSON new_sibling GET "${journal_json}" entries ${entry_offset} newSibling)

        # Mark this entry durably before its destination is touched.
        _text_journal_set_state("${journal_path}" ${entry_offset} "publishing")

        if(inject_failure_at GREATER 0 AND inject_failure_at EQUAL entry_index)
            message(STATUS "text transaction injected pre-rename failure at ${entry_index}")
            return()
        endif()

        file(RENAME "${new_sibling}" "${destination}" RESULT rename_result)
        if(NOT rename_result EQUAL 0)
            message(WARNING "text transaction rename failed for ${destination}: ${rename_result}")
            return()
        endif()
        file(SHA256 "${destination}" published_sha)
        if(NOT published_sha STREQUAL staged_sha)
            message(WARNING "text transaction published hash mismatch for ${destination}")
            return()
        endif()

        math(EXPR negative_index "0 - ${entry_index}")
        if(inject_failure_at LESS 0 AND inject_failure_at EQUAL negative_index)
            message(STATUS "text transaction injected crash-window failure at ${entry_index}")
            return()
        endif()

        _text_journal_set_state("${journal_path}" ${entry_offset} "published")
    endforeach()

    # Every destination now holds verified new bytes; drop the rollback state.
    file(READ "${journal_path}" journal_json)
    foreach(entry_offset RANGE 0 ${last_entry})
        string(JSON backup_sibling GET "${journal_json}" entries ${entry_offset} backupSibling)
        if(NOT backup_sibling STREQUAL "")
            file(REMOVE "${backup_sibling}")
        endif()
    endforeach()
    file(REMOVE_RECURSE "${transaction_directory}")
    file(REMOVE "${journal_path}")

    set(${result_var} TRUE PARENT_SCOPE)
endfunction()

# Finish any pending transaction described by ${journal_path}.
# Returns TRUE when the destination root is safe and carries no pending
# transaction, and FALSE when an external conflict blocks automatic recovery.
function(text_recover_artifact_journal result_var journal_path
         allowed_destination_roots)
    set(${result_var} FALSE PARENT_SCOPE)

    if(NOT EXISTS "${journal_path}")
        set(${result_var} TRUE PARENT_SCOPE)
        return()
    endif()

    file(READ "${journal_path}" journal_json)
    string(JSON schema ERROR_VARIABLE schema_error GET "${journal_json}" schema)
    if(schema_error OR NOT schema EQUAL 1)
        message(WARNING "text journal schema is not 1: ${journal_path}")
        return()
    endif()
    string(JSON transaction_id GET "${journal_json}" transactionId)
    string(JSON transaction_directory GET "${journal_json}" transactionDirectory)
    string(JSON entry_count GET "${journal_json}" entryCount)
    string(JSON actual_entries LENGTH "${journal_json}" entries)
    # CMake regex has no {n} repetition; check the charset and length apart.
    string(LENGTH "${transaction_id}" transaction_id_length)
    if(NOT transaction_id MATCHES "^[0-9a-f]+$" OR NOT transaction_id_length EQUAL 32)
        message(WARNING "text journal transaction id is malformed")
        return()
    endif()
    if(NOT actual_entries EQUAL entry_count)
        message(WARNING "text journal entry count disagrees with its entries")
        return()
    endif()

    # Validate every journal path against the caller's independent allowlist.
    _text_path_is_allowed(directory_allowed "${transaction_directory}"
                          "${allowed_destination_roots}")
    if(NOT directory_allowed)
        message(WARNING "text journal transaction directory is outside the approved roots")
        return()
    endif()
    math(EXPR last_entry "${entry_count} - 1")
    foreach(entry_offset RANGE 0 ${last_entry})
        math(EXPR expected_index "${entry_offset} + 1")
        string(JSON entry_index GET "${journal_json}" entries ${entry_offset} index)
        string(JSON destination GET "${journal_json}" entries ${entry_offset} destination)
        string(JSON new_sibling GET "${journal_json}" entries ${entry_offset} newSibling)
        string(JSON backup_sibling GET "${journal_json}" entries ${entry_offset} backupSibling)
        if(NOT entry_index EQUAL expected_index)
            message(WARNING "text journal entry ${entry_offset} has index ${entry_index}")
            return()
        endif()
        foreach(candidate "${destination}" "${new_sibling}" "${backup_sibling}")
            if(NOT candidate STREQUAL "")
                _text_path_is_allowed(candidate_allowed "${candidate}"
                                      "${allowed_destination_roots}")
                if(NOT candidate_allowed)
                    message(WARNING "text journal path is outside the approved roots: ${candidate}")
                    return()
                endif()
            endif()
        endforeach()
        if(NOT new_sibling MATCHES "\\.molga-new-${transaction_id}-${expected_index}$")
            message(WARNING "text journal new sibling does not match its transaction: ${new_sibling}")
            return()
        endif()
        if(NOT backup_sibling STREQUAL ""
           AND NOT backup_sibling MATCHES "\\.molga-backup-${transaction_id}-${expected_index}$")
            message(WARNING "text journal backup sibling does not match its transaction: ${backup_sibling}")
            return()
        endif()
    endforeach()

    # Roll back published/publishing destinations in reverse index order.
    foreach(entry_offset RANGE ${last_entry} 0 -1)
        string(JSON state GET "${journal_json}" entries ${entry_offset} state)
        if(NOT state STREQUAL "published" AND NOT state STREQUAL "publishing")
            continue()
        endif()
        string(JSON destination GET "${journal_json}" entries ${entry_offset} destination)
        string(JSON staged_sha GET "${journal_json}" entries ${entry_offset} stagedSha256)
        string(JSON existed GET "${journal_json}" entries ${entry_offset} destinationExisted)
        string(JSON destination_sha GET "${journal_json}" entries ${entry_offset} destinationSha256)
        string(JSON backup_sibling GET "${journal_json}" entries ${entry_offset} backupSibling)

        set(current_sha "")
        if(EXISTS "${destination}")
            file(SHA256 "${destination}" current_sha)
        endif()

        set(needs_restore FALSE)
        if(current_sha STREQUAL staged_sha AND NOT current_sha STREQUAL "")
            set(needs_restore TRUE)
        elseif(existed AND current_sha STREQUAL destination_sha)
            # Already holds its recorded old bytes; no write is required.
        elseif(NOT existed AND current_sha STREQUAL "")
            # Never created; no write is required.
        else()
            message(WARNING
                "text journal external conflict at ${destination}; journal and backups retained")
            return()
        endif()

        if(needs_restore)
            if(existed)
                file(RENAME "${backup_sibling}" "${destination}" RESULT restore_result)
                if(NOT restore_result EQUAL 0)
                    message(WARNING "text rollback rename failed for ${destination}: ${restore_result}")
                    return()
                endif()
                file(SHA256 "${destination}" restored_sha)
                if(NOT restored_sha STREQUAL destination_sha)
                    message(WARNING "text rollback hash mismatch for ${destination}")
                    return()
                endif()
            else()
                file(REMOVE "${destination}")
                if(EXISTS "${destination}")
                    message(WARNING "text rollback could not remove ${destination}")
                    return()
                endif()
            endif()
        endif()
    endforeach()

    # Every destination is verified old again; drop the transaction artifacts.
    foreach(entry_offset RANGE 0 ${last_entry})
        string(JSON new_sibling GET "${journal_json}" entries ${entry_offset} newSibling)
        string(JSON backup_sibling GET "${journal_json}" entries ${entry_offset} backupSibling)
        file(REMOVE "${new_sibling}")
        if(NOT backup_sibling STREQUAL "")
            file(REMOVE "${backup_sibling}")
        endif()
    endforeach()
    file(REMOVE_RECURSE "${transaction_directory}")
    file(REMOVE "${journal_path}")

    set(${result_var} TRUE PARENT_SCOPE)
endfunction()

# Single entrypoint: recover any pending journal, then publish the given
# staged/destination pairs atomically.
function(text_publish_artifact_set result_var journal_path inject_failure_at
         allowed_destination_roots)
    text_recover_artifact_journal(
      recovery_ok "${journal_path}" "${allowed_destination_roots}")
    if(NOT recovery_ok)
        message(FATAL_ERROR "text artifact journal recovery failed")
    endif()
    set(pair_args ${ARGN})
    list(LENGTH pair_args pair_arg_count)
    math(EXPR pair_remainder "${pair_arg_count} % 2")
    if(pair_arg_count EQUAL 0 OR NOT pair_remainder EQUAL 0)
        message(FATAL_ERROR
          "text_publish_artifact_set requires staged/destination pairs")
    endif()
    text_prepare_artifact_transaction(
      prepare_ok "${journal_path}" "${allowed_destination_roots}"
      "${pair_args}")
    if(NOT prepare_ok)
        set(${result_var} FALSE PARENT_SCOPE)
        return()
    endif()
    text_publish_prepared_artifact_transaction(
      publish_ok "${journal_path}" "${inject_failure_at}")
    if(NOT publish_ok)
        text_recover_artifact_journal(
          recovered "${journal_path}" "${allowed_destination_roots}")
        if(NOT recovered)
            message(FATAL_ERROR "text artifact rollback validation failed")
        endif()
        set(${result_var} FALSE PARENT_SCOPE)
        return()
    endif()
    set(${result_var} TRUE PARENT_SCOPE)
endfunction()
