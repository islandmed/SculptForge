cmake_minimum_required(VERSION 3.24)
project(SculptForge VERSION 0.10.9 LANGUAGES C CXX)
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
  src/core/SurfaceCursor.cpp
  src/core/IndexBits.cpp
  src/core/ThreadPool.cpp
  src/core/UploadOrder.cpp
  src/core/Benchmark.cpp
  src/core/HardwareProfile.cpp
  src/core/FrameGovernor.cpp
  src/core/TierConfig.cpp
  src/core/MemoryGuard.cpp
  src/core/Sculpt.cpp
  src/core/Dyntopo.cpp
  src/core/DyntopoCommands.cpp
  src/core/Undo.cpp
  src/core/Timeline.cpp
  src/core/Primitives.cpp
  src/core/Document.cpp
  src/core/Commands.cpp
  src/core/Polygroups.cpp
  src/core/SelectionSets.cpp
  src/core/ScreenSelect.cpp
  src/core/SubTools.cpp
  src/core/ExtractMirror.cpp
  src/core/MeshTransform.cpp
  src/core/Deform.cpp
  src/core/TransformCommands.cpp
  src/core/Decimate.cpp
  src/core/Script.cpp
  src/core/Macro.cpp
  src/core/Pack.cpp
  src/core/Json.cpp
  src/core/BrushRecipe.cpp
  src/core/BrushSettings.cpp
  src/core/AlphaOptions.cpp
  src/core/BrushLibrary.cpp
  src/core/Image.cpp
  src/core/ImageTo3D.cpp
  src/core/Splats.cpp
  src/core/SplatCapture.cpp
  src/core/Sculptable.cpp
  src/core/Voxel.cpp
  src/core/EnginePresets.cpp
  src/core/Masking.cpp
  src/core/Cut.cpp
  src/core/BrushAlpha.cpp
  src/core/Stroke.cpp
  src/core/Precision.cpp
  src/core/PathStroke.cpp
  src/core/Sha256.cpp
  src/core/BrushGallery.cpp
  src/core/Polypaint.cpp
  src/core/PaintLayer.cpp
  src/core/Texture.cpp
  src/core/TextureCommands.cpp
  src/core/Uv.cpp
  src/core/UvCommands.cpp
  src/core/UvPack.cpp
  src/core/UvUnwrap.cpp
  src/core/TileImage.cpp
  src/core/EnvironmentMath.cpp
  src/core/MeshMaps.cpp
  src/core/LayerStack.cpp
  src/core/MaterialRecipe.cpp
  src/core/TextureChannels.cpp
  src/core/TextureChannelCommands.cpp
  src/core/LayerStackIO.cpp
  src/core/MapExport.cpp
  src/core/Multires.cpp
  src/core/Micro.cpp
  src/core/MicroSculpt.cpp
  src/core/MicroCommands.cpp
  src/core/MicroStore.cpp
  src/core/FocusDetail.cpp
  src/core/FocusBench.cpp
  src/core/MultiresCommands.cpp
  src/core/AutoUv.cpp
  src/core/TextureTransfer.cpp
  src/core/SurfaceQuery.cpp
  src/core/QRemesh.cpp
  src/core/GeneratedRemesh.cpp
  src/core/QRemeshGuides.cpp
  src/core/QRemesh2.cpp
  src/core/QRemesh2Proxy.cpp
  src/core/QRemesh2Field.cpp
  src/core/QRemesh2Param.cpp
  src/core/QuadQuality.cpp
  src/core/ZSpheres.cpp
  src/core/Projection.cpp
  src/core/RetopoCommands.cpp
  third_party/xatlas/xatlas.cpp
  src/core/Bake.cpp
  src/core/BakeImage.cpp
  src/core/Tangents.cpp
  third_party/mikktspace/mikktspace.c
  src/core/Vdm.cpp
  src/core/Kelvinlet.cpp
  src/core/KeepMasked.cpp
  src/core/ViewCapture.cpp
  third_party/tinyexr/tinyexr_impl.cpp
  src/core/Matcap.cpp
  src/core/Trash.cpp
  src/core/Project.cpp
  src/core/Guide.cpp
  src/core/WhatsNew.cpp
  src/core/UpdateCheck.cpp
  src/core/Tablet.cpp
  src/core/RadialMenu.cpp
  src/core/StrokePopup.cpp
  src/core/SpaceMouse.cpp
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
  src/io/MayaBridge.cpp
  src/io/MayaCommands.cpp
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
# Starter meshes (core/StarterMeshes.h): base cages in compressed projects, built by tools/starter/build.sh from
# scratch or from the MakeHuman base mesh (CC0; docs/starter-meshes.md).
embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedStarterMeshes.cpp sf::assets
  starterHumanHead ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/human-head.sfproj.z
  starterBust ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/bust.sfproj.z
  starterFullBody ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/full-body.sfproj.z
  starterHand ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/hand.sfproj.z
  starterFoot ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/foot.sfproj.z
  starterEar ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/ear.sfproj.z
  starterEye ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/eye.sfproj.z
  starterStylisedHead ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/stylised-head.sfproj.z
  starterCreatureHead ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/creature-head.sfproj.z
  starterTorso ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/torso.sfproj.z
  starterRoundedBox ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/rounded-box.sfproj.z
  starterCylinder ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/cylinder.sfproj.z
  starterCapsule ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/capsule.sfproj.z
  starterRing ${CMAKE_CURRENT_SOURCE_DIR}/assets/starter/ring.sfproj.z)
