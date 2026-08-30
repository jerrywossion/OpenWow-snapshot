foreach(required_variable IN ITEMS
    OPENWOW_IOS_DEVICE
    OPENWOW_IOS_BUNDLE_IDENTIFIER
    OPENWOW_LOCAL_CONTENT_ROOT
    OPENWOW_XCRUN_EXECUTABLE
    OPENWOW_IOS_SYNC_MARKER)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR
      "${required_variable} is required for incremental iOS Data sync")
  endif()
endforeach()

set(data_source "${OPENWOW_LOCAL_CONTENT_ROOT}/Data")
if(NOT IS_DIRECTORY "${data_source}")
  message(FATAL_ERROR
    "No Data directory exists under OPENWOW_LOCAL_CONTENT_ROOT="
    "${OPENWOW_LOCAL_CONTENT_ROOT}")
endif()

set(device_game_root "Library/Application Support/OpenWoW/GameRoot")
set(device_data_root "${device_game_root}/Data")
message(STATUS
  "Incrementally syncing ${data_source} to ${OPENWOW_IOS_DEVICE}:"
  "${device_data_root}")
execute_process(
  COMMAND "${OPENWOW_XCRUN_EXECUTABLE}" devicectl device copy to
    --device "${OPENWOW_IOS_DEVICE}"
    --source "${data_source}"
    --destination "${device_data_root}"
    --domain-type appDataContainer
    --domain-identifier "${OPENWOW_IOS_BUNDLE_IDENTIFIER}"
  RESULT_VARIABLE data_copy_result
  COMMAND_ECHO STDOUT)
if(NOT data_copy_result EQUAL 0)
  message(FATAL_ERROR
    "iOS Data sync failed with exit code ${data_copy_result}. The signed "
    "${OPENWOW_IOS_BUNDLE_IDENTIFIER} app must be installed before syncing.")
endif()

file(WRITE "${OPENWOW_IOS_SYNC_MARKER}"
  "OpenWoW build-12340 Data sync completed.\n")
set(device_ready_marker
  "${device_game_root}/.openwow-ios-data-ready")
execute_process(
  COMMAND "${OPENWOW_XCRUN_EXECUTABLE}" devicectl device copy to
    --device "${OPENWOW_IOS_DEVICE}"
    --source "${OPENWOW_IOS_SYNC_MARKER}"
    --destination "${device_ready_marker}"
    --domain-type appDataContainer
    --domain-identifier "${OPENWOW_IOS_BUNDLE_IDENTIFIER}"
  RESULT_VARIABLE marker_copy_result
  COMMAND_ECHO STDOUT)
if(NOT marker_copy_result EQUAL 0)
  message(FATAL_ERROR
    "Data files were transferred, but the iOS readiness marker failed to "
    "copy with exit code ${marker_copy_result}; rerun this target.")
endif()

message(STATUS
  "iOS Data is ready. Later syncs skip files that have not changed.")
