# A crash leaves a note in sculptforge.log (#16: a crash on Windows left nothing to report). Runs the app with
# --crash-test KIND for each kind of crash, then once normally: each crash's log names the crash and the last
# thing done, and the next start keeps it as sculptforge-previous.log and says the last session didn't close.
# cmake -DAPP=<sculptforge> -DDIR=<scratch folder> -P crash_log.cmake
file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
foreach(kind segv abort throw)
  execute_process(COMMAND "${APP}" --log-dir "${DIR}" --crash-test ${kind} RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
  if(rc EQUAL 0)
    message(FATAL_ERROR "--crash-test ${kind} exited normally")
  endif()
  file(READ "${DIR}/sculptforge.log" log)
  if(NOT log MATCHES "CRASH: SculptForge [^\n]* crashed" OR NOT log MATCHES "CRASH: last thing done: crash_test ${kind}")
    message(FATAL_ERROR "no crash note for ${kind}:\n${log}")
  endif()
  if(kind STREQUAL "throw" AND NOT log MATCHES "uncaught exception: crash test")
    message(FATAL_ERROR "the exception's text is missing:\n${log}")
  endif()
endforeach()
# The next start (another crash test: it needs no window) keeps the crashed session's log and says so.
execute_process(COMMAND "${APP}" --log-dir "${DIR}" --crash-test abort OUTPUT_QUIET ERROR_QUIET)
file(READ "${DIR}/sculptforge.log" log)
file(READ "${DIR}/sculptforge-previous.log" previous)
if(NOT log MATCHES "The last session did not close normally" OR NOT previous MATCHES "crash_test throw")
  message(FATAL_ERROR "the last session's log wasn't kept:\n${log}\n---\n${previous}")
endif()
message(STATUS "crash notes written")
