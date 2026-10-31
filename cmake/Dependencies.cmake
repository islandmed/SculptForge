# Third-party dependencies. Everything is fetched at configure time so a fresh
# checkout builds the same way on Linux, macOS and Windows.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# glm (header-only maths)
FetchContent_Declare(glm
  URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

if(NOT SCULPTFORGE_BUILD_APP)
  return()
endif()

# SDL3 (windowing, input, pen pressure, native file dialogs)
if(SCULPTFORGE_USE_SYSTEM_SDL)
  find_package(SDL3 3.2 CONFIG QUIET)
endif()
if(SDL3_FOUND)
  message(STATUS "Using system SDL3 ${SDL3_VERSION}")
else()
  message(STATUS "Fetching SDL3")
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-3.2.30.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # Static: release packages then need no separate SDL library (zlib licence allows this).
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
endif()

# Dear ImGui (UI) with the SDL3 + OpenGL3 backends
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# Item hooks used by the scripted demo recorder (src/app/Demo.cpp); inert unless enabled at runtime.
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
target_link_libraries(imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})
