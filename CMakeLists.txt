cmake_minimum_required(VERSION 3.24)
project(SculptForge VERSION 0.9.3 LANGUAGES C CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SCULPTFORGE_BUILD_APP "Build the SculptForge desktop application" ON)
option(SCULPTFORGE_BUILD_PYTHON "Build the Python module (import sculptforge)" OFF)
set(SCULPTFORGE_PACKAGE_SUFFIX "" CACHE STRING "Extra tag in package file names, e.g. vfx2026")
option(SCULPTFORGE_BUILD_TESTS "Build unit tests" ON)
option(SCULPTFORGE_USE_SYSTEM_SDL "Use an installed SDL3 if one is found" ON)
option(SCULPTFORGE_UPDATE_CHECK "The app checks the website for a newer version once a day (OFF for package managers)" ON)
if(SCULPTFORGE_BUILD_TESTS)
  enable_testing()
endif()
# Position-independent code so the core libraries can also go into the Python module.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/Dependencies.cmake)

# --- Core library: geometry, sculpting, undo, commands, file IO (no GPU/UI) ---
add_library(sculptforge_core STATIC
  src/core/Mesh.cpp
  src/core/BVH.cpp
  src/core/IndexBits.cpp
  src/core/ThreadPool.cpp
  src/core/UploadOrder.cpp
  src/core/Benchmark.cpp
  src/core/Sculpt.cpp
  src/core/Dyntopo.cpp
  src/core/DyntopoCommands.cpp
  src/core/Undo.cpp
  src/core/Primitives.cpp
  src/core/Document.cpp
  src/core/Commands.cpp
  src/core/Polygroups.cpp
  src/core/ScreenSelect.cpp
  src/core/SubTools.cpp
  src/core/MeshTransform.cpp
  src/core/TransformCommands.cpp
  src/core/Decimate.cpp
  src/core/Script.cpp
  src/core/Json.cpp
  src/core/BrushRecipe.cpp
  src/core/BrushLibrary.cpp
  src/core/Image.cpp
  src/core/ImageTo3D.cpp
  src/core/Splats.cpp
  src/core/SplatCapture.cpp
  src/core/Sculptable.cpp
  src/core/Voxel.cpp
  src/core/EnginePresets.cpp
  src/core/Masking.cpp
  src/core/BrushAlpha.cpp
  src/core/Stroke.cpp
  src/core/PathStroke.cpp
  src/core/Sha256.cpp
  src/core/BrushGallery.cpp
  src/core/Polypaint.cpp
  src/core/PaintLayer.cpp
  src/core/Texture.cpp
  src/core/TextureCommands.cpp
  src/core/Multires.cpp
  src/core/MultiresCommands.cpp
  src/core/AutoUv.cpp
  src/core/TextureTransfer.cpp
  src/core/SurfaceQuery.cpp
  src/core/QRemesh.cpp
  src/core/QRemeshGuides.cpp
  src/core/Projection.cpp
  src/core/RetopoCommands.cpp
  third_party/xatlas/xatlas.cpp
  src/core/Bake.cpp
  src/core/BakeImage.cpp
  src/core/Tangents.cpp
  third_party/mikktspace/mikktspace.c
  src/core/Vdm.cpp
  third_party/tinyexr/tinyexr_impl.cpp
  src/core/Matcap.cpp
  src/core/Project.cpp
  src/core/Guide.cpp
  src/core/UpdateCheck.cpp
  src/core/Tablet.cpp
  src/io/ObjIO.cpp
  src/io/FbxIO.cpp
  third_party/ufbx/ufbx.c
  src/io/GltfIO.cpp
  src/io/PlyIO.cpp
  src/io/UsdIO.cpp
  src/io/Zip.cpp
  src/io/GozIO.cpp
  src/io/GozCommands.cpp
  src/io/BlenderBridge.cpp
  src/io/BlenderCommands.cpp
  src/io/StlIO.cpp
  src/io/ArExport.cpp
)
# Facial shapes (core/Face.h): the template head with the 52 ARKit shapes, built from ICT-FaceKit (MIT) by
# tools/face/build_ict_template.py, embedded so the app, the CLI and the tests all have it.
include(cmake/EmbedFile.cmake)
embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedFace.cpp sf::assets
  faceTemplateData ${CMAKE_CURRENT_SOURCE_DIR}/assets/face/ict-arkit.sfface)