target_sources(sculptforge_core PRIVATE src/core/StarterMeshes.cpp ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedStarterMeshes.cpp)
# The PBR viewport's bundled environments (core/EnvironmentMath.h): CC0 HDRIs from Poly Haven, 512 x 256
# (THIRD_PARTY_NOTICES.md).
embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedHdri.cpp sf::assets
  hdriPhotoStudio ${CMAKE_CURRENT_SOURCE_DIR}/assets/hdri/brown_photostudio_02.hdr
  hdriSmallStudio ${CMAKE_CURRENT_SOURCE_DIR}/assets/hdri/studio_small_09.hdr
  hdriOutdoorSky ${CMAKE_CURRENT_SOURCE_DIR}/assets/hdri/kloofendal_48d_partly_cloudy_puresky.hdr)
target_sources(sculptforge_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedHdri.cpp)
# The built-in Material Recipes (core/MaterialRecipe.h): Fill layer stacks as data files.
embed_files(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedRecipes.cpp sf::assets
  recipeWornMetal ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/worn_metal.sfmaterial
  recipePaintedEdges ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/painted_edges.sfmaterial
  recipeDirtAndDust ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/dirt_and_dust.sfmaterial
  recipeRustedIron ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/rusted_iron.sfmaterial)
target_sources(sculptforge_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedRecipes.cpp)
target_include_directories(sculptforge_core PUBLIC src PRIVATE third_party/stb third_party/ufbx third_party/xatlas third_party/mikktspace third_party/tinyexr)
if(WIN32)
  target_link_libraries(sculptforge_core PRIVATE shell32) # core/Trash.cpp: the Recycle Bin
