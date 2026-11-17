# SculptForge - the CPython the app's Python scripting ships with (SCULPTFORGE_PYTHON_RUNTIME, CMakeLists.txt).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RUNTIME is an unpacked python-build-standalone "install_only" CPython. It goes beside the host library as
# python/ (the host's home, src/app/Python.cpp; on macOS in the bundle's Resources, as codesign wants only code in
# Frameworks), trimmed to what scripts use: no test suite, Tk, IDLE, turtle demo,
# ensurepip, help() topics, REPL editor, venv, headers, static libraries or command-line tools. On Windows the DLLs the host links go next to the exe.
function(sf_install_python_runtime RUNTIME DEST)
  set(drop
    PATTERN "__pycache__" EXCLUDE
    PATTERN "test" EXCLUDE PATTERN "tests" EXCLUDE PATTERN "idle_test" EXCLUDE
    PATTERN "idlelib" EXCLUDE PATTERN "tkinter" EXCLUDE PATTERN "turtledemo" EXCLUDE PATTERN "turtle.py" EXCLUDE
    PATTERN "ensurepip" EXCLUDE PATTERN "lib2to3" EXCLUDE
    # help()'s topic pages, the interactive REPL's line editor (the console is code.InteractiveConsole) and venv
    PATTERN "pydoc_data" EXCLUDE PATTERN "_pyrepl" EXCLUDE PATTERN "venv" EXCLUDE
    PATTERN "_tkinter*" EXCLUDE PATTERN "tcl*" EXCLUDE PATTERN "tk*" EXCLUDE PATTERN "itcl*" EXCLUDE PATTERN "thread2*" EXCLUDE
    PATTERN "libtcl*" EXCLUDE PATTERN "libtk*" EXCLUDE PATTERN "_test*" EXCLUDE PATTERN "_ctypes_test*" EXCLUDE
    PATTERN "xxlimited*" EXCLUDE PATTERN "xxsubtype*" EXCLUDE
    # the stable-ABI shim (python3.dll's counterpart): nothing loads it, and its $ORIGIN/../lib need trips dpkg-shlibdeps
    PATTERN "libpython3.so" EXCLUDE
    PATTERN "*.a" EXCLUDE PATTERN "*.lib" EXCLUDE PATTERN "pkgconfig" EXCLUDE PATTERN "config-3*" EXCLUDE
    PATTERN "site-packages" EXCLUDE)
  if(WIN32)
    file(GLOB dlls "${RUNTIME}/python3*.dll")
    install(FILES ${dlls} DESTINATION ${DEST})
    install(DIRECTORY "${RUNTIME}/Lib" "${RUNTIME}/DLLs" DESTINATION ${DEST}/python ${drop})
    install(FILES "${RUNTIME}/LICENSE.txt" DESTINATION ${DEST}/python OPTIONAL)
  elseif(APPLE)
    # The build folder's bundle has the runtime staged in it as a link to the unpacked runtime (CMakeLists.txt), and
    # installing the bundle copies that link: drop it, so the package holds the trimmed runtime itself.
    install(CODE "file(REMOVE_RECURSE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/SculptForge.app/Contents/Resources/python\")")
    install(DIRECTORY "${RUNTIME}/lib" DESTINATION SculptForge.app/Contents/Resources/python USE_SOURCE_PERMISSIONS ${drop})
  else()
    install(DIRECTORY "${RUNTIME}/lib" DESTINATION ${DEST}/python USE_SOURCE_PERMISSIONS ${drop})
  endif()
endfunction()
