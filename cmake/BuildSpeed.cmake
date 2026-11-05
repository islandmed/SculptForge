# Edit-build loop speed (docs/build-speed.md): mold for links, precompiled standard headers. Both have an off switch;
# release packaging builds pass -DSCULPTFORGE_MOLD=OFF (the shipped binaries keep the system linker).
# included before cmake/Dependencies.cmake (the ccache settings below apply to the dependencies too).

# --- mold (MIT licence, github.com/rui314/mold): links in a fraction of ld.bfd's time. On when installed. ---
if(NOT MSVC AND NOT APPLE)
  find_program(SF_MOLD_PROGRAM mold)
endif()
if(SF_MOLD_PROGRAM)
  set(sf_mold_default ON)
else()
  set(sf_mold_default OFF)
endif()
option(SCULPTFORGE_MOLD "Link with mold when it is installed (faster links; release packaging turns it off)" ${sf_mold_default})
if(SCULPTFORGE_MOLD)
  include(CheckLinkerFlag)
  check_linker_flag(CXX -fuse-ld=mold SF_LINKER_MOLD) # GCC 12.1+ and Clang
  if(SF_LINKER_MOLD)
    add_link_options(-fuse-ld=mold)
    message(STATUS "Linker: mold (${SF_MOLD_PROGRAM}); -DSCULPTFORGE_MOLD=OFF for the default linker")
  else()
    message(STATUS "Linker: mold requested but ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} can't use it")
  endif()
endif()

# --- Debug info in separate .dwo files for Debug and RelWithDebInfo builds on Linux: the linker skips it. ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
  option(SCULPTFORGE_SPLIT_DWARF "Debug info in .dwo files beside the objects (faster links)" ON)
  if(SCULPTFORGE_SPLIT_DWARF)
    add_compile_options(-gsplit-dwarf)
  endif()
endif()

# --- Precompiled headers: the standard library and glm, parsed once per target instead of once per file. ---
# Only headers that never change with our sources (an edit to src/ never rebuilds a PCH). On by default where the
# edit loop runs (Linux, GCC or Clang); elsewhere opt-in: CI on Windows caches with ccache standing in for cl.exe,
# which can't cache /Yu compiles, and macOS release builds are universal (one PCH can't serve two architectures).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(sf_pch_default ON)
else()
  set(sf_pch_default OFF)
endif()
option(SCULPTFORGE_PCH "Precompile the standard library and glm headers for each target" ${sf_pch_default})
# ccache caches compiles that use a PCH (ours, and SDL's own) only with these sloppiness settings; ccache 4.8+ takes
# them on its command line. Set before the dependencies are configured, so their PCH compiles are cached too.
foreach(lang C CXX)
  if(CMAKE_${lang}_COMPILER_LAUNCHER MATCHES "ccache" AND NOT CMAKE_${lang}_COMPILER_LAUNCHER MATCHES "sloppiness")
    list(GET CMAKE_${lang}_COMPILER_LAUNCHER 0 sf_ccache)
    execute_process(COMMAND ${sf_ccache} --version OUTPUT_VARIABLE sf_ccache_version ERROR_QUIET)
    string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _ "${sf_ccache_version}")
    if(CMAKE_MATCH_1 VERSION_GREATER_EQUAL 4.8)
      set(CMAKE_${lang}_COMPILER_LAUNCHER ${sf_ccache} sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime)
    elseif(lang STREQUAL "CXX" AND SCULPTFORGE_PCH)
      message(STATUS "ccache older than 4.8: precompiled headers off (ccache would not cache those compiles)")
      set(SCULPTFORGE_PCH OFF)
    endif()
  endif()
endforeach()
if(SCULPTFORGE_PCH)
  message(STATUS "Precompiled headers: on; -DSCULPTFORGE_PCH=OFF to build without")
endif()

set(SF_PCH_HEADERS
  <algorithm> <array> <atomic> <chrono> <cmath> <cstddef> <cstdint> <cstdio> <cstdlib> <cstring> <filesystem>
  <format> <fstream> <functional> <map> <memory> <mutex> <optional> <set> <span> <sstream> <string> <string_view>
  <thread> <tuple> <unordered_map> <unordered_set> <utility> <variant> <vector> <glm/glm.hpp>)

# sculptforge_pch(target [extra headers...]): precompile SF_PCH_HEADERS (+ extras) for the target's C++ files.
function(sculptforge_pch target)
  if(NOT SCULPTFORGE_PCH)
    return()
  endif()
  set(headers)
  foreach(h ${SF_PCH_HEADERS} ${ARGN})
    string(REGEX REPLACE ">$" "$<ANGLE-R>" h "${h}") # a bare > would end the generator expression
    list(APPEND headers "$<$<COMPILE_LANGUAGE:CXX>:${h}>")
  endforeach()
  target_precompile_headers(${target} PRIVATE ${headers})
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang") # no timestamp in the PCH, or ccache can never reuse a compile using it
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-Xclang -fno-pch-timestamp>")
  endif()
endfunction()
