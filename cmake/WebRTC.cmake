# WebRTC data channels for the phone link's live face feed (src/link/PhoneRtc.cpp), optional:
# -DSCULPTFORGE_WEBRTC=OFF builds without it (the phone link then offers capture and AR only).
# libdatachannel (MPL-2.0) with libjuice (MPL-2.0), usrsctp (BSD-3-Clause), plog (MIT) and Mbed TLS (Apache-2.0),
# all static and fetched at configure time like the other dependencies. No media (SRTP) and no WebSocket parts.
include(${CMAKE_CURRENT_LIST_DIR}/Fetch.cmake) # include(FetchContent), the pins, sf_fetch_declare

sf_fetch_declare(mbedtls DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
sf_fetch_declare(libdatachannel DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# libdatachannel's submodules, at the commits its v0.24.6 tag pins (GitHub archives leave submodules out).
sf_fetch_declare(ldc_usrsctp DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
sf_fetch_declare(ldc_libjuice DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
sf_fetch_declare(ldc_plog DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

foreach(dep mbedtls libdatachannel ldc_usrsctp ldc_libjuice ldc_plog)
  FetchContent_GetProperties(${dep})
  if(NOT ${dep}_POPULATED)
    if(POLICY CMP0169)
      cmake_policy(PUSH)
      cmake_policy(SET CMP0169 OLD) # FetchContent_Populate: we add the projects ourselves, EXCLUDE_FROM_ALL
    endif()
    FetchContent_Populate(${dep})
    if(POLICY CMP0169)
      cmake_policy(POP)
    endif()
  endif()
endforeach()

# The dependencies' own install rules (headers, static libraries, CMake packages) stay out of our packages:
# install() does nothing while SF_SKIP_INSTALL is set.
function(install)
  if(NOT SF_SKIP_INSTALL)
    _install(${ARGV})
  endif()
endfunction()
set(SF_SKIP_INSTALL ON)

# Mbed TLS: the libraries only, static.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_USER_CONFIG_FILE ${CMAKE_CURRENT_LIST_DIR}/mbedtls_user_config.h CACHE FILEPATH "" FORCE)
add_subdirectory(${mbedtls_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_webrtc/mbedtls EXCLUDE_FROM_ALL) # (sources may be shared between build trees)
add_library(sf_mbedtls INTERFACE)
target_link_libraries(sf_mbedtls INTERFACE mbedtls mbedx509 mbedcrypto)
add_library(MbedTLS::MbedTLS ALIAS sf_mbedtls)

# libdatachannel looks for its submodules under deps/.
foreach(sub usrsctp libjuice plog)
  if(NOT EXISTS ${libdatachannel_SOURCE_DIR}/deps/${sub}/CMakeLists.txt)
    file(REMOVE_RECURSE ${libdatachannel_SOURCE_DIR}/deps/${sub})
    file(COPY ${ldc_${sub}_SOURCE_DIR}/ DESTINATION ${libdatachannel_SOURCE_DIR}/deps/${sub})
  endif()
endforeach()
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_DEPS_LIBS OFF CACHE BOOL "" FORCE)
set(_sf_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${libdatachannel_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_webrtc/libdatachannel EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_sf_shared})
set(SF_SKIP_INSTALL OFF)
# Third-party code: compile quietly.
foreach(t datachannel-static juice-static usrsctp mbedtls mbedx509 mbedcrypto)
  if(TARGET ${t})
    if(MSVC)
      target_compile_options(${t} PRIVATE /w)
    else()
      target_compile_options(${t} PRIVATE -w)
    endif()
  endif()
endforeach()
