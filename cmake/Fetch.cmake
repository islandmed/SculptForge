# Every third-party archive the build fetches, pinned by SHA-256 (the Flatpak manifest pins the same ones), and the
# one way it fetches them: sf_fetch_declare(<name> [FetchContent_Declare options]).
# The archive is downloaded once into SF_DEPS_CACHE (environment, relative to the source tree; default
# <build>/_deps/downloads), retried with backoff (CMake's own download gives up on the first HTTP error: 0.11.1's
# packages died on GitHub 504s), checked against its hash, and FetchContent then extracts the local copy. CI caches
# that folder keyed by this file's hash (actions/cache, key deps-<hash of cmake/Fetch.cmake>).
include(FetchContent)

set(SF_PIN_glm https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
    9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)
set(SF_PIN_SDL3 https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-3.2.30.tar.gz
    4c3b09330d866dc52eb65b66259a6684ad387252ca8c57901b3a2b534eb42e3d)
# Linux: SDL 3.4 (cmake/Dependencies.cmake), for Wayland pens: one pen across proximity changes, the eraser end,
# the tablet's own cursor. (Windows and macOS stay on 3.2.30 until they are checked on real tablets.)
set(SF_PIN_SDL3_linux https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
set(SF_PIN_imgui https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz
    3872a5f90df78fced023c1945f4466b654fd74573370b77b17742149763a7a7c)
set(SF_PIN_mbedtls https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6)
set(SF_PIN_libdatachannel https://github.com/paullouisageneau/libdatachannel/archive/refs/tags/v0.24.6.tar.gz
    22a66aebebd8dae561d8e5181da96f2fcc203e067c5b2186c07b39af913464c0)
set(SF_PIN_ldc_usrsctp https://github.com/paullouisageneau/usrsctp/archive/fec583d54493f879d2ae44a743423bf8a04371ab.tar.gz
    e5c114afe73c9a0ec419fab5f5b3f63f3ce57b09b90d06ea85e63dca8aed3e7c)
set(SF_PIN_ldc_libjuice https://github.com/paullouisageneau/libjuice/archive/b89c792e3612faf2f12cf35bcc56857313a06be3.tar.gz
    713b22174e8cb2e06538fa9bac2650bda21b83c86f5128813ab4dcf8f5bea9ed)
set(SF_PIN_ldc_plog https://github.com/SergiusTheBest/plog/archive/94899e0b926ac1b0f4750bfbd495167b4a6ae9ef.tar.gz
    92a08bce559b5f28aa88d3fd9071567414b9f43a83fe2a05a6dd14f1da536072)
set(SF_PIN_pybind11 https://github.com/pybind/pybind11/archive/refs/tags/v3.0.4.tar.gz
    74b6a2c2b4573a400cafb6ecbf60c98df300cd3d0041296b913d02b2cbbb2676)

function(sf_fetch_declare name)
  if(NOT DEFINED SF_PIN_${name})
    message(FATAL_ERROR "sf_fetch_declare(${name}): no SF_PIN_${name} (URL and SHA-256) in cmake/Fetch.cmake")
  endif()
  list(GET SF_PIN_${name} 0 url)
  list(GET SF_PIN_${name} 1 sha)
  string(TOUPPER "${name}" upper)
  if(FETCHCONTENT_SOURCE_DIR_${upper} OR FETCHCONTENT_FULLY_DISCONNECTED)
    # A packager's own sources (Flatpak, AUR: offline builds): FetchContent uses those, nothing is downloaded.
    FetchContent_Declare(${name} URL "${url}" URL_HASH SHA256=${sha} ${ARGN})
    return()
  endif()
  set(dir "$ENV{SF_DEPS_CACHE}")
  if(dir STREQUAL "")
    set(dir "${CMAKE_BINARY_DIR}/_deps/downloads")
  endif()
  get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}") # CI: SF_DEPS_CACHE=.deps
  get_filename_component(base "${url}" NAME)
  set(file "${dir}/${sha}-${base}") # the hash in the name: a changed pin never reuses an old file
  set(have "")
  if(EXISTS "${file}")
    file(SHA256 "${file}" have)
  endif()
  if(NOT have STREQUAL sha)
    file(MAKE_DIRECTORY "${dir}")
    set(why "")
    foreach(wait 0 5 15 30 60)
      if(wait)
        message(STATUS "${name}: ${why}; retrying in ${wait} s")
        execute_process(COMMAND ${CMAKE_COMMAND} -E sleep ${wait})
      endif()
      file(DOWNLOAD "${url}" "${file}.part" STATUS st INACTIVITY_TIMEOUT 60 TIMEOUT 600)
      list(GET st 0 code)
      if(code EQUAL 0)
        file(SHA256 "${file}.part" have)
        if(have STREQUAL sha)
          file(RENAME "${file}.part" "${file}")
          break()
        endif()
        set(why "SHA-256 ${have}, expected ${sha}")
        break() # a different file, not a network hiccup: no retry
      else()
        list(GET st 1 msg)
        set(why "download failed (${code}: ${msg})")
      endif()
    endforeach()
    if(NOT have STREQUAL sha)
      file(REMOVE "${file}.part")
      message(FATAL_ERROR "${name}: ${url}: ${why}")
    endif()
  endif()
  FetchContent_Declare(${name} URL "${file}" URL_HASH SHA256=${sha} ${ARGN})
endfunction()
