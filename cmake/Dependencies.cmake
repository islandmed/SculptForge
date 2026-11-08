# Third-party dependencies. Everything is fetched at configure time so a fresh
# checkout builds the same way on Linux, macOS and Windows.
include(${CMAKE_CURRENT_LIST_DIR}/Fetch.cmake) # include(FetchContent), the pins, sf_fetch_declare
set(FETCHCONTENT_QUIET ON)

# glm (header-only maths)
sf_fetch_declare(glm DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# wasmtime (sandboxed plugins; the core library uses it, so also without the app)
include(${CMAKE_CURRENT_LIST_DIR}/Wasmtime.cmake)

if(NOT SCULPTFORGE_BUILD_APP)
  return()
endif()

# SDL3 (windowing, input, pen pressure, native file dialogs)
# Linux needs 3.4.18 or later: before 3.4 a Wayland pen was a new pen each time it came into range and its eraser
# end drew as the tip (3.4.18); an older system SDL is replaced by the pinned one.
set(SF_SDL3_MIN 3.2)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(SF_SDL3_MIN 3.4.18)
  set(SF_PIN_SDL3 ${SF_PIN_SDL3_linux})
endif()
if(SCULPTFORGE_USE_SYSTEM_SDL)
  find_package(SDL3 ${SF_SDL3_MIN} CONFIG QUIET)
  if(NOT SDL3_FOUND)
    message(STATUS "No system SDL3 ${SF_SDL3_MIN} or later")
  endif()
endif()
if(SDL3_FOUND)
  message(STATUS "Using system SDL3 ${SDL3_VERSION}")
else()
  message(STATUS "Fetching SDL3")
  sf_fetch_declare(SDL3 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # Static: release packages then need no separate SDL library (zlib licence allows this).
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  # SDL 3.4 needs the XTest headers for X11 unless told not to; nothing here uses XTest, and the build machines
  # (CI, the PC's Linux builds) don't all have libxtst-dev.
  set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
  # SDL 3.4.18's SDL_waylandevents.c names WL_KEYBOARD_KEY_STATE_REPEATED, which wayland-client's headers have only
  # from 1.24 (Ubuntu 22.04/24.04, EL8/9: 1.20-1.22). Which copy of wayland-client-protocol.h a file sees (the
  # system's or SDL's generated one) depends on include order, so the file itself says: without the enum's
  # _SINCE_VERSION macro, the protocol value 2. (SDL binds wl_seat at version 9 there: no compositor sends it.)
  set(sf_wl_events "${sdl3_SOURCE_DIR}/src/video/wayland/SDL_waylandevents.c")
  if(EXISTS "${sf_wl_events}")
    file(READ "${sf_wl_events}" sf_src)
    set(sf_anchor "static void keyboard_handle_key(")
    if(NOT sf_src MATCHES "SculptForge: wayland-client" AND sf_src MATCHES "WL_KEYBOARD_KEY_STATE_REPEATED")
      string(FIND "${sf_src}" "${sf_anchor}" sf_at)
      if(sf_at LESS 0)
        message(FATAL_ERROR "SDL_waylandevents.c changed: update the WL_KEYBOARD_KEY_STATE_REPEATED guard in cmake/Dependencies.cmake")
      endif()
      string(REPLACE "${sf_anchor}" "// SculptForge: wayland-client before 1.24 has no repeated key state (cmake/Dependencies.cmake)
#ifndef WL_KEYBOARD_KEY_STATE_REPEATED_SINCE_VERSION
#define WL_KEYBOARD_KEY_STATE_REPEATED 2
#endif
${sf_anchor}" sf_src "${sf_src}")
      file(WRITE "${sf_wl_events}" "${sf_src}")
    endif()
  endif()
endif()

# Dear ImGui (UI) with the SDL3 + OpenGL3 backends
sf_fetch_declare(imgui DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

# imgui_draw.cpp is built from a copy with two changes for text people type (src/app/UserGlyphs.cpp), which falls back
# to the system's fonts: ImFont::FindGlyph reports a missing glyph (GImFontMissingGlyph), and AddFont keeps a font
# passed with FontDataOwnedByAtlas = false where it is instead of copying it per font size (1.91.9 copies: a 19 MB
# system font merged into nine sizes cost 190 MB; every font the app adds stays alive while the atlas does). The
# fetched or packaged sources are left as they are; an ImGui whose code differs stops the configure here.
file(READ ${imgui_SOURCE_DIR}/imgui_draw.cpp SF_IMGUI_DRAW)
function(sf_imgui_patch from to)
  string(FIND "${SF_IMGUI_DRAW}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "imgui_draw.cpp changed; update its patches in cmake/Dependencies.cmake:\n${from}")
  endif()
  string(REPLACE "${from}" "${to}" patched "${SF_IMGUI_DRAW}")
  set(SF_IMGUI_DRAW "${patched}" PARENT_SCOPE)
endfunction()
sf_imgui_patch("ImFontGlyph* ImFont::FindGlyph(ImWchar c)
{
    if (c >= (size_t)IndexLookup.Size)
        return FallbackGlyph;
    const ImU16 i = IndexLookup.Data[c];
    if (i == (ImU16)-1)
        return FallbackGlyph;
    return &Glyphs.Data[i];
}" "void (*GImFontMissingGlyph)(ImFont* font, ImWchar c) = NULL; // SculptForge (cmake/Dependencies.cmake)
ImFontGlyph* ImFont::FindGlyph(ImWchar c)
{
    const ImU16 i = c < (size_t)IndexLookup.Size ? IndexLookup.Data[c] : (ImU16)-1;
    if (i == (ImU16)-1)
    {
        if (GImFontMissingGlyph)
            GImFontMissingGlyph(this, c);
        return FallbackGlyph;
    }
    return &Glyphs.Data[i];
}")
sf_imgui_patch("    if (!new_font_cfg.FontDataOwnedByAtlas)
    {
        new_font_cfg.FontData = IM_ALLOC(new_font_cfg.FontDataSize);
        new_font_cfg.FontDataOwnedByAtlas = true;
        memcpy(new_font_cfg.FontData, font_cfg->FontData, (size_t)new_font_cfg.FontDataSize);
    }
" "    // SculptForge: FontDataOwnedByAtlas = false data is the caller's, alive while the atlas is (no copy).
")
file(WRITE ${CMAKE_BINARY_DIR}/imgui_hooked/imgui_draw.cpp.new "${SF_IMGUI_DRAW}")
configure_file(${CMAKE_BINARY_DIR}/imgui_hooked/imgui_draw.cpp.new ${CMAKE_BINARY_DIR}/imgui_hooked/imgui_draw.cpp COPYONLY)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${CMAKE_BINARY_DIR}/imgui_hooked/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# Item hooks used by the scripted demo recorder (src/app/Demo.cpp); inert unless enabled at runtime.
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
# Characters past U+FFFF (CJK Extension B and up, e.g. 𠀋 in a Part's name) as themselves, not U+FFFD.
target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
target_link_libraries(imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})
