foreach(required_variable IN ITEMS
    OPENWOW_IOS_DEVICE
    OPENWOW_IOS_BUNDLE_IDENTIFIER
    OPENWOW_LOCAL_CONTENT_ROOT
    OPENWOW_PROJECT_SOURCE_DIR
    OPENWOW_IOS_TEXTURE_CACHE_LOCALE
    OPENWOW_IOS_TEXTURE_CACHE_TOOL
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

if(NOT EXISTS "${OPENWOW_IOS_TEXTURE_CACHE_TOOL}")
  message(FATAL_ERROR
    "The native iOS texture-cache tool is missing at "
    "${OPENWOW_IOS_TEXTURE_CACHE_TOOL}. Configure the native release preset "
    "and build target openwow-ios-texture-cache before syncing Data.")
endif()

message(STATUS "Preparing the incremental iOS ASTC texture cache")
execute_process(
  COMMAND "${OPENWOW_IOS_TEXTURE_CACHE_TOOL}"
    --game-root "${OPENWOW_LOCAL_CONTENT_ROOT}"
    --output "${data_source}/OpenWoWDerived/iOS/Textures"
    --locale "${OPENWOW_IOS_TEXTURE_CACHE_LOCALE}"
    --enhanced-assets-root "${OPENWOW_PROJECT_SOURCE_DIR}/assets/overrides"
  RESULT_VARIABLE texture_cache_result
  COMMAND_ECHO STDOUT)
if(NOT texture_cache_result EQUAL 0)
  message(FATAL_ERROR
    "iOS ASTC texture cache preparation failed with exit code "
    "${texture_cache_result}; Data was not synced.")
endif()

set(device_game_root "Library/Application Support/OpenWoW/GameRoot")
set(device_data_root "${device_game_root}/Data")
set(device_ready_marker
  "${device_game_root}/.openwow-ios-data-ready")

set(texture_cache_root
  "${data_source}/OpenWoWDerived/iOS/Textures")
set(texture_cache_manifest
  "${data_source}/OpenWoWDerived/iOS/texture-cache-v1.manifest")
if(NOT IS_DIRECTORY "${texture_cache_root}" OR
   NOT EXISTS "${texture_cache_manifest}")
  message(FATAL_ERROR
    "The iOS ASTC texture cache completed without its texture directory or "
    "manifest; Data was not synced.")
endif()

file(READ "${texture_cache_manifest}" texture_cache_identity)
string(STRIP "${texture_cache_identity}" texture_cache_identity)

# Track retail Data separately from the derived cache. A cache generator or
# project-override change must not enqueue the already installed 20+ GiB retail
# archives again.
file(GLOB data_entries LIST_DIRECTORIES true "${data_source}/*")
list(SORT data_entries)
set(retail_data_signature_input "")
foreach(data_entry IN LISTS data_entries)
  get_filename_component(data_entry_name "${data_entry}" NAME)
  if(data_entry_name STREQUAL "OpenWoWDerived" OR
     data_entry_name STREQUAL ".DS_Store")
    continue()
  endif()
  if(IS_DIRECTORY "${data_entry}")
    file(GLOB_RECURSE retail_entry_files LIST_DIRECTORIES false
      "${data_entry}/*")
    list(SORT retail_entry_files)
  else()
    set(retail_entry_files "${data_entry}")
  endif()
  foreach(retail_entry_file IN LISTS retail_entry_files)
    file(RELATIVE_PATH retail_entry_relative
      "${data_source}" "${retail_entry_file}")
    file(SIZE "${retail_entry_file}" retail_entry_size)
    file(TIMESTAMP "${retail_entry_file}" retail_entry_timestamp UTC)
    string(APPEND retail_data_signature_input
      "${retail_entry_relative}\n${retail_entry_size}\n"
      "${retail_entry_timestamp}\n")
  endforeach()
endforeach()
string(SHA256 retail_data_identity "${retail_data_signature_input}")

file(WRITE "${OPENWOW_IOS_SYNC_MARKER}"
  "OpenWoW build-12340 Data sync completed.\n"
  "retail-data=${retail_data_identity}\n"
  "texture-cache=${texture_cache_identity}\n")

# A successful marker is a cheap whole-tree incremental check. It also keeps a
# failed transfer resumable: the old marker remains in place until every batch
# below has completed.
set(remote_marker_copy "${OPENWOW_IOS_SYNC_MARKER}.device")
set(retail_data_is_current false)
file(REMOVE "${remote_marker_copy}")
execute_process(
  COMMAND "${OPENWOW_XCRUN_EXECUTABLE}" devicectl device copy from
    --device "${OPENWOW_IOS_DEVICE}"
    --source "${device_ready_marker}"
    --destination "${remote_marker_copy}"
    --domain-type appDataContainer
    --domain-identifier "${OPENWOW_IOS_BUNDLE_IDENTIFIER}"
    --timeout 20
  RESULT_VARIABLE marker_probe_result
  OUTPUT_VARIABLE marker_probe_output
  ERROR_VARIABLE marker_probe_error)
if(marker_probe_result EQUAL 0 AND EXISTS "${remote_marker_copy}")
  file(READ "${remote_marker_copy}" remote_marker_contents)
  file(READ "${OPENWOW_IOS_SYNC_MARKER}" expected_marker_contents)
  if(remote_marker_contents STREQUAL expected_marker_contents)
    file(REMOVE "${remote_marker_copy}")
    message(STATUS
      "iOS Data is already current on ${OPENWOW_IOS_DEVICE}; nothing to copy.")
    return()
  endif()

  string(FIND "${remote_marker_contents}"
    "retail-data=${retail_data_identity}\n" matching_retail_identity)
  if(NOT matching_retail_identity EQUAL -1)
    set(retail_data_is_current true)
  else()
    string(FIND "${remote_marker_contents}"
      "retail-data=" remote_retail_identity_field)
    string(FIND "${remote_marker_contents}"
      "OpenWoW build-12340 Data sync completed.\n" legacy_marker_prefix)
    if(remote_retail_identity_field EQUAL -1 AND legacy_marker_prefix EQUAL 0)
      # Markers written by the original whole-Data sync predate the separate
      # retail identity. They were published only after that copy completed.
      set(retail_data_is_current true)
      message(STATUS
        "The device has a completed legacy Data sync; only the derived iOS "
        "cache will be transferred.")
    endif()
  endif()
elseif(NOT marker_probe_result EQUAL 0)
  string(TOLOWER
    "${marker_probe_output}\n${marker_probe_error}" marker_probe_diagnostic)
  if(marker_probe_diagnostic MATCHES "timed out|timeout|network socket|device was not found")
    message(FATAL_ERROR
      "CoreDevice cannot currently reach ${OPENWOW_IOS_DEVICE}. Keep the "
      "iPhone unlocked, reconnect it over USB, wait for Xcode to finish "
      "preparing the device, and rerun this target. The previous Data remains "
      "intact and the next run will resume incrementally.\n"
      "${marker_probe_output}${marker_probe_error}")
  endif()
endif()
file(REMOVE "${remote_marker_copy}")

function(openwow_copy_to_ios_device)
  set(one_value_args DESTINATION DESCRIPTION)
  set(multi_value_args SOURCES)
  cmake_parse_arguments(COPY "" "${one_value_args}" "${multi_value_args}"
    ${ARGN})
  if(NOT COPY_SOURCES OR NOT COPY_DESTINATION)
    message(FATAL_ERROR
      "openwow_copy_to_ios_device requires SOURCES and DESTINATION")
  endif()

  set(copy_command
    "${OPENWOW_XCRUN_EXECUTABLE}" devicectl device copy to
    --device "${OPENWOW_IOS_DEVICE}")
  foreach(copy_source IN LISTS COPY_SOURCES)
    list(APPEND copy_command --source "${copy_source}")
  endforeach()
  list(APPEND copy_command
    --destination "${COPY_DESTINATION}"
    --domain-type appDataContainer
    --domain-identifier "${OPENWOW_IOS_BUNDLE_IDENTIFIER}"
    --timeout 1800)

  set(copy_attempt 1)
  while(copy_attempt LESS_EQUAL 3)
    message(STATUS
      "${COPY_DESCRIPTION} (attempt ${copy_attempt}/3)")
    execute_process(
      COMMAND ${copy_command}
      RESULT_VARIABLE copy_result)
    if(copy_result EQUAL 0)
      return()
    endif()
    if(copy_attempt LESS 3)
      message(WARNING
        "CoreDevice transfer failed with exit code ${copy_result}; retrying "
        "the same incremental batch. Already copied files are not duplicated.")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
    endif()
    math(EXPR copy_attempt "${copy_attempt} + 1")
  endwhile()

  message(FATAL_ERROR
    "${COPY_DESCRIPTION} failed after 3 attempts. This is a CoreDevice "
    "transport failure, not a signing error. Keep the iPhone unlocked, "
    "reconnect it over USB, and rerun this target; completed batches will be "
    "skipped and no duplicate Data will be created.")
endfunction()

message(STATUS
  "Incrementally syncing ${data_source} to ${OPENWOW_IOS_DEVICE}:"
  "${device_data_root}")

set(sync_empty_directory "${OPENWOW_IOS_SYNC_MARKER}.empty-directory")
file(REMOVE_RECURSE "${sync_empty_directory}")
file(MAKE_DIRECTORY "${sync_empty_directory}")
openwow_copy_to_ios_device(
  SOURCES "${sync_empty_directory}"
  DESTINATION "${device_data_root}"
  DESCRIPTION "Ensuring the iOS Data directory exists")
openwow_copy_to_ios_device(
  SOURCES "${sync_empty_directory}"
  DESTINATION "${device_data_root}/OpenWoWDerived/iOS/Textures"
  DESCRIPTION "Ensuring the iOS ASTC cache directory exists")

if(retail_data_is_current)
  message(STATUS
    "Retail Data is already present on ${OPENWOW_IOS_DEVICE}; skipping all "
    "original MPQ and locale files.")
else()
  # A fresh install has no readiness marker. Keep its initial retail transfer
  # in independently retryable transactions rather than one 20+ GiB request.
  foreach(data_entry IN LISTS data_entries)
    get_filename_component(data_entry_name "${data_entry}" NAME)
    if(data_entry_name STREQUAL "OpenWoWDerived" OR
       data_entry_name STREQUAL ".DS_Store")
      continue()
    endif()
    openwow_copy_to_ios_device(
      SOURCES "${data_entry}"
      DESTINATION "${device_data_root}/${data_entry_name}"
      DESCRIPTION "Syncing Data/${data_entry_name}")
  endforeach()
endif()

# CoreDevice is prone to socket timeouts when one request contains the entire
# 106k-file cache. Use bounded batches; reruns skip files already transferred.
file(GLOB texture_cache_files LIST_DIRECTORIES false
  "${texture_cache_root}/*.owtx")
list(SORT texture_cache_files)
list(LENGTH texture_cache_files texture_cache_file_count)
if(texture_cache_file_count EQUAL 0)
  message(FATAL_ERROR "The iOS ASTC texture cache contains no .owtx files")
endif()

set(texture_batch_size 512)
set(texture_batch_offset 0)
set(texture_batch_number 0)
math(EXPR texture_batch_count
  "(${texture_cache_file_count} + ${texture_batch_size} - 1) / ${texture_batch_size}")
while(texture_batch_offset LESS texture_cache_file_count)
  math(EXPR texture_batch_remaining
    "${texture_cache_file_count} - ${texture_batch_offset}")
  if(texture_batch_remaining GREATER texture_batch_size)
    set(texture_batch_length "${texture_batch_size}")
  else()
    set(texture_batch_length "${texture_batch_remaining}")
  endif()
  list(SUBLIST texture_cache_files "${texture_batch_offset}"
    "${texture_batch_length}" texture_batch)
  math(EXPR texture_batch_number "${texture_batch_number} + 1")
  openwow_copy_to_ios_device(
    SOURCES ${texture_batch}
    DESTINATION "${device_data_root}/OpenWoWDerived/iOS/Textures"
    DESCRIPTION
      "Syncing iOS ASTC batch ${texture_batch_number}/${texture_batch_count}")
  math(EXPR texture_batch_offset
    "${texture_batch_offset} + ${texture_batch_length}")
endwhile()

openwow_copy_to_ios_device(
  SOURCES "${texture_cache_manifest}"
  DESTINATION
    "${device_data_root}/OpenWoWDerived/iOS/texture-cache-v1.manifest"
  DESCRIPTION "Syncing the iOS ASTC cache manifest")

openwow_copy_to_ios_device(
  SOURCES "${OPENWOW_IOS_SYNC_MARKER}"
  DESTINATION "${device_ready_marker}"
  DESCRIPTION "Publishing the iOS Data readiness marker")

message(STATUS
  "iOS Data is ready. Later syncs with the same source manifest perform no "
  "Data transfer.")
