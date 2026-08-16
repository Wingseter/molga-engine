cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED MIGRATOR OR NOT EXISTS "${MIGRATOR}")
    message(FATAL_ERROR "MIGRATOR must name the built molga_migrate executable")
endif()
if(NOT DEFINED WORK_ROOT OR WORK_ROOT STREQUAL "")
    message(FATAL_ERROR "WORK_ROOT is required")
endif()

set(project_root "${WORK_ROOT}/LegacyProject")
set(settings_dir "${project_root}/ProjectSettings")
set(input_file "${settings_dir}/input_actions.json")
file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${settings_dir}")
file(WRITE "${input_file}" [=[
[
  {
    "name": "Move",
    "isAxis": true,
    "bindings": [
      { "device": "Keyboard", "code": 68, "multiplier": 1.0 },
      { "device": "GamepadAxis", "code": 0, "multiplier": 1.0 }
    ]
  }
]
]=])
file(SHA256 "${input_file}" original_sha)

execute_process(
    COMMAND "${MIGRATOR}" input --project "${project_root}"
    RESULT_VARIABLE dry_result
    OUTPUT_VARIABLE dry_output
    ERROR_VARIABLE dry_error
)
if(NOT dry_result EQUAL 0)
    message(FATAL_ERROR "dry-run failed (${dry_result}): ${dry_error}")
endif()
if(NOT dry_output MATCHES "Dry run")
    message(FATAL_ERROR "dry-run did not report its non-mutating mode")
endif()
file(SHA256 "${input_file}" after_dry_sha)
if(NOT original_sha STREQUAL after_dry_sha)
    message(FATAL_ERROR "dry-run modified input_actions.json")
endif()

execute_process(
    COMMAND "${MIGRATOR}" input --project "${project_root}" --apply
    RESULT_VARIABLE apply_result
    OUTPUT_VARIABLE apply_output
    ERROR_VARIABLE apply_error
)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "apply failed (${apply_result}): ${apply_error}")
endif()

file(READ "${input_file}" migrated_json)
string(JSON schema_version ERROR_VARIABLE json_error GET "${migrated_json}" schemaVersion)
if(json_error OR NOT schema_version EQUAL 2)
    message(FATAL_ERROR "migrated document is not schema v2: ${json_error}")
endif()
string(JSON keyboard_control GET "${migrated_json}" actions 0 bindings 0 control)
string(JSON gamepad_control GET "${migrated_json}" actions 0 bindings 1 control)
if(NOT keyboard_control STREQUAL "D" OR NOT gamepad_control STREQUAL "LeftX")
    message(FATAL_ERROR "legacy bindings were not translated symbolically")
endif()

file(GLOB backups "${input_file}.bak.*")
list(LENGTH backups backup_count)
if(NOT backup_count EQUAL 1)
    message(FATAL_ERROR "expected one timestamped backup, found ${backup_count}")
endif()
list(GET backups 0 backup_file)
file(SHA256 "${backup_file}" backup_sha)
if(NOT backup_sha STREQUAL original_sha)
    message(FATAL_ERROR "backup does not match the original document")
endif()

execute_process(
    COMMAND "${MIGRATOR}" input --project "${project_root}" --apply
    RESULT_VARIABLE repeat_result
    OUTPUT_VARIABLE repeat_output
    ERROR_VARIABLE repeat_error
)
if(NOT repeat_result EQUAL 0 OR NOT repeat_output MATCHES "already use schema v2")
    message(FATAL_ERROR "repeat migration was not idempotent: ${repeat_error}")
endif()