target_sources(sculptforge_core PRIVATE src/core/Face.cpp src/core/FaceCommands.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedFace.cpp)
target_include_directories(sculptforge_core PUBLIC src PRIVATE third_party/stb third_party/ufbx third_party/xatlas third_party/mikktspace third_party/tinyexr)
# Third-party C code: compile quietly.
if(MSVC)
  set_source_files_properties(third_party/ufbx/ufbx.c third_party/xatlas/xatlas.cpp third_party/mikktspace/mikktspace.c third_party/tinyexr/tinyexr_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(third_party/ufbx/ufbx.c third_party/xatlas/xatlas.cpp third_party/mikktspace/mikktspace.c third_party/tinyexr/tinyexr_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
find_package(Threads REQUIRED)
target_link_libraries(sculptforge_core PUBLIC glm::glm Threads::Threads)
target_compile_definitions(sculptforge_core PUBLIC GLM_FORCE_XYZW_ONLY)
target_compile_definitions(sculptforge_core PRIVATE SCULPTFORGE_VERSION="${PROJECT_VERSION}") # written into projects
if(MSVC)
  target_compile_options(sculptforge_core PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(sculptforge_core PUBLIC NOMINMAX _USE_MATH_DEFINES)
else()
  target_compile_options(sculptforge_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Bring-your-own-AI (brush assistant). HTTPS via WinHTTP or libcurl. ---
add_library(sculptforge_ai STATIC src/ai/Http.cpp src/ai/BrushAssistant.cpp src/ai/Generator3D.cpp
  src/ai/ImageTo3DService.cpp src/ai/GradioClient.cpp src/ai/ComfyUI.cpp src/ai/AiCommands.cpp src/ai/BrushGalleryClient.cpp src/link/Socket.cpp
  src/link/UnrealLink.cpp src/ai/Process.cpp src/ai/SplatTrainer.cpp)
# Lets a build run from its tree find the bundled ComfyUI workflows (integrations/comfyui/workflows).
target_compile_definitions(sculptforge_ai PRIVATE SF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
find_package(Threads REQUIRED)
target_link_libraries(sculptforge_ai PUBLIC sculptforge_core Threads::Threads)
if(WIN32)
  target_link_libraries(sculptforge_ai PRIVATE winhttp ws2_32)
else()
  find_package(CURL QUIET)
  if(CURL_FOUND)
    target_link_libraries(sculptforge_ai PRIVATE CURL::libcurl)
    target_compile_definitions(sculptforge_ai PRIVATE SF_HAVE_CURL)
  else()
    message(WARNING "libcurl not found: AI features will report that HTTP is unavailable")
  endif()
endif()
if(MSVC)
  target_compile_options(sculptforge_ai PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(sculptforge_ai PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Phone link (src/link/PhoneLink.h, docs/phone-link.md): the capture page server, AR export, live face feed ---
option(SCULPTFORGE_WEBRTC "Live face feed over WebRTC (fetches libdatachannel and Mbed TLS; OFF: capture and AR only)" ON)
include(cmake/EmbedFile.cmake)
embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedPhone.cpp sf::phone_assets
  capturePage ${CMAKE_CURRENT_SOURCE_DIR}/assets/phone/capture.html
  facePage ${CMAKE_CURRENT_SOURCE_DIR}/website/link/index.html)
add_library(sculptforge_phone STATIC ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedPhone.cpp src/link/HttpServer.cpp
  src/link/PhoneLink.cpp src/link/FaceFeed.cpp src/link/PhoneRtc.cpp src/link/Qr.cpp third_party/qrcodegen/qrcodegen.cpp)
target_include_directories(sculptforge_phone PRIVATE third_party/qrcodegen)
target_link_libraries(sculptforge_phone PUBLIC sculptforge_ai)
if(SCULPTFORGE_WEBRTC)
  include(cmake/WebRTC.cmake)
  target_link_libraries(sculptforge_phone PRIVATE LibDataChannel::LibDataChannelStatic MbedTLS::MbedTLS)
  target_compile_definitions(sculptforge_phone PRIVATE SF_HAVE_WEBRTC=1)
else()
  target_compile_definitions(sculptforge_phone PRIVATE SF_HAVE_WEBRTC=0)
endif()
if(MSVC)
  target_compile_options(sculptforge_phone PRIVATE /W4 /utf-8 /permissive-)
  set_source_files_properties(third_party/qrcodegen/qrcodegen.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  target_compile_options(sculptforge_phone PRIVATE -Wall -Wextra -Wno-unused-parameter)
  set_source_files_properties(third_party/qrcodegen/qrcodegen.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# --- Headless command-line tool (pipelines, build farms, AI agents) ---
add_executable(sculptforge-cli src/cli/main.cpp src/cli/McpServer.cpp)
target_compile_definitions(sculptforge-cli PRIVATE SCULPTFORGE_VERSION="${PROJECT_VERSION}")
target_link_libraries(sculptforge-cli PRIVATE sculptforge_core sculptforge_ai)

# --- Desktop application ---
if(SCULPTFORGE_BUILD_APP)
  include(cmake/EmbedFile.cmake)
  embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedFonts.cpp sf::assets
    fontRegular ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
    fontSemiBold ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf
    fontLucide ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/lucide.ttf
    appIcon ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/sculptforge-256.png)
  include(cmake/EmbedGuide.cmake)
  embed_guide(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedGuide.cpp ${CMAKE_CURRENT_SOURCE_DIR}/docs/guide)
  add_executable(sculptforge
    ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedFonts.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedGuide.cpp
    src/app/main.cpp
    src/app/Bench.cpp
    src/app/App.cpp
    src/app/Demo.cpp
    src/app/UiTest.cpp
    src/app/Actions.cpp
    src/app/Ui.cpp
    src/app/UiLayout.cpp
    src/app/Shell.cpp
    src/app/ShellGeometry.cpp
    src/app/Palettes.cpp
    src/app/Theme.cpp
    src/app/CommandPalette.cpp
    src/app/Console.cpp
    src/app/Config.cpp
    src/app/LightBox.cpp
    src/app/Thumbnails.cpp
    src/app/Pickers.cpp
    src/app/UserGuide.cpp
    src/app/AssetLibrary.cpp
    src/app/PanelMasking.cpp
    src/app/PanelPolygroups.cpp
    src/app/Selection.cpp
    src/app/PanelBrushes.cpp
    src/app/PanelVdm.cpp
    src/app/PanelFace.cpp
    src/app/PanelBrushLibrary.cpp
    src/app/PanelBrushGallery.cpp
    src/app/PanelPolypaint.cpp
    src/app/PanelPaintLayers.cpp
    src/app/PanelTexture.cpp
    src/app/PanelGeometry.cpp
    src/app/Gizmo.cpp
    src/app/PanelBake.cpp
    src/app/PanelRetopo.cpp
    src/app/PanelSplatTrain.cpp
    src/app/PanelSculptable.cpp
    src/app/PanelPerformance.cpp
    src/app/PanelGoz.cpp
    src/app/PanelBlender.cpp
    src/app/PanelPhone.cpp
    src/app/PanelProject.cpp
    src/app/PanelSubTools.cpp
    src/app/Tablet.cpp
    src/app/Input.cpp
    src/render/GL.cpp
    src/render/Camera.cpp
    src/render/Clusters.cpp
    src/render/Renderer.cpp
  )
  target_link_libraries(sculptforge PRIVATE sculptforge_core sculptforge_ai sculptforge_phone imgui SDL3::SDL3)
  target_include_directories(sculptforge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lucide)
  target_compile_definitions(sculptforge PRIVATE SCULPTFORGE_VERSION="${PROJECT_VERSION}"
                                                 SF_UPDATE_CHECK=$<BOOL:${SCULPTFORGE_UPDATE_CHECK}>)
  if(MSVC)
    target_compile_options(sculptforge PRIVATE /W4 /utf-8 /permissive-)
  else()
    target_compile_options(sculptforge PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
  if(WIN32)
    set_target_properties(sculptforge PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release,MinSizeRel>)
    set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/sculptforge.ico")
    if(CMAKE_HOST_WIN32)
      string(REPLACE "/" "\\\\" ICON_PATH "${ICON_PATH}")
    endif()
    configure_file(packaging/windows/sculptforge.rc ${CMAKE_CURRENT_BINARY_DIR}/sculptforge.rc @ONLY)
    target_sources(sculptforge PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/sculptforge.rc)
  endif()
  if(APPLE)
    # App icon: build an .icns from the 1024 px PNG with Apple's own tools.
    set(ICONSET ${CMAKE_CURRENT_BINARY_DIR}/SculptForge.iconset)
    set(ICNS ${CMAKE_CURRENT_BINARY_DIR}/SculptForge.icns)
    set(ICON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/sculptforge-1024.png)
    set(ICON_COMMANDS COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET})
    foreach(size 16 32 128 256 512)
      math(EXPR size2 "${size} * 2")
      list(APPEND ICON_COMMANDS
        COMMAND sips -z ${size} ${size} ${ICON_SRC} --out ${ICONSET}/icon_${size}x${size}.png
        COMMAND sips -z ${size2} ${size2} ${ICON_SRC} --out ${ICONSET}/icon_${size}x${size}@2x.png)
    endforeach()
    add_custom_command(OUTPUT ${ICNS} ${ICON_COMMANDS}
      COMMAND iconutil -c icns ${ICONSET} -o ${ICNS}
      DEPENDS ${ICON_SRC} VERBATIM)
    target_sources(sculptforge PRIVATE ${ICNS})
    set_source_files_properties(${ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(sculptforge PROPERTIES
      MACOSX_BUNDLE TRUE
      OUTPUT_NAME "SculptForge"
      MACOSX_BUNDLE_BUNDLE_NAME "SculptForge"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.sculptforge.SculptForge"
      MACOSX_BUNDLE_ICON_FILE "SculptForge.icns"
      MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 The SculptForge contributors. GPL-3.0-or-later."
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    install(TARGETS sculptforge BUNDLE DESTINATION .)
    # The command-line tool travels inside the app bundle.
    install(TARGETS sculptforge-cli RUNTIME DESTINATION SculptForge.app/Contents/MacOS)
  else()
    install(TARGETS sculptforge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

# --- Install layout and packages ---
if(NOT APPLE)
  install(TARGETS sculptforge-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(UNIX AND NOT APPLE)
  install(FILES packaging/linux/org.sculptforge.SculptForge.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/linux/org.sculptforge.SculptForge.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES assets/branding/sculptforge-icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
          RENAME org.sculptforge.SculptForge.svg)
  install(FILES assets/branding/sculptforge-256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
          RENAME org.sculptforge.SculptForge.png)
  install(FILES assets/branding/sculptforge-512.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
          RENAME org.sculptforge.SculptForge.png)
endif()
if(APPLE)
  set(SF_DOC_DIR .)
elseif(WIN32)
  set(SF_DOC_DIR doc)
else()
  set(SF_DOC_DIR ${CMAKE_INSTALL_DOCDIR})
endif()
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md CHANGELOG.md DESTINATION ${SF_DOC_DIR})
install(FILES docs/ai-services.md docs/unreal.md docs/generators.md docs/comfyui.md DESTINATION ${SF_DOC_DIR}/docs)
# Bundled ComfyUI workflows, where the Generate 3D window looks for them (see src/ai/ComfyUI.cpp).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integrations/comfyui/workflows)
  if(APPLE)
    set(SF_COMFY_DIR SculptForge.app/Contents/Resources/comfyui)
  else()
    set(SF_COMFY_DIR ${CMAKE_INSTALL_DATADIR}/sculptforge/comfyui)
  endif()
  install(DIRECTORY integrations/comfyui/workflows DESTINATION ${SF_COMFY_DIR})
endif()

set(CPACK_PACKAGE_NAME "sculptforge")
set(CPACK_PACKAGE_VENDOR "The SculptForge contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/islandmed/SculptForge/issues")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Free and open-source digital sculpting with game-engine pipelines")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://sculptforge.app/")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SculptForge")
set(CPACK_PACKAGE_EXECUTABLES "sculptforge" "SculptForge")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" SF_OS)
set(SF_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if(SF_ARCH MATCHES "^(AMD64|amd64|x86_64)$")
  set(SF_ARCH x86_64)
elseif(SF_ARCH MATCHES "^(arm64|ARM64|aarch64)$")
  set(SF_ARCH arm64)
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES ";")
  set(SF_ARCH universal)
endif()
set(CPACK_PACKAGE_FILE_NAME "SculptForge-${PROJECT_VERSION}-${SF_OS}-${SF_ARCH}")
if(SCULPTFORGE_PACKAGE_SUFFIX)
  string(APPEND CPACK_PACKAGE_FILE_NAME "-${SCULPTFORGE_PACKAGE_SUFFIX}")
endif()
set(CPACK_STRIP_FILES ON)
# Debian / Ubuntu
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "The SculptForge contributors <https://github.com/islandmed/SculptForge>")
# Fedora / RHEL
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_URL "https://sculptforge.app/")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
# Windows installer
set(CPACK_NSIS_DISPLAY_NAME "SculptForge")
set(CPACK_NSIS_PACKAGE_NAME "SculptForge")
set(CPACK_NSIS_MUI_ICON ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/sculptforge.ico)
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\sculptforge.exe")
set(CPACK_NSIS_URL_INFO_ABOUT "https://sculptforge.app/")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\SculptForge.lnk' '$INSTDIR\\\\bin\\\\sculptforge.exe'")
# macOS disk image
set(CPACK_DMG_VOLUME_NAME "SculptForge")
if(APPLE)
  set(CPACK_PRE_BUILD_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacSign.cmake)
endif()
if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
else()
  set(CPACK_GENERATOR "DEB;RPM;TGZ")
endif()
include(CPack)

# --- Python module ---
if(SCULPTFORGE_BUILD_PYTHON)
  find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
  FetchContent_Declare(pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v3.0.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(pybind11)
  pybind11_add_module(sculptforge_python MODULE src/python/module.cpp)
  set_target_properties(sculptforge_python PROPERTIES OUTPUT_NAME sculptforge)
  target_link_libraries(sculptforge_python PRIVATE sculptforge_core sculptforge_ai)
  target_compile_definitions(sculptforge_python PRIVATE SCULPTFORGE_VERSION="${PROJECT_VERSION}")
  set(SCULPTFORGE_PYTHON_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages"
      CACHE STRING "Where to install the Python module")
  install(TARGETS sculptforge_python LIBRARY DESTINATION ${SCULPTFORGE_PYTHON_INSTALL_DIR})
  if(SCULPTFORGE_BUILD_TESTS)
    add_test(NAME python_api COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=$<TARGET_FILE_DIR:sculptforge_python>
             ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python.py)
  endif()
endif()

# --- Blender add-on (addons/blender): `cmake --build build --target blender_addon` writes its zips ---
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_custom_target(blender_addon
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/addons/blender/build_addon.py --check
            --version ${PROJECT_VERSION} --out ${CMAKE_CURRENT_BINARY_DIR}/blender-addon
    COMMENT "Packaging the Blender add-on (sculptforge_bridge-${PROJECT_VERSION}.zip)")
endif()

# --- Tests ---
if(SCULPTFORGE_BUILD_TESTS)
  # tests/test_main.cpp plus every tests/test_*.cpp (add a file to add tests; see tests/TestHarness.h).
  file(GLOB SCULPTFORGE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
  add_executable(sculptforge_tests ${SCULPTFORGE_TEST_SOURCES} src/cli/McpServer.cpp src/app/AssetLibrary.cpp
                                   src/app/UiLayout.cpp src/app/ShellGeometry.cpp src/render/Camera.cpp
                                   src/render/Clusters.cpp)
  target_include_directories(sculptforge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_definitions(sculptforge_tests PRIVATE SF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  target_link_libraries(sculptforge_tests PRIVATE sculptforge_core sculptforge_ai sculptforge_phone)
  if(SCULPTFORGE_BUILD_APP) # the action registry (key chords, shortcut overrides) needs SDL's key names
    target_sources(sculptforge_tests PRIVATE src/app/Actions.cpp)
    target_link_libraries(sculptforge_tests PRIVATE SDL3::SDL3)
    target_compile_definitions(sculptforge_tests PRIVATE SF_TEST_ACTIONS)
  endif()
  if(MSVC)
    target_compile_options(sculptforge_tests PRIVATE /utf-8)
  endif()
  add_test(NAME sculptforge_tests COMMAND sculptforge_tests)
  # A stand-in for Brush, COLMAP and ffmpeg (tests/tools/fake_splat_tools.cpp): splat training is tested without a GPU.
  add_executable(sf_fake_splat_tools tests/tools/fake_splat_tools.cpp)
  target_include_directories(sf_fake_splat_tools PRIVATE third_party/stb)
  target_link_libraries(sf_fake_splat_tools PRIVATE sculptforge_core)
  add_dependencies(sculptforge_tests sf_fake_splat_tools)
  target_compile_definitions(sculptforge_tests PRIVATE SF_FAKE_SPLAT_TOOLS="$<TARGET_FILE:sf_fake_splat_tools>")

  # Tests that launch our programs from Python get a path they can run directly. When cross-compiling
  # (e.g. a Windows build tested under Wine, tools/wintest.sh) that is a script calling the emulator.
  function(sculptforge_runner out target)
    if(CMAKE_CROSSCOMPILING_EMULATOR)
      list(JOIN CMAKE_CROSSCOMPILING_EMULATOR " " emulator)
      file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/run-${target}.sh
           CONTENT "#!/bin/sh\nexec ${emulator} \"$<TARGET_FILE:${target}>\" \"$@\"\n"
           FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
      set(${out} ${CMAKE_CURRENT_BINARY_DIR}/run-${target}.sh PARENT_SCOPE)
    else()
      set(${out} $<TARGET_FILE:${target}> PARENT_SCOPE)
    endif()
  endfunction()

  # The seeded community brush gallery (tools/brush-gallery/repo) must stay valid
  # and its index.json current: every brush is round-tripped through sculptforge-cli.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    sculptforge_runner(SCULPTFORGE_CLI_RUNNER sculptforge-cli)
    add_test(NAME brush_gallery_index
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/brush-gallery/build_index.py
                     ${CMAKE_CURRENT_SOURCE_DIR}/tools/brush-gallery/repo --check --cli ${SCULPTFORGE_CLI_RUNNER})
    # The user guide (docs/guide): every link, anchor and image resolves, only characters the app's font has,
    # and the website pages build.
    # The Blender add-on's exchange code (addons/blender) against sculptforge-cli: a round trip through the
    # bridge is the identity (needs numpy, which Blender bundles).
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy" RESULT_VARIABLE SCULPTFORGE_NO_NUMPY OUTPUT_QUIET ERROR_QUIET)
    if(SCULPTFORGE_NO_NUMPY EQUAL 0)
      add_test(NAME blender_exchange COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/blender/test_exchange.py
                                           --cli ${SCULPTFORGE_CLI_RUNNER})
    endif()
    # The add-on's zips build and its manifest passes the checks Blender's validator makes.
    add_test(NAME blender_addon_package COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/addons/blender/build_addon.py
                                              --check --version ${PROJECT_VERSION} --out ${CMAKE_CURRENT_BINARY_DIR}/blender-addon)
    # The add-on inside a real Blender, when one is installed (tests/blender/test_in_blender.py).
    find_program(SCULPTFORGE_BLENDER blender)
    if(SCULPTFORGE_BLENDER)
      add_test(NAME blender_addon COMMAND ${SCULPTFORGE_BLENDER} -b --factory-startup -noaudio --python-exit-code 1
                                        --python ${CMAKE_CURRENT_SOURCE_DIR}/tests/blender/test_in_blender.py
                                        -- --cli ${SCULPTFORGE_CLI_RUNNER})
      set_tests_properties(blender_addon PROPERTIES TIMEOUT 300)
    endif()
    add_test(NAME guide_site COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/guide/build_site.py --check
                                     --out ${CMAKE_CURRENT_BINARY_DIR}/guide-site)
  endif()

  # The phone link's relay (services/link-worker, a Cloudflare Worker) under plain Node, when Node 20+ is installed.
  find_program(SCULPTFORGE_NODE node)
  if(SCULPTFORGE_NODE)
    add_test(NAME link_worker COMMAND ${SCULPTFORGE_NODE} --test test/relay.test.mjs
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/services/link-worker)
  endif()

  # UI tests: each tests/ui/*.sfui script drives the real app headless (needs an
  # OpenGL 3.3 context; CI runs them on Linux under Xvfb with Mesa). Label "ui".
  if(SCULPTFORGE_BUILD_APP)
    file(GLOB SCULPTFORGE_UI_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/*.sfui)
    foreach(script ${SCULPTFORGE_UI_TESTS})
      get_filename_component(name ${script} NAME_WE)
      add_test(NAME ui_${name} COMMAND sculptforge --ui-test ${script} --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/${name})
      set_tests_properties(ui_${name} PROPERTIES LABELS ui TIMEOUT 300)
    endforeach()
    # Splat training (also started by a phone scan) runs the fake Brush / COLMAP / ffmpeg.
    foreach(t ui_splat_train ui_phone_link)
      if(TEST ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT
          "SCULPTFORGE_BRUSH=$<TARGET_FILE:sf_fake_splat_tools>;SCULPTFORGE_COLMAP=$<TARGET_FILE:sf_fake_splat_tools>;SCULPTFORGE_FFMPEG=$<TARGET_FILE:sf_fake_splat_tools>")
      endif()
    endforeach()
    # The shell layout again on a 1920x1080 window (docs/ui-design.md §2.3).
    add_test(NAME ui_shell_layout_1080 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/shell_layout.sfui
             --size 1920x1080 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/shell_layout_1080)
    set_tests_properties(ui_shell_layout_1080 PROPERTIES LABELS ui TIMEOUT 300)
    add_test(NAME ui_help_console_1080 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/help_console.sfui
             --size 1920x1080 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/help_console_1080)
    set_tests_properties(ui_help_console_1080 PROPERTIES LABELS ui TIMEOUT 300)
    add_test(NAME ui_user_guide_1080 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/user_guide.sfui
             --size 1920x1080 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/user_guide_1080)
    set_tests_properties(ui_user_guide_1080 PROPERTIES LABELS ui TIMEOUT 300)
    # The guide's keyboard table must match the app's default shortcuts (tools/guide/update_shortcuts.py).
    if(Python3_Interpreter_FOUND)
      sculptforge_runner(SCULPTFORGE_APP_RUNNER sculptforge)
      add_test(NAME ui_guide_shortcuts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/guide/update_shortcuts.py
               --app ${SCULPTFORGE_APP_RUNNER} --check)
      set_tests_properties(ui_guide_shortcuts PROPERTIES LABELS ui TIMEOUT 300)
    endif()
  endif()
endif()