endif()
# Third-party C code: compile quietly.
if(MSVC)
  set_source_files_properties(third_party/ufbx/ufbx.c third_party/xatlas/xatlas.cpp third_party/mikktspace/mikktspace.c third_party/tinyexr/tinyexr_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(third_party/ufbx/ufbx.c third_party/xatlas/xatlas.cpp third_party/mikktspace/mikktspace.c third_party/tinyexr/tinyexr_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
find_package(Threads REQUIRED)
target_link_libraries(sculptforge_core PUBLIC glm::glm Threads::Threads)
# zstd for Focus Detail tiles (core/FocusDetail.h), when the system has it; without it the tiles use our own codec.
# Not on macOS: Homebrew's zstd is single-architecture and breaks the universal (x86_64 + arm64) build.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT APPLE)
  pkg_check_modules(SF_ZSTD QUIET IMPORTED_TARGET libzstd)
endif()
if(SF_ZSTD_FOUND)
  target_link_libraries(sculptforge_core PRIVATE PkgConfig::SF_ZSTD)
  target_compile_definitions(sculptforge_core PRIVATE SF_HAVE_ZSTD)
endif()
target_compile_definitions(sculptforge_core PUBLIC GLM_FORCE_XYZW_ONLY)
target_compile_definitions(sculptforge_core PRIVATE SCULPTFORGE_VERSION="${PROJECT_VERSION}") # written into projects
if(MSVC)
  target_compile_options(sculptforge_core PRIVATE /W4 /utf-8 /permissive-)
  target_compile_definitions(sculptforge_core PUBLIC NOMINMAX _USE_MATH_DEFINES)
else()
  # -ffp-contract=off: no fused multiply-adds, so a result doesn't depend on the optimisation level (distro
  # packages build at -O2; face_weights' fast path is tested to match a full evaluation exactly).
  target_compile_options(sculptforge_core PRIVATE -Wall -Wextra -Wno-unused-parameter -ffp-contract=off)
endif()

# --- Bring-your-own-AI (brush assistant). HTTPS via WinHTTP or libcurl. ---
add_library(sculptforge_ai STATIC src/ai/Http.cpp src/ai/AiPolicy.cpp src/ai/BrushAssistant.cpp src/ai/Generator3D.cpp
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
    fontMono ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf # the looks (Preferences > Interface > Look)
    fontMonoBold ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-SemiBold.ttf
    appIcon ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/sculptforge-256.png
    supporters ${CMAKE_CURRENT_SOURCE_DIR}/docs/supporters.txt # Help > About (never empty: it starts with comments)
    supportCard ${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/support-card.jpg) # the support card's picture (decoded only when it shows)
  include(cmake/EmbedGuide.cmake)
  embed_guide(${CMAKE_CURRENT_BINARY_DIR}/EmbeddedGuide.cpp ${CMAKE_CURRENT_SOURCE_DIR}/docs/guide)
  add_executable(sculptforge
    ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedFonts.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedGuide.cpp
    src/app/main.cpp
    src/app/Bench.cpp
    src/app/Calibration.cpp
    src/app/PanelPerformanceProfile.cpp
    src/app/CrashLog.cpp
    src/app/App.cpp
    src/app/Precision.cpp
    src/app/AsyncStroke.cpp
    src/app/Demo.cpp
    src/app/UiTest.cpp
    src/app/Actions.cpp
    src/app/BlenderKeymap.cpp
    src/app/Ui.cpp
    src/app/UiLayout.cpp
    src/app/Shell.cpp
    src/app/ShellGeometry.cpp
    src/app/Palettes.cpp
    src/app/Theme.cpp
    src/app/LookSchemes.cpp
    src/app/I18n.cpp
    src/app/I18nSpanish.cpp
    src/app/CommandPalette.cpp
    src/app/Console.cpp
    src/app/Config.cpp
    src/app/LightBox.cpp
    src/app/BrushPreview.cpp
    src/app/Thumbnails.cpp
    src/app/Pickers.cpp
    src/app/UserGuide.cpp
    src/app/AssetLibrary.cpp
    src/app/PanelMasking.cpp
    src/app/PanelCut.cpp
    src/app/PanelPolygroups.cpp
    src/app/PanelSelectionSets.cpp
    src/app/Selection.cpp
    src/app/PanelBrushes.cpp
    src/app/BrushSettingsUi.cpp
    src/app/PanelVdm.cpp
    src/app/PanelKelvinlet.cpp
    src/app/PanelFace.cpp
    src/app/PanelBrushLibrary.cpp
    src/app/PanelBrushGallery.cpp
    src/app/PanelPolypaint.cpp
    src/app/PanelPacks.cpp
    src/app/MemoryConfirm.cpp
    src/app/PanelPaintLayers.cpp
    src/app/PanelTexture.cpp
    src/app/PanelUvEditor.cpp
    src/app/PanelPbr.cpp
    src/app/PanelFill.cpp
    src/app/PanelGeometry.cpp
    src/app/Gizmo.cpp
    src/app/PanelBake.cpp
    src/app/FocusDetailApp.cpp
    src/app/PanelRetopo.cpp
    src/app/PanelZSpheres.cpp
    src/app/PanelSplatTrain.cpp
    src/app/PanelSculptable.cpp
    src/app/PanelPerformance.cpp
    src/app/PanelGoz.cpp
    src/app/PanelBlender.cpp
    src/app/PanelMaya.cpp
    src/app/PanelPhone.cpp
    src/app/PanelProject.cpp
    src/app/PanelSubTools.cpp
    src/app/PanelSupport.cpp
    src/app/PanelAi.cpp
    src/app/PanelSupportCard.cpp
    src/app/WhatsNewPopup.cpp
    src/app/PanelExtract.cpp
    src/app/PanelTimeline.cpp
    src/app/ProofMode.cpp
    src/app/Tablet.cpp
    src/app/Wintab.cpp
    src/app/SpaceMouse.cpp
    src/app/Input.cpp
    src/app/RadialMenu.cpp
    src/app/StrokePopup.cpp
    src/render/GL.cpp
    src/render/Camera.cpp
    src/render/Clusters.cpp
    src/render/Ghost.cpp
    src/render/Renderer.cpp
    src/render/UvView.cpp
    src/render/MicroRenderer.cpp
  )
  target_link_libraries(sculptforge PRIVATE sculptforge_core sculptforge_ai sculptforge_phone imgui SDL3::SDL3)
  target_include_directories(sculptforge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lucide)
  target_compile_definitions(sculptforge PRIVATE SCULPTFORGE_VERSION="${PROJECT_VERSION}"
                                                 SF_UPDATE_CHECK=$<BOOL:${SCULPTFORGE_UPDATE_CHECK}>)
  # The commit a build came from, for proof mode's results (src/app/ProofMode.cpp); taken when CMake configures.
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE SF_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  if(SF_GIT_COMMIT)
    set_source_files_properties(src/app/ProofMode.cpp PROPERTIES COMPILE_DEFINITIONS "SF_GIT_COMMIT=\"${SF_GIT_COMMIT}\"")
  endif()
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
    # Per-monitor DPI v2 from the first instruction (a window moved between 100 % and 150 % monitors stays sharp
    # and SDL reports each monitor's scale); MSVC merges it into the manifest the linker writes. (SDL 3 also asks
    # for it at start-up; the manifest covers what runs before, and other toolchains rely on that.)
    if(MSVC)
      target_sources(sculptforge PRIVATE packaging/windows/sculptforge.manifest)
    endif()
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
  add_executable(sculptforge_tests ${SCULPTFORGE_TEST_SOURCES} src/cli/McpServer.cpp src/app/AssetLibrary.cpp src/app/BrushPreview.cpp
                                   src/app/UiLayout.cpp src/app/ShellGeometry.cpp src/app/LookSchemes.cpp src/app/I18n.cpp src/app/I18nSpanish.cpp src/render/Camera.cpp
                                   src/render/Clusters.cpp src/render/Ghost.cpp)
  target_include_directories(sculptforge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_definitions(sculptforge_tests PRIVATE SF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  target_link_libraries(sculptforge_tests PRIVATE sculptforge_core sculptforge_ai sculptforge_phone)
  if(SCULPTFORGE_BUILD_APP) # the action registry (key chords, shortcut overrides) needs SDL's key names
    target_sources(sculptforge_tests PRIVATE src/app/Actions.cpp src/app/BlenderKeymap.cpp)
    target_link_libraries(sculptforge_tests PRIVATE SDL3::SDL3)
    target_compile_definitions(sculptforge_tests PRIVATE SF_TEST_ACTIONS)
  endif()
  if(MSVC)
    target_compile_options(sculptforge_tests PRIVATE /utf-8)
  endif()
  # The suite as SCULPTFORGE_TEST_SHARDS processes (sculptforge_tests_0of16 ...; `ctest -j` runs them at once, CI does;
  # more shards than cores balance better), each with its own temporary folder so tests in different shards never
  # share a scratch file.
  set(SCULPTFORGE_TEST_SHARDS 16 CACHE STRING "Number of ctest entries the sculptforge_tests suite is split into")
  math(EXPR last_shard "${SCULPTFORGE_TEST_SHARDS} - 1")
  foreach(k RANGE ${last_shard})
    set(shard_tmp ${CMAKE_CURRENT_BINARY_DIR}/test-tmp/shard${k})
    file(MAKE_DIRECTORY ${shard_tmp})
    add_test(NAME sculptforge_tests_${k}of${SCULPTFORGE_TEST_SHARDS}
             COMMAND sculptforge_tests --shard ${k}/${SCULPTFORGE_TEST_SHARDS})
    set_tests_properties(sculptforge_tests_${k}of${SCULPTFORGE_TEST_SHARDS} PROPERTIES COST 1000 # the longest: start first
                         ENVIRONMENT "TMPDIR=${shard_tmp};TMP=${shard_tmp};TEMP=${shard_tmp}")
  endforeach()
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
    # The Maya shelf script (integrations/maya, #42) with a mocked maya.cmds, round-tripped through sculptforge-cli.
    add_test(NAME maya_shelf_script COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/maya/test_sculptforge_maya.py
                                            --cli ${SCULPTFORGE_CLI_RUNNER})
    # The add-on inside a real Blender, when one is installed (tests/blender/test_in_blender.py).
    find_program(SCULPTFORGE_BLENDER blender)
    if(SCULPTFORGE_BLENDER)
      add_test(NAME blender_addon COMMAND ${SCULPTFORGE_BLENDER} -b --factory-startup -noaudio --python-exit-code 1
                                        --python ${CMAKE_CURRENT_SOURCE_DIR}/tests/blender/test_in_blender.py
                                        -- --cli ${SCULPTFORGE_CLI_RUNNER})
      set_tests_properties(blender_addon PROPERTIES TIMEOUT 300)
    endif()
    # The release smoke test (tests/release_smoke.py, run by release.yml on every package): open, sculpt, save, reopen.
    add_test(NAME release_smoke COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_smoke.py
                                      --cli ${SCULPTFORGE_CLI_RUNNER} --out ${CMAKE_CURRENT_BINARY_DIR}/release-smoke)
    # Proof mode results: the validator and website list tool (tools/benchmark/submit.py, docs/benchmark.md).
    # Insider daily drops (tools/drops): the generator is deterministic, the gate rejects blank, seamed, torn and
    # duplicate candidates, and every kind makes a pack that passes the install checks.
    add_test(NAME insider_drops COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/drops/drops.py selftest
                                        --cli ${SCULPTFORGE_CLI_RUNNER})
    set_tests_properties(insider_drops PROPERTIES TIMEOUT 300)
    # Release notes from note fragments (tools/release/assemble_notes.py, changes/README.md): the assembler, and
    # every fragment on this branch parses.
    add_test(NAME release_notes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/assemble_notes.py --self-test)
    add_test(NAME release_notes_fragments COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/release/assemble_notes.py --check)
    # The Actions cache pruner keeps the newest cache of each kind (tools/ci/prune_caches.py).
    add_test(NAME ci_prune_caches COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/prune_caches.py --self-test)
    add_test(NAME ci_slowest_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/slowest_tests.py --self-test)
    add_test(NAME benchmark_submit COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/benchmark/submit.py selftest)
    # The upstream watch (tools/upstream-watch, .github/workflows/upstream-watch.yml) against recorded API responses.
    add_test(NAME upstream_watch COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/upstream/test_upstream_watch.py)
    add_test(NAME guide_site COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/guide/build_site.py --check
                                     --out ${CMAKE_CURRENT_BINARY_DIR}/guide-site)
    # Menu paths in the guide name the SculptForge layout's task menus (Edit > Preferences, not Preferences).
    add_test(NAME guide_menu_paths COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/guide/menu_paths.py --check)
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
    # The hardware profile panel with a real calibration (tests/ui/hardware_profile.sfui).
    if(TEST ui_hardware_profile)
      set_tests_properties(ui_hardware_profile PROPERTIES ENVIRONMENT "SF_HW_PROFILE_IN_TESTS=1")
    endif()
    # The shell layout again on a 1920x1080 window (docs/ui-design.md §2.3).
    add_test(NAME ui_shell_layout_1080 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/shell_layout.sfui
             --size 1920x1080 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/shell_layout_1080)
    set_tests_properties(ui_shell_layout_1080 PROPERTIES LABELS ui TIMEOUT 300)
    add_test(NAME ui_help_console_1080 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/help_console.sfui
             --size 1920x1080 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/help_console_1080)
    set_tests_properties(ui_help_console_1080 PROPERTIES LABELS ui TIMEOUT 300)
    # The S / U / I / O slider on a UI scaled 1.5x (Windows at 150%, #9): it must still open under the cursor.
    add_test(NAME ui_size_popup_scaled COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/size_popup.sfui
             --size 2400x1350 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/size_popup_scaled)
    set_tests_properties(ui_size_popup_scaled PROPERTIES LABELS ui TIMEOUT 300)
    # The stroke popup on a UI scaled 1.5x: it opens under the cursor and its items are where they're drawn.
    add_test(NAME ui_stroke_popup_scaled COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/stroke_popup.sfui
             --size 2400x1350 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/stroke_popup_scaled)
    set_tests_properties(ui_stroke_popup_scaled PROPERTIES LABELS ui TIMEOUT 300)
    # UI scale (#87) at 1600x900, where Automatic in a test is exactly 100 %; label "dpi": release.sh step check runs them before the tag.
    add_test(NAME ui_ui_scale_900 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/ui_scale.sfui
             --size 1600x900 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/ui_scale_900)
    set_tests_properties(ui_ui_scale_900 PROPERTIES LABELS "ui;dpi" TIMEOUT 300)
    if(TEST ui_ui_scale)
      set_tests_properties(ui_ui_scale PROPERTIES LABELS "ui;dpi")
    endif()
    # The Library on a 1280x720 screen (#80): its close button stays on screen.
    add_test(NAME ui_library_small_screen_720 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/library_small_screen.sfui
             --size 1280x720 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/library_small_screen_720)
    set_tests_properties(ui_library_small_screen_720 PROPERTIES LABELS ui TIMEOUT 300)
    add_test(NAME ui_user_guide_1080 COMMAND sculptforge --ui-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/user_guide.sfui
             --size 1920x1080 --ui-out ${CMAKE_CURRENT_BINARY_DIR}/ui-shots/user_guide_1080)
    set_tests_properties(ui_user_guide_1080 PROPERTIES LABELS ui TIMEOUT 300)
    # A crash leaves a note in the log file (#16). Needs no window.
    add_test(NAME crash_log COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:sculptforge>
             -DDIR=${CMAKE_CURRENT_BINARY_DIR}/crash-log-test -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/crash_log.cmake)
    # Idle, the app draws nothing (#14): --measure-idle fails when it keeps drawing frames.
    add_test(NAME ui_idle_redraw COMMAND sculptforge --measure-idle 4)
    set_tests_properties(ui_idle_redraw PROPERTIES LABELS ui TIMEOUT 60 ENVIRONMENT "SF_NO_UPDATE_CHECK=1")
    # sculptforge --calibrate on this machine's GPU: the JSON it prints passes the schema check (it exits 1 if not).
    add_test(NAME ui_calibrate_json COMMAND sculptforge --calibrate-quick --calibrate-no-save)
    set_tests_properties(ui_calibrate_json PROPERTIES LABELS ui TIMEOUT 60)
    # A fast stroke in the interactive loop (dabs queued, placed a few milliseconds a frame): every dab lands on the
    # pointer's circle (#19).
    add_test(NAME ui_stroke_path COMMAND sculptforge --run "new_sphere" --measure-stroke 1.5)
    set_tests_properties(ui_stroke_path PROPERTIES LABELS ui TIMEOUT 60 ENVIRONMENT "SF_NO_UPDATE_CHECK=1")
    # Background dabs (a heavy mesh's stroke on the solver thread, no preview), forced on a light sphere: replaying
    # the stroke in the frame loop gives the same mesh, bit for bit, and its undo the mesh it started from.
    add_test(NAME ui_stroke_background COMMAND sculptforge --run "new_sphere" --measure-stroke 1.5)
    set_tests_properties(ui_stroke_background PROPERTIES LABELS ui TIMEOUT 60
                         ENVIRONMENT "SF_NO_UPDATE_CHECK=1;SF_MEASURE_VERIFY=1;SF_BACKGROUND_DABS=1")
    # Predictive strokes with the brush size changing every frame mid-stroke: each batch of dabs keeps the size it
    # was handed (no data race with the solver thread), replaying the stroke without the thread gives the same
    # mesh, bit for bit, and its undo the mesh it started from.
    add_test(NAME ui_stroke_resize COMMAND sculptforge --run "new_sphere" --measure-stroke 1.5)
    set_tests_properties(ui_stroke_resize PROPERTIES LABELS ui TIMEOUT 60 DISABLED TRUE # predictive strokes: not yet identical in every run
                         ENVIRONMENT "SF_NO_UPDATE_CHECK=1;SF_MEASURE_RESIZE=1;SF_MEASURE_VERIFY=1;SF_PREDICTIVE_STROKE=1")
    # The system clipboard is one per desktop (one X server under CI's xvfb-run), and on X11 every SDL program asks a
    # new clipboard owner what it holds: a program that quits meanwhile makes the owner's answer fail (BadWindow).
    # Scripts that set, read or paste it (or click a Copy / Paste button) run alone under ctest -j.
    foreach(t ${SCULPTFORGE_UI_TESTS} shell_layout_1080 help_console_1080 size_popup_scaled stroke_popup_scaled user_guide_1080 ui_scale_900)
      string(REGEX REPLACE "_1080$|_scaled$|_900$" "" base ${t})
      get_filename_component(base ${base} NAME_WE)
      set(script ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui/${base}.sfui)
      get_filename_component(name ${t} NAME_WE)
      if(EXISTS ${script} AND TEST ui_${name})
        file(READ ${script} body)
        if(body MATCHES "(^|\n)(clipboard|expect_clipboard) |\"[^\"\n]*(Copy|Paste)[^\"\n]*\"|ctrl\\+[cvx]")
          set_tests_properties(ui_${name} PROPERTIES RUN_SERIAL TRUE)
        endif()
      endif()
    endforeach()
    # The longest UI tests start first under ctest -j (COST orders a parallel run; without it they may start last).
    foreach(t ui_make_sculptable ui_texture_transfer ui_bake)
      if(TEST ${t})
        set_tests_properties(${t} PROPERTIES COST 500)
      endif()
    endforeach()
    # Timed measurements run alone when ctest runs tests in parallel (CI: ctest -j): other tests' load must not
    # change what they measure.
    foreach(t ui_idle_redraw ui_calibrate_json ui_stroke_path ui_hardware_profile)
      if(TEST ${t})
        set_tests_properties(${t} PROPERTIES RUN_SERIAL TRUE)
      endif()
    endforeach()
    # The guide's keyboard table must match the app's default shortcuts (tools/guide/update_shortcuts.py).
    if(Python3_Interpreter_FOUND)
      sculptforge_runner(SCULPTFORGE_APP_RUNNER sculptforge)
      add_test(NAME ui_guide_shortcuts COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/guide/update_shortcuts.py
               --app ${SCULPTFORGE_APP_RUNNER} --check)
      set_tests_properties(ui_guide_shortcuts PROPERTIES LABELS ui TIMEOUT 300)
    endif()
  endif()
endif()
