# A portable copy (core/DataDir.h): portable.txt beside the program makes its data folder <program folder>/data.
# Copies the real program (and the libraries beside it) to a folder with spaces, then
#   1. runs it with the marker: data/ is created and holds the saved value and the crash log, and every per-user
#      variable (HOME, XDG_*, APPDATA ...) points at an empty folder that must stay empty;
#   2. moves the whole folder and runs it again: the value is kept;
#   3. removes the marker: the data is per-user again (under the machine folder), data/ is left alone.
# cmake -DAPP=<sculptforge> -DDIR=<scratch folder> -P portable_data.cmake
file(REMOVE_RECURSE "${DIR}")
set(PROG "${DIR}/Program Files test/SculptForge")
set(MACHINE "${DIR}/machine")
file(MAKE_DIRECTORY "${PROG}" "${MACHINE}")
get_filename_component(APP_DIR "${APP}" DIRECTORY)
get_filename_component(APP_NAME "${APP}" NAME)
file(COPY "${APP}" DESTINATION "${PROG}")
file(GLOB libs "${APP_DIR}/*.dll" "${APP_DIR}/lib*.so" "${APP_DIR}/lib*.so.*" "${APP_DIR}/*.dylib")
if(libs)
  file(COPY ${libs} DESTINATION "${PROG}")
endif()
file(WRITE "${PROG}/portable.txt" "portable\n")

function(run_app prog out_var)
  execute_process(COMMAND ${CMAKE_COMMAND} -E env
      "HOME=${MACHINE}" "XDG_DATA_HOME=${MACHINE}" "XDG_CONFIG_HOME=${MACHINE}" "XDG_CACHE_HOME=${MACHINE}"
      "APPDATA=${MACHINE}" "LOCALAPPDATA=${MACHINE}" "USERPROFILE=${MACHINE}"
      "SF_ISOLATE=0" "SDL_VIDEO_DRIVER=dummy" "SDL_AUDIO_DRIVER=dummy"
      "${prog}/${APP_NAME}" ${ARGN}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  set(${out_var} "${out}" PARENT_SCOPE)
  set(${out_var}_rc "${rc}" PARENT_SCOPE)
  set(${out_var}_err "${err}" PARENT_SCOPE)
endfunction()

function(machine_files var)
  file(GLOB_RECURSE found "${MACHINE}/*")
  set(${var} "${found}" PARENT_SCOPE)
endfunction()

# 1. with the marker
run_app("${PROG}" r1 --portable-selftest "bright")
if(NOT r1_rc EQUAL 0 OR NOT r1 MATCHES "portable=1" OR NOT r1 MATCHES "value=bright")
  message(FATAL_ERROR "portable run: rc=${r1_rc}\n${r1}\n${r1_err}")
endif()
if(NOT EXISTS "${PROG}/data/portable-selftest.txt")
  message(FATAL_ERROR "the value was not saved under data/:\n${r1}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E env
    "HOME=${MACHINE}" "XDG_DATA_HOME=${MACHINE}" "XDG_CONFIG_HOME=${MACHINE}" "APPDATA=${MACHINE}" "LOCALAPPDATA=${MACHINE}"
    "USERPROFILE=${MACHINE}" "SF_ISOLATE=0" "${PROG}/${APP_NAME}" --crash-test throw
  RESULT_VARIABLE crash OUTPUT_QUIET ERROR_QUIET)
if(crash EQUAL 0)
  message(FATAL_ERROR "--crash-test throw exited normally")
endif()
if(NOT EXISTS "${PROG}/data/sculptforge.log")
  message(FATAL_ERROR "the crash log is not under data/")
endif()
machine_files(leaked)
if(leaked)
  message(FATAL_ERROR "a portable run wrote outside its data folder:\n${leaked}")
endif()

# 2. the folder moved
set(MOVED "${DIR}/Another place/My Stick")
file(MAKE_DIRECTORY "${DIR}/Another place")
file(RENAME "${PROG}" "${MOVED}")
run_app("${MOVED}" r2 --portable-selftest)
if(NOT r2_rc EQUAL 0 OR NOT r2 MATCHES "portable=1" OR NOT r2 MATCHES "value=bright")
  message(FATAL_ERROR "the moved folder lost its setting: rc=${r2_rc}\n${r2}\n${r2_err}")
endif()
machine_files(leaked)
if(leaked)
  message(FATAL_ERROR "the moved portable run wrote outside its data folder:\n${leaked}")
endif()

# 3. without the marker: per-user again
file(REMOVE "${MOVED}/portable.txt")
file(REMOVE_RECURSE "${MOVED}/data")
run_app("${MOVED}" r3 --portable-selftest "plain")
if(NOT r3_rc EQUAL 0 OR NOT r3 MATCHES "portable=0" OR NOT r3 MATCHES "value=plain")
  message(FATAL_ERROR "no marker: rc=${r3_rc}\n${r3}\n${r3_err}")
endif()
if(EXISTS "${MOVED}/data")
  message(FATAL_ERROR "a copy without the marker made a data/ folder")
endif()
# macOS names the user's folder by the account (not HOME), so the sentinel can't see where a per-user run wrote
machine_files(there)
if(NOT there AND NOT APPLE)
  message(FATAL_ERROR "no marker: nothing was saved under the machine folder")
endif()
message(STATUS "portable data folder behaves")
