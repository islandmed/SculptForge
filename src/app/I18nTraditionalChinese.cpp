// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Traditional Chinese interface (GitHub #114), translated by yoyomaimai and reviewed for SculptForge. The English
// text is the key (I18n.h); a label without an entry stays in English.
//
// Conventions (Taiwan usage):
//   - Names of tools, brushes and products stay in English: Quad Remesh, Forge Spheres, Lit Sphere, Splats, Clay Block
//     and every other brush, VDM, Alpha. Generic words are translated: 部件 (Part), 資源庫 (Library), 頂點繪製
//     (Vertex Paint), 面群組 (Face Groups), 工具列 (shelf), 工具匣 (tray).
//   - A space between Chinese and Latin text or numbers (新增 VDM 平面), full-width punctuation (，。：（）) except
//     right after a printf conversion or a literal %, where ASCII punctuation keeps the conversions the same.
//   - printf conversions (%d, %.1f, %s, %%) stay in the same order: tests check every entry.
//   - Every character has to be in assets/fonts/NotoSansTC-UI.otf: after changing this table, run
//     tools/i18n/subset_cjk_font.py (a test lists what is missing).
// The entries follow the Spanish table's order and groups, one entry per English text.
#include "app/I18n.h"

#include <iterator>

namespace sf::i18n {

namespace {

constexpr Entry kTraditionalChinese[] = {
    // --- Palettes (the palette bar) and their sub-palettes ---
    {"Alpha", "Alpha"},
    {"Brush", "筆刷"},
    {"Color", "色彩"},
    {"Draw", "繪製"},
    {"Edit", "編輯"},
    {"File", "檔案"},
    {"Generate", "產生"},
    {"Help", "說明"},
    {"Material", "材質"},
    {"Preferences", "偏好設定"},
    {"Render", "算繪"},
    {"Script", "指令碼"},
    {"Splats", "Splats"},
    {"Stroke", "筆畫"},
    {"Tool", "工具"},
    {"Transform", "變形"},
    // The SculptForge layout's task menus and their sections (Palettes.cpp taskMenus), Preferences > Interface > Look.
    {"Mesh", "網格"},
    {"View", "檢視"},
    {"Draw & Tablet", "繪製與繪圖板"},
    {"Symmetry & Gizmo", "對稱與操控器"},
    {"Vertex Paint & Color", "頂點繪製與色彩"},
    {"Parts & Geometry", "部件與幾何"},
    {"Materials", "材質"},
    {"Gaussian Splats", "高斯 Splat"},
    {"Look", "外觀"},
    {"Colours:", "色彩："},
    {"Match my desktop theme", "配合我的桌面佈景主題"},
    {"Uses your Omarchy theme's colours when Omarchy is installed", "安裝 Omarchy 後，使用您的 Omarchy 主題顏色"},
    {"Green", "綠色"},
    {"Amber", "琥珀色"},
    {"Scanlines & glow", "掃描線與光暈"},
    {"AI brush provider", "AI 筆刷提供者"},
    {"Autosave", "自動儲存"},
    {"Bake", "烘焙"},
    {"Config", "設定"},
    {"Deformation", "變形"},
    {"Display", "顯示"},
    {"Export", "匯出"},
    {"Facial Shapes", "臉部形狀"},
    {"Geometry", "幾何"},
    {"Hotkeys", "快速鍵"},
    {"Input", "輸入"},
    {"Interface", "界面"},
    {"Kelvinlet & Capture", "彈性形變與擷取"},
    {"Layers", "圖層"},
    {"Library", "資源庫"},
    {"Masking", "遮罩"},
    {"Paint Layers", "繪製圖層"},
    {"Performance", "效能"},
    {"Face Groups", "面群組"},
    {"Remesh", "重新網格化"},
    {"Part", "部件"},
    {"Parts", "部件"},
    {"Tablet", "繪圖板"},
    {"Texture Map", "貼圖"},
    {"Updates", "更新"},
    {"Vector Displacement", "向量位移"},
    {"Visibility", "可見性"},
    {"Forge Spheres", "Forge Spheres"},
    {"Edit recipe", "編輯配方"},
    // --- Palette paths in the Ctrl-help box ("Tool > Geometry") ---
    {"Bottom shelf", "底部工具列"},
    {"Left shelf", "左側工具列"},
    {"Right shelf", "右側工具列"},
    {"Top shelf", "頂部工具列"},
    {"Title", "標題"},
    {"Layout", "版面配置"},
    {"Dynamic topology", "動態拓樸"},
    {"Camera", "相機"},
    {"Import / Export", "匯入／匯出"},
    {"From an image", "從影像建立"},
    {"From Image", "從影像"},
    {"Voxel Remesh", "體素重新網格化"},
    {"Modify Topology", "修改拓樸"},
    {"Game Ready", "適用於遊戲"},
    {"Navigation", "導覽"},
    // --- Title bar, status line, search ---
    {"Untitled", "未命名"},
    {"Project", "專案"},
    {"Search", "搜尋"},
    {"Minimize", "最小化"},
    {"Menus", "選單"},
    {"Snapshots", "快照"},
    {"Save Snapshot", "儲存快照"},
    {"Base", "基底"},
    {"   Splats ", "   Splats "},
    {"Verts %s   Tris %s   Part %s (%d/%d)   Layer %s%s%s", "頂點 %s   三角面 %s   部件 %s (%d/%d)   圖層 %s%s%s"},
    {"Customize: Ctrl+Alt+drag to move or remove items", "自訂：按住 Ctrl+Alt 並拖曳，即可移動或移除項目"},
    {"Customize off", "自訂已關閉"},
    {"Saved", "已儲存"},
    {"Background: Flat Colour", "背景：純色"},
    {"Background: Gradient", "背景：漸層"},
    // --- Command palette ---
    {"Search commands, brushes and settings", "搜尋命令、筆刷和設定"},
    {"Recently used", "最近使用"},
    {"  No matching commands", "  找不到相符的命令"},
    {" result", " 個結果"},
    {" results", " 個結果"},
    {"select", "選取"},
    {"run", "執行"},
    {"edit in console", "在主控台中編輯"},
    {"close", "關閉"},
    // --- Ctrl-help box ---
    {"No UVs yet: pick a method and Unwrap (or mark seams first)", "尚無 UV：選擇一種方法並展開（或先標記接縫）"},
    {"No mesh", "沒有網格"},
    {"No description yet.", "目前沒有說明。"},
    {"Not available right now.", "目前無法使用。"},
    {"Learn more: %s", "深入瞭解：%s"},
    {"Back to the palette bar", "返回調色盤列"},
    {"Move to the tray", "移至工具匣"},
    {"Take this palette out of the tray.", "將此調色盤從工具匣中取出。"},
    {"Keep this palette in the right tray (Shift+click: left tray), or drag the icon onto a tray.",
     "將此調色盤留在右側工具匣（Shift+按一下可移至左側工具匣），或將圖示拖曳至工具匣。"},
    {"Click to open or close. Opening one closes the others (Shift+click keeps them open; Preferences > Interface).",
     "按一下即可開啟或關閉。開啟其中一個會關閉其他項目（Shift+按一下可讓它們保持開啟；偏好設定 > 界面）。"},
    {"Click to save the project. * means it has unsaved changes.", "按一下即可儲存專案。* 表示有尚未儲存的變更。"},
    {"Find any command, brush or material by name and run it.", "依名稱搜尋任何命令、筆刷或材質，並執行。"},
    {"Minimize the SculptForge window.", "最小化 SculptForge 視窗。"},
    {"Left tray", "左側工具匣"},
    {"Right tray", "右側工具匣"},
    {"Shortcut for", "快速鍵："},
    // --- Shelves ---
    {"Add a brush", "新增筆刷"},
    {"Put another brush (built-in or one of yours) on this shelf.", "將其他筆刷（內建筆刷或自訂筆刷）加入此工具列。"},
    {"One of your custom brushes.", "你的其中一個自訂筆刷。"},
    {"Remove from shelf", "從工具列移除"},
    {"Tile size", "圖示大小"},
    {"Small", "小"},
    {"Medium", "中"},
    {"Large", "大"},
    {"The active brush. Click for the brush picker; the bottom shelf holds favourites.", "目前使用的筆刷。按一下可開啟筆刷選擇器；底部工具列可放置我的最愛。"},
    {"How dabs follow the cursor: Dots, Drag Stamp, Slide Stamp, Spray or Lazy Stroke. Click to choose.",
     "筆觸印記跟隨游標的方式：點狀、拖曳印章、滑動印章、噴灑或延遲筆畫。按一下即可選擇。"},
    {"The VDM brush's vector displacement map. Click to pick another.", "VDM 筆刷的向量位移貼圖。按一下可選擇其他貼圖。"},
    {"The brush's stamp shape (white = full effect). Click for the alpha picker.", "筆刷的印章形狀（白色代表完整效果）。按一下可開啟 Alpha 選擇器。"},
    {"How the model is shaded in the view. Click for the material picker.", "模型在視圖中的著色方式。按一下可開啟材質選擇器。"},
    {"Brush radius in screen pixels. [ and ] change it by 10%; Ctrl+click to type.",
     "以螢幕像素為單位的筆刷半徑。[ 和 ] 可調整 10%; 按住 Ctrl 並按一下即可輸入數值。"},
    {"Brush radius in screen pixels. [ and ] change it by 10%.", "以螢幕像素為單位的筆刷半徑。[ 和 ] 可調整 10%."},
    {"ActiveTris / TotalTris", "作用中三角面數／三角面總數"},
    {"Move", "移動"},
    {"Zoom", "縮放"},
    {"Rotate", "旋轉"},
    {"Scale", "比例"},
    // --- Draw and the top shelf ---
    {"Intensity", "強度"},
    {"Smooth Intensity", "平滑強度"},
    {"Focal Length", "焦距"},
    {"Detail", "細節"},
    {"Sculpt", "雕刻"},
    {"Paint", "繪製"},
    {"Dynamic", "動態"},
    {"Constant detail", "固定細節"},
    {"Axes", "座標軸"},
    {"No pen detected", "未偵測到筆"},
    {"Pen pressure", "筆壓"},
    {"Pressure > Size", "筆壓 > 大小"},
    {"Pressure > Strength", "筆壓 > 強度"},
    {"Front faces only", "僅限正面"},
    {"Ctrl+Shift+drag a Cut shape", "按住 Ctrl+Shift 並拖曳以建立切割形狀"},
    {"Depth %.2f", "深度 %.2f"},
    {"Copy the active Part (%s)", "複製目前的部件 (%s)"},
    {"How deep Cut In goes, or how far Cut Add stands out, as a share of the Part's size.",
     "Cut In 的切入深度，或 Cut Add 的凸出距離，以部件大小的比例表示。"},
    {"Invert (Alt)", "反轉 (Alt)"},
    {"Invert", "反轉"},
    {"Smooth keeps volume", "平滑時保留體積"},
    {"Smooth face group borders only", "僅平滑面群組邊界"},
    {"Symmetry", "對稱"},
    {"Symmetry X", "X 軸對稱"},
    {"Local", "區域"},
    {"X axis", "X 軸"},
    {"Y axis", "Y 軸"},
    {"Z axis", "Z 軸"},
    {"Timeline position", "時間軸位置"},
    {"Top of viewport", "視埠頂端"},
    {"How strongly painting colours the surface (1-100); Ctrl+click to type.", "繪製為表面著色的強度（1-100）；按住 Ctrl 並按一下即可輸入數值。"},
    {"This Part has subdivision levels, so strokes don't remesh it: delete the lower (or higher) levels first.",
     "此部件具有細分層級，因此筆畫不會對其重新網格化：請先刪除較低（或較高）的層級。"},
    // --- Edit, File, Help ---
    {"Undo", "復原"},
    {"Redo", "重做"},
    {"Paste brush", "貼上筆刷"},
    {"New Sphere", "新增球體"},
    {"New Plane", "新增平面"},
    {"New Forge Sphere", "新增 Forge Sphere"},
    {"Open", "開啟"},
    {"Open Recent", "開啟最近使用的檔案"},
    {"Save", "儲存"},
    {"Save As", "另存新檔"},
    {"Import", "匯入"},
    {"Export OBJ", "匯出 OBJ"},
    {"Export STL", "匯出 STL"},
    {"Export GLB", "匯出 GLB"},
    {"Export LOD Chain", "匯出 LOD 鏈"},
    {"Export USD / USDZ", "匯出 USD / USDZ"},
    {"Export FBX for Unreal", "匯出供 Unreal 使用的 FBX"},
    {"Export EXR", "匯出 EXR"},
    {"Export Maps", "匯出貼圖"},
    {"Export Texture", "匯出紋理"},
    {"Export Displacement", "匯出位移貼圖"},
    {"Export timelapse", "匯出縮時影片"},
    {"Send to Unreal", "傳送至 Unreal"},
    {"Send to Blender", "傳送至 Blender"},
    {"Receive from Blender", "從 Blender 接收"},
    {"Save Splats", "儲存 Splats"},
    {"Capture from phone", "從手機擷取"},
    {"Quit", "結束"},
    {"Controls", "操作方式"},
    {"User Guide", "使用指南"},
    {"What's New", "最新消息"},
    {"Check for updates", "檢查更新"},
    {"Command palette", "命令面板"},
    {"Website & documentation", "網站與說明文件"},
    {"Join the Discord", "加入 Discord"},
    {"Join the community on Discord", "加入 Discord 社群"},
    {"Report a problem", "回報問題"},
    {"Open log folder", "開啟記錄檔資料夾"},
    {"Fixes & suggestions", "修正與建議"},
    {"Benchmark (proof mode)", "效能基準測試（證明模式）"},
    {"Support SculptForge", "支持 SculptForge"},
    {"About SculptForge", "關於 SculptForge"},
    {"Welcome", "歡迎"},
    // --- Generate ---
    {"Generate 3D", "生成 3D 模型"},
    {"Choose image", "選擇圖片"},
    {"Change image", "更換圖片"},
    {"Relief", "浮雕"},
    {"Inflate", "膨脹"},
    {"Stamp", "壓印"},
    {"AI Brush", "AI 筆刷"},
    {"Image or text to a sculptable 3D model: Hugging Face (free), fal.ai, Meshy, your ComfyUI server, your own command, or an offline blockout.",
     "將圖片或文字轉換成可雕刻的 3D 模型：Hugging Face（免費）、fal.ai、Meshy、你的 ComfyUI 伺服器、自訂命令，或離線建立粗模。"},
    // --- Render, Display, Material ---
    {"Wireframe", "線框"},
    {"Show Mesh", "顯示網格"},
    {"Show Vertex Paint", "顯示頂點繪製"},
    {"Show Mask", "顯示遮罩"},
    {"Show Face Groups", "顯示面群組"},
    {"Show Splats", "顯示 Splats"},
    {"Show mesh", "顯示網格"},
    {"Show mask", "顯示遮罩"},
    {"Show face groups", "顯示面群組"},
    {"Show vertex paint", "顯示頂點繪製"},
    {"Colour tints the material", "顏色會為材質染色"},
    {"Show texture", "顯示紋理"},
    {"Show UVs", "顯示 UV"},
    {"Mask", "遮罩"},
    {"Groups", "群組"},
    {"Material colour", "材質顏色"},
    {"Colour of the Studio material (the clay colour).", "Studio 材質的顏色（黏土色）。"},
    {"Frame model", "將模型置中顯示"},
    {"Frame Part", "將部件置中顯示"},
    {"Fullscreen", "全螢幕"},
    {"Gradient", "漸層"},
    {"Flat Colour", "純色"},
    {"Background colour", "背景顏色"},
    {"Flat", "平面"},
    {"Load Lit Sphere", "載入 Lit Sphere"},
    {"Material picker", "材質選擇器"},
    {"Material swatches", "材質色票"},
    // --- Script ---
    {"Console", "主控台"},
    {"Run script file", "執行指令碼檔案"},
    {"Copy session as script", "將工作階段複製為指令碼"},
    {"Clear console", "清除主控台"},
    {"Close the console", "關閉主控台"},
    {"Every action is a script command: run them with --script \"subdivide; remesh resolution=200\" or the sculptforge-cli tool.",
     "每個操作都是一條指令碼命令：使用 --script \"subdivide; remesh resolution=200\" 或 sculptforge-cli 工具來執行。"},
    // --- Stroke ---
    {"Continuous", "連續"},
    {"Stroke picker", "筆畫選擇器"},
    {"Lazy radius", "延遲半徑"},
    // --- Tool: header, Parts ---
    {"tris", "tris"},
    {"Append", "附加"},
    {"Append as Part", "附加為部件"},
    {"Add a new Part beside the model", "在模型旁新增部件"},
    {"Insert on the model", "插入模型"},
    {"Duplicate", "複製"},
    {"Rename", "重新命名"},
    {"Delete", "刪除"},
    {"Move up", "上移"},
    {"Move down", "下移"},
    {"Move up the list", "在清單中上移"},
    {"Move down the list", "在清單中下移"},
    {"Merge down", "向下合併"},
    {"Merge the active Part with the one below it", "將作用中的部件與下方部件合併"},
    {"Merge visible", "合併可見項目"},
    {"Split parts", "分離部件"},
    {"Split groups", "分離群組"},
    {"One Part per disconnected piece", "每個不相連的部分各自建立一個部件"},
    {"One Part per face group", "每個面群組各自建立一個部件"},
    {"Alt+click a Part to select it", "按住 Alt 並點選部件即可選取"},
    {"All Parts", "所有部件"},
    {"Solo", "單獨顯示"},
    {"Sphere", "球體"},
    {"Cube", "立方體"},
    {"Plane", "平面"},
    {"From file", "從檔案"},
    {"Active Part's copy", "作用中部件的副本"},
    {"Not enough GPU memory to show every Part: hide some or step down a subdivision level.",
     "GPU 記憶體不足，無法顯示所有部件：請隱藏部分部件或降低一級細分。"},
    {"%s\n%zu vertices, %zu triangles%s%s\nClick to select, double-click to rename", "%s\n%zu 個頂點，%zu 個三角形%s%s\n點選以選取，連按兩下以重新命名"},
    {"Extract", "擷取"},
    {"Accept", "接受"},
    {"Double", "雙倍"},
    {"Thick", "厚度"},
    {"Dist", "距離"},
    // --- Geometry ---
    {"Divide", "細分"},
    {"Subdivide", "細分"},
    {"Reconstruct Subdiv", "重建細分"},
    {"Divide to Micro", "細分至微細分"},
    {"Delete Micro", "刪除微細分"},
    {"Bake Micro", "烘焙微細分"},
    {"Micro Surface Noise", "微細分表面雜訊"},
    {"Micro", "微細分"},
    {"SDiv", "細分級"},
    {"No subdivision levels - Divide adds them", "沒有細分級別——使用「細分」新增級別"},
    {"Decimate", "減面"},
    {"Keep", "保留"},
    {"Keep Masked", "保留遮罩"},
    {"Keep Groups", "保留群組"},
    {"Keep Original", "保留原始模型"},
    {"Keep Texture", "保留紋理"},
    {"Reduce the triangle count while keeping the shape. Sculpt layers are kept.", "在保留形狀的同時減少三角形數量，並保留雕刻圖層。"},
    {"Fraction of the triangles Decimate keeps.", "減面後保留的三角形比例。"},
    {"Mirror", "鏡像"},
    {"Mirror And Weld", "鏡像並焊接"},
    {"Close Gaps", "封閉間隙"},
    {"Fill holes", "填補孔洞"},
    {"Delete hidden", "刪除隱藏部分"},
    // --- Quad Remesh and Remesh ---
    {"Quad Remesh", "四邊形重拓樸"},
    {"Quad Remesh Guides", "四邊形重拓樸引導線"},
    {"Target Polygons", "目標多邊形數量"},
    {"Adaptive Size", "自適應尺寸"},
    {"Curves Strength", "曲線強度"},
    {"Guides", "引導線"},
    {"Use Vertex Paint", "使用頂點繪製"},
    {"Detect Edges", "偵測邊緣"},
    {"Half", "一半"},
    {"Same", "相同"},
    {"Quality Report", "品質報告"},
    {"no curves", "沒有曲線"},
    {"%zu curves (%zu loops)", "%zu 條曲線（%zu 個迴圈）"},
    {"%zu polygons", "%zu 個多邊形"},
    {"%s: %zu quads in %.1f s", "%s：%zu 個四邊形，耗時 %.1f 秒"},
    {"%zu piece(s) used the legacy method", "%zu 個部分使用了舊版方法"},
    {"Corners off square: %.1f deg mean, %.0f deg worst 1%%", "角落偏離直角：平均 %.1f°，最差 %.0f°（最差的 1%%）"},
    {"Poles: %zu  (3 edges %zu, 5 edges %zu, other %zu)", "極點：%zu（3 條邊 %zu，5 條邊 %zu，其他 %zu）"},
    {"Distance to the sculpt: %.3f%% mean, %.2f%% max", "與雕刻表面的距離：平均 %.3f%%，最大 %.2f%%"},
    {"Resolution", "解析度"},
    {"Remeshing", "正在重拓樸"},
    {"Cancel", "取消"},
    {"Stop the remesh; the Part stays as it is.", "停止重拓樸；部件將維持目前狀態。"},
    {"Voxels across the model: higher keeps more detail and makes more triangles.", "模型寬度方向的體素數量：數值越高，保留的細節越多，產生的三角形也越多。"},
    {"Remesh for sculpting", "重拓樸以利雕刻"},
    // --- Masking ---
    {"Clear", "清除"},
    {"All", "全部"},
    {"Blur", "模糊"},
    {"Sharpen", "銳化"},
    {"Grow", "擴張"},
    {"Shrink", "縮減"},
    {"By cavity", "依凹陷處"},
    {"By face group", "依面群組"},
    {"Passes", "次數"},
    {"Mask Strength", "遮罩強度"},
    {"Threshold", "閾值"},
    {"Clear Mask", "清除遮罩"},
    {"Invert Mask", "反轉遮罩"},
    {"Mask All", "全部加上遮罩"},
    {"Mask brush", "遮罩筆刷"},
    {"Unmask everything (Mask Pen: Ctrl + drag background)", "取消所有遮罩（遮罩筆：按住 Ctrl 並拖曳背景）"},
    {"Ctrl + click background", "Ctrl + 點選背景"},
    {"Mask creases and folds (replaces the mask; uses the mask strength)", "遮罩皺褶與折痕（取代目前的遮罩；使用遮罩強度設定）"},
    {"Mask one face group (replaces the mask)", "遮罩單一面群組（取代目前的遮罩）"},
    // --- Face Groups and Visibility ---
    {"By normals", "依法線"},
    {"By pieces", "依部件"},
    {"From mask", "從遮罩建立"},
    {"Clear groups", "清除群組"},
    {"Angle", "角度"},
    {"Group by UVs", "依 UV 分組"},
    {"Masked triangles become a new group", "將遮罩中的三角形建立為新群組"},
    {"New group wherever the surface turns more than the angle", "在表面轉折角度大於指定角度處建立新群組"},
    {"One group per disconnected piece", "每個不相連的部件各自建立一個群組"},
    {"Visible face groups", "可見的面群組"},
    {"Hide face group under cursor", "隱藏游標下方的面群組"},
    {"Isolate face group under cursor", "隔離游標下方的面群組"},
    {"Show all", "全部顯示"},
    {"Unhide all", "全部取消隱藏"},
    {"Invert visible", "反轉可見狀態"},
    {"Hide", "隱藏"},
    {"Show", "顯示"},
    {"Ctrl+Shift+click a group to isolate it, +Alt to hide it, on the background to show all.\nCtrl+Shift+drag hides outside the rectangle or lasso (Alt: inside).",
     "Ctrl+Shift+按一下群組可將其隔離；加按 Alt 可將其隱藏；在背景按一下則顯示全部。\nCtrl+Shift+拖曳可隱藏矩形或套索範圍外的內容（加按 Alt 則隱藏範圍內的內容）。"},
    // --- Layers, paint, texture ---
    {"New paint layer", "新增繪製圖層"},
    {"Delete layer", "刪除圖層"},
    {"Opacity", "不透明度"},
    {"Blend mode", "混合模式"},
    {"Texture", "貼圖"},
    {"New Texture", "新增貼圖"},
    {"Import Texture", "匯入貼圖"},
    {"Delete Texture", "刪除貼圖"},
    {"Vertex Paint > Texture", "頂點繪製 > 貼圖"},
    {"Texture > Vertex Paint", "貼圖 > 頂點繪製"},
    {"Texture size", "貼圖尺寸"},
    {"Auto UV", "自動 UV"},
    {"Pick colour", "取樣顏色"},
    {"Switch Color", "交換顏色"},
    {"Main colour", "主要顏色"},
    {"Second colour", "次要顏色"},
    // --- Transform and the gizmo ---
    {"Gizmo", "操縱器"},
    {"Front view", "前視圖"},
    {"Back view", "後視圖"},
    {"Left view", "左視圖"},
    {"Right view", "右視圖"},
    {"Top view", "頂視圖"},
    {"Bottom view", "底視圖"},
    {"Opposite side", "相反側"},
    {"Orbit left", "向左環繞"},
    {"Orbit right", "向右環繞"},
    {"Orbit up", "向上環繞"},
    {"Orbit down", "向下環繞"},
    {"Zoom in", "放大"},
    {"Zoom out", "縮小"},
    {"Orthographic", "正交"},
    {"Larger brush", "放大筆刷"},
    {"Smaller brush", "縮小筆刷"},
    {"Pivot", "樞紐點"},
    // --- Preferences > Config ---
    // --- Preferences > Interface ---
    {"Language", "語言"},
    {"Tooltip delay", "工具提示延遲"},
    {"One sub-palette open", "一次只開啟一個子調色盤"},
    {"Reduce motion", "減少動態效果"},
    {"Auto collapse left tray", "自動收合左側工具匣"},
    {"Auto collapse right tray", "自動收合右側工具匣"},
    {"Palettes strictly A-Z", "調色盤嚴格依 A 至 Z 排列"},
    {"Left-handed layout", "左手操作版面"},
    {"Shelf tiles", "工具列圖示大小"},
    {"UI scale", "界面縮放比例"},
    {"Automatic", "自動"},
    {"Rotation", "旋轉"},
    {"Turntable", "轉盤"},
    {"Camera notch", "相機瀏海區"},
    {"Always reserve", "一律保留空間"},
    {"Off", "關閉"},
    {"Hold Ctrl and point at any control to see what it does and its shortcut. Ctrl+Alt shows a command's scripting parameters.",
     "按住 Ctrl，將游標移至任何控制項上，即可查看其功能和快速鍵。按 Ctrl+Alt 可查看命令的指令碼參數。"},
    {"How long to point at an icon-only button (or a clipped label) before its name appears.",
     "游標停留在僅含圖示的按鈕（或遭截斷的標籤）上多久後，才會顯示其名稱。"},
    {"Opening a sub-palette closes the others in that palette. Shift+click opens more than one.",
     "開啟子調色盤時，會關閉同一調色盤中的其他子調色盤。按住 Shift 並按一下，可同時開啟多個。"},
    {"Opening a palette in the left tray collapses the others when they would not fit.", "在左側工具匣開啟調色盤時，若空間不足，其他調色盤會自動收合。"},
    {"Opening a palette in the right tray collapses the others when they would not fit.", "在右側工具匣開啟調色盤時，若空間不足，其他調色盤會自動收合。"},
    {"Mirror the shelves and trays: tools on the left, navigation on the right.", "鏡像排列工具列和工具匣：左側放工具，右側放導覽控制項。"},
    {"How big the brush tiles on the bottom shelf are. Small leaves the most room for the model.",
     "設定底部工具列上筆刷圖示的大小。選擇小型可為模型保留最多空間。"},
    // --- Preferences > Input ---
    {"Style", "操作風格"},
    {"Blender", "Blender"},
    {"Maya", "Maya"},
    {"SculptForge", "SculptForge"},
    {"Emulate Numpad", "模擬數字鍵盤"},
    {"Right click opens the Quick Menu", "按滑鼠右鍵開啟快速選單"},
    {"Hold Space and drag to move the camera", "按住空白鍵並拖曳以移動相機"},
    {"Start strokes off the model", "在模型外開始筆畫"},
    {"Strokes start off the model", "筆畫從模型外開始"},
    {"Toggle strokes off the model", "切換在模型外開始筆畫"},
    {"Cursor follows surface", "游標沿著表面移動"},
    {"Hide ring while stroking", "繪製時隱藏圓環"},
    {"Outlines", "輪廓線"},
    {"Don't show again", "不再顯示"},
    {"Hints", "提示"},
    {"Show hints in the viewport", "在視埠中顯示提示"},
    {"Show all hints again", "重新顯示所有提示"},
    {"Previewing an LOD", "正在預覽 LOD"},
    {"Face landmark steps", "臉部特徵點步驟"},
    {"Subtle", "細微"},
    {"Outline opacity", "輪廓線不透明度"},
    {"Viewport cursor", "視埠游標"},
    {"Crosshair", "十字線"},
    {"Arrow", "箭頭"},
    {"Import Blender keymap", "匯入 Blender 快速鍵配置"},
    {"Quick Menu", "快速選單"},
    // --- Preferences > Hotkeys ---
    {"Filter", "篩選"},
    {"press a key", "按下任意按鍵"},
    {"Default shortcuts", "預設快速鍵"},
    // --- Preferences > Display, Library, Tablet, Performance, Updates, Autosave ---
    {"Show at startup", "啟動時顯示"},
    {"Show Library at startup", "啟動時顯示資源庫"},
    {"Placement", "位置"},
    {"Top", "上方"},
    {"Bottom", "下方"},
    {"Rows", "列數"},
    {"Library folder", "資源庫資料夾"},
    {"Use the default folder", "使用預設資料夾"},
    {"Mouse & pen", "滑鼠與筆"},
    {"Mouse mode (relative)", "滑鼠模式（相對）"},
    {"Mouse mode", "滑鼠模式"},
    {"Pressure curve", "壓力曲線"},
    {"Pressure test", "壓力測試"},
    {"Pressure meter", "壓力計"},
    {"Tablet defaults", "繪圖板預設值"},
    {"Tilt  X %+.0f\xc2\xb0   Y %+.0f\xc2\xb0", "傾斜  X %+.0f\xc2\xb0   Y %+.0f\xc2\xb0"},
    {"Min", "最小值"},
    {"Max", "最大值"},
    {"Minimum", "最小值"},
    {"Maximum", "最大值"},
    {"Full at", "達到滿值"},
    {"Full pressure at", "達到最大壓力"},
    {"Curve", "曲線"},
    {"Pen", "筆"},
    {"Keyboard", "鍵盤"},
    {"Bring the pen close to the tablet. Pressure works through the system's tablet driver.", "將筆靠近繪圖板。壓力感應由系統的繪圖板驅動程式提供。"},
    {"Bring the pen close to the tablet. Wacom: turn on \"Use Windows Ink\" in Wacom Tablet Properties > Mapping; Huion / XP-Pen: enable Windows Ink in the driver.",
     "將筆靠近繪圖板。Wacom：在 Wacom Tablet Properties > Mapping 中啟用 \"Use Windows Ink\"；Huion / XP-Pen：在驅動程式中啟用 Windows Ink。"},
    {"SpaceMouse (experimental)", "SpaceMouse（實驗性功能）"},
    {"Use SpaceMouse", "使用 SpaceMouse"},
    {"SpaceMouse speed", "SpaceMouse 速度"},
    {"SpaceMouse defaults", "SpaceMouse 預設值"},
    {"Speed", "速度"},
    {"Dead zone", "死區"},
    {"Dominant axis", "主要軸"},
    {"Lock rotation to turntable", "將旋轉鎖定為轉盤模式"},
    {"Plug it in. If 3DxWare is installed it may keep the device to itself: see the Navigation page of the guide.",
     "請接上裝置。如果已安裝 3DxWare，它可能會獨占此裝置；請參閱指南中的「導覽」頁面。"},
    {"Push the cap: if the bars below stay still, another program (3DxWare, spacenavd) holds the SpaceMouse. See the Navigation page of the guide.",
     "推動控制帽：如果下方的指示條沒有反應，表示 SpaceMouse 正被其他程式（3DxWare、spacenavd）占用。請參閱指南中的「導覽」頁面。"},
    {"No joysticks or game controllers seen.", "找不到搖桿或遊戲控制器。"},
    {"Plug it in. SculptForge reads it as a joystick (/dev/input): you need read access to it, and spacenavd may hold it. See the Navigation page of the guide.",
     "請接上裝置。SculptForge 會將它辨識為搖桿（/dev/input）：你需要有讀取權限，而且它可能正被 spacenavd 占用。請參閱指南中的「導覽」頁面。"},
    {"Antialiasing", "抗鋸齒"},
    {"Anti-aliasing", "抗鋸齒"},
    {"Fast Navigation", "快速導覽"},
    {"Multithreading", "多執行緒"},
    {"Predictive Strokes", "預測筆畫"},
    {"Draft Strokes", "草稿筆畫"},
    {"%d threads", "%d 個執行緒"},
    {"Memory", "記憶體"},
    {"Hardware profile", "硬體設定檔"},
    {"Frame Governor", "影格速率調節器"},
    {"Re-calibrate", "重新校準"},
    {"Keep vertical sync on (frame rate capped at the display's)", "保持垂直同步（影格速率上限為螢幕更新率）"},
    {"Every %d min", "每 %d 分鐘"},
    {"Autosave off", "自動儲存已關閉"},
    {"Save a recovery copy this often (0 = off). Restored after a crash.", "依此間隔儲存復原副本（0 = 關閉）。當程式當機後可還原。"},
    // --- Library ---
    {"Image", "影像"},
    {"Recent", "最近使用"},
    {"Nothing here yet.", "目前沒有內容。"},
    {"Nothing here yet. Projects you save (Ctrl+S) into the User folder show up here.", "目前沒有內容。儲存（Ctrl+S）至 User 資料夾的專案會顯示在這裡。"},
    {"Nothing here yet. Save Snapshot keeps timestamped copies of your scene here.", "目前沒有內容。使用「儲存快照」後，場景的時間戳記副本會保存在這裡。"},
    {"Nothing here yet. Files you open show up here.", "目前沒有內容。開啟的檔案會顯示在這裡。"},
    {"Nothing here yet. Put reference images in the User folder.", "目前沒有內容。請將參考影像放入 User 資料夾。"},
    {"Nothing here yet. Put Gaussian splat captures (.ply, .splat) in the User folder.",
     "目前沒有內容。請將 Gaussian splat 擷取檔（.ply、.splat）放入 User 資料夾。"},
    {"Back", "上一頁"},
    {"Forward", "下一頁"},
    {"Open folder", "開啟資料夾"},
    {"Close Library", "關閉資源庫"},
    {"Relief from image", "從影像建立浮雕"},
    {"Inflate image", "膨脹影像"},
    {"Project onto model", "投影至模型"},
    {"Use as alpha", "用作 Alpha"},
    {"Use as Alpha", "用作 Alpha"},
    {"Use as VDM", "用作 VDM"},
    {"Show in file manager", "在檔案管理員中顯示"},
    {"Refresh thumbnail", "重新整理縮圖"},
    {"Nothing here matches \"%s\".", "找不到符合「%s」的項目。"},
    {"Library rows", "資源庫列數"},
    {"Library at the top", "資源庫置於上方"},
    {"Library at the bottom", "資源庫置於下方"},
    {"Show the items whose names contain this text.", "只顯示名稱包含這段文字的項目。"},
    {"Show only the items whose names contain this text.", "只顯示名稱包含這段文字的項目。"},
    {"The previous tab or folder.", "返回上一個分頁或資料夾。"},
    {"Forward again after going back.", "返回後再次前進。"},
    {"Hide the Library (comma toggles it).", "隱藏資源庫（按逗號鍵切換顯示）。"},
    {"How many rows of thumbnails the Library shows (1-4).", "設定資源庫中縮圖的列數（1-4）。"},
    {"Your projects (.sfproj) and recently opened projects.", "你的專案（.sfproj）及最近開啟的專案。"},
    {"Models to start from or append as Parts: primitives and model files.", "可作為起始模型或新增為部件的模型：基本幾何體與模型檔案。"},
    {"Every brush: built-in, custom and .sfbrush files in your library.", "所有筆刷：內建筆刷、自訂筆刷，以及資源庫中的 .sfbrush 檔案。"},
    {"Brush alphas: built-in stamps and images in your Alphas folder.", "筆刷 Alpha：內建印章及 Alphas 資料夾中的影像。"},
    {"Viewport materials: Studio and the Lit Spheres.", "視埠材質：Studio 和 Lit Spheres。"},
    {"Reference images: generate 3D, relief, inflate, project onto the model or use as an alpha.",
     "參考影像：生成 3D 模型、建立浮雕、膨脹影像、投影至模型，或用作 Alpha。"},
    {"Gaussian splat captures (.ply, .splat) to sculpt against.", "用於對照雕刻的 Gaussian splat 擷取檔（.ply、.splat）。"},
    {"Snapshots and the autosave, newest first.", "快照與自動儲存檔，最新的排在最前面。"},
    {"Recently opened files of every kind.", "最近開啟的各類檔案。"},
    {"Add a folder of your own to the Library (shown at the root of every tab).", "將自己的資料夾新增至資源庫（會顯示在每個分頁的根目錄）。"},
    {"Browse projects, models, brushes, alphas, materials, images and splats. Double-click to load; Shift+double-click a model to append it as a Part; drag sideways to scrub.",
     "瀏覽專案、模型、筆刷、Alpha、材質、影像和 splat。按兩下即可載入；對模型按 Shift+兩下可將其新增為部件；左右拖曳即可快速瀏覽。"},
    // --- Pickers and brushes ---
    {"Brush picker", "筆刷選取器"},
    {"Alpha picker", "Alpha 選取器"},
    {"Community brushes", "社群筆刷"},
    {"Save to Library", "儲存至資源庫"},
    {"Strength", "強度"},
    {"Falloff", "衰減"},
    {"Name", "名稱"},
    // --- Common words and buttons ---
    {"Apply", "套用"},
    {"Reset", "重設"},
    {"Preview", "預覽"},
    {"Finish", "完成"},
    {"Close", "關閉"},
    {"OK", "確定"},
    {"Yes", "是"},
    {"No", "否"},
    {"Lock", "鎖定"},
    {"Live", "即時"},
    {"Mean", "平均值"},
    {"Source", "來源"},
    {"Method", "方法"},
    {"Weight", "權重"},
    {"Review", "審閱"},
    {"Network", "網路"},
    {"Contents", "內容"},
    {"Video", "影片"},
    {"Surface", "表面"},
    {"World", "世界"},
    {"Vertex", "頂點"},
    {"Front", "前方"},
    {"Rear", "後方"},
    {"Sharpness", "銳利度"},
    {"Padding", "內距"},
    {"Map size", "貼圖大小"},
    {"Map Size", "貼圖大小"},
    {"Output folder", "輸出資料夾"},
    {"Output Folder", "輸出資料夾"},
    {"Open in browser", "在瀏覽器中開啟"},
    {"Open Folder", "開啟資料夾"},
    // --- Project: unsaved changes, recovery, updates ---
    {"Clear Recent", "清除最近使用項目"},
    {"Autosave (min)", "自動儲存（分鐘）"},
    {"Save changes to %s before continuing?", "繼續之前要將變更儲存至 %s 嗎？"},
    {"Your changes will be lost if you don't save them.", "如果不儲存，變更將會遺失。"},
    {"Don't Save", "不要儲存"},
    {"SculptForge did not close normally last time.", "SculptForge 上次未正常關閉。"},
    {"Restore the autosaved work?", "要還原自動儲存的工作內容嗎？"},
    {"What happened is in sculptforge-previous.log (Help > Open log folder);", "詳細記錄請見 sculptforge-previous.log（說明 > 開啟記錄資料夾）；"},
    {"please attach it if you report the problem.", "回報問題時請附上此檔案。"},
    {"Restore", "還原"},
    {"Discard", "捨棄"},
    {"SculptForge %s is available", "SculptForge %s 已推出"},
    {"(you have %s)", "（你目前使用的是 %s）"},
    {"What's new", "版本新功能"},
    {"Download", "下載"},
    {"Later", "稍後"},
    {"Skip this version", "略過此版本"},
    {"Checking", "正在檢查"},
    {"This build gets its updates from your package manager.", "此版本的更新由你的套件管理員提供。"},
    // --- Sculpt Timeline ---
    {"Sculpt Timeline", "雕刻時間軸"},
    {"Step %zu of %zu", "第 %zu 步，共 %zu 步"},
    {"Step %d: %s", "第 %d 步：%s"},
    {"Save with project", "與專案一併儲存"},
    {"Start of the history", "歷史記錄起點"},
    {"Show the model here", "在此顯示模型"},
    {"Sculpting from an earlier step starts a branch;\nthe later steps stay as a lane above.",
     "從較早的步驟開始雕刻會建立分支；\n後續步驟則會保留在上方的時間軸軌道中。"},
    {"The history of the active Part (%zu steps up to the cursor) played as a turntable.", "以轉盤方式播放目前部件的歷史記錄（游標之前共 %zu 步）。"},
    {"Length", "長度"},
    {"Rendering timelapse", "正在算繪縮時影片"},
    // --- Color, Vertex Paint, paint layers ---
    {"Fill", "填滿"},
    {"Fill layer", "填滿圖層"},
    {"Fill masked", "填滿遮罩區域"},
    {"Fill the unmasked area (the whole mesh if nothing is masked)", "填滿未遮罩區域（若沒有任何遮罩，則填滿整個網格）"},
    {"Cavity", "凹陷"},
    {"Darken creases and cavities (respects the mask)", "加深皺褶與凹陷（遵循遮罩）"},
    {"Background", "背景"},
    {"Shading", "著色"},
    {"Scene", "場景"},
    {"Colour", "色彩"},
    {"Base colour", "基礎色彩"},
    {"Paint brush (P)", "繪製筆刷 (P)"},
    {"Paint into", "繪製到"},
    {"Paint layer", "繪製圖層"},
    {"Painting on: %s", "正在繪製於：%s"},
    {"Painting on: texture, %s", "正在繪製於：紋理，%s"},
    {"Layer channel", "圖層通道"},
    {"Channel view", "通道檢視"},
    {"RGB while sculpting", "雕刻時使用 RGB"},
    {"Min Opacity", "最低不透明度"},
    {"+ Add", "+ 新增"},
    {"Group %d", "群組 %d"},
    {"Group", "群組"},
    {"(Ctrl paints, Ctrl+Alt erases)", "（Ctrl 繪製，Ctrl+Alt 擦除）"},
    {"(Ctrl masks, Ctrl+Alt unmasks)", "（Ctrl 遮罩，Ctrl+Alt 取消遮罩）"},
    // --- More actions (shelves, Quick Menu, palettes) ---
    {"Shelves", "工具列"},
    {"Frame", "置中顯示"},
    {"Intensity at cursor", "游標處的強度"},
    {"Falloff at cursor", "游標處的衰減"},
    {"Last stroke", "最後一筆"},
    {"To Surface", "到表面"},
    {"Crop Box", "裁切框"},
    {"Make Mesh", "建立網格"},
    {"Delete Last", "刪除最後一個"},
    {"Make sculptable", "轉為可雕刻"},
    {"Sculpt to Splats", "雕刻轉為 Splats"},
    {"Preview Normal Map", "預覽法線貼圖"},
    {"AA Off", "關閉抗鋸齒"},
    {"AA 2x", "抗鋸齒 2x"},
    {"AA 4x", "抗鋸齒 4x"},
    {"AA 8x", "抗鋸齒 8x"},
    {"Pan X", "X 軸平移"},
    {"Pan Y", "Y 軸平移"},
    {"Roll", "翻滾"},
    {"Tilt", "傾斜"},
    {"Spin", "旋轉"},
    // --- Extract, Project, Deformation ---
    {"E Smt", "E Smt"},
    {"Parts too", "連同其他部件"},
    {"This Part", "此部件"},
    {"Move along normals", "沿法線移動"},
    {"Mirror left and right", "左右鏡像"},
    {"Flip X", "翻轉 X 軸"},
    {"Flip Y", "翻轉 Y 軸"},
    {"Flip Z", "翻轉 Z 軸"},
    {"Swap Y/Z", "交換 Y/Z 軸"},
    {"Squash", "壓縮"},
    {"Fit plane", "擬合平面"},
    {"Edge Fade", "邊緣淡化"},
    {"Base Smoothing", "基礎平滑"},
    {"All pieces", "所有部分"},
    {"Face loops", "面環"},
    {"Low-poly", "低多邊形"},
    {"Base mesh", "基礎網格"},
    // --- Bake ---
    {"Baking", "烘焙"},
    {"AO rays", "AO 光線"},
    {"AO reach", "AO 範圍"},
    {"Auto distance", "自動距離"},
    {"Max Frontal Distance", "最大正面距離"},
    {"Max Rear Distance", "最大背面距離"},
    {"Normal map convention", "法線貼圖慣例"},
    {"Export preset", "匯出預設集"},
    {"EXR displacement", "EXR 位移貼圖"},
    {"UV view", "UV 檢視"},
    // --- Facial shapes, Make sculptable, phone, splats ---
    {"Generate Facial Shapes", "生成臉部形狀"},
    {"Record face take", "錄製臉部表演"},
    {"Play Take", "播放錄製"},
    {"Load Take", "載入錄製"},
    {"Bake Take", "烘焙錄製"},
    {"Clear Baked Take", "清除已烘焙的錄製"},
    {"Cancel Bake", "取消烘焙"},
    {"Take", "錄製"},
    {"Live axes", "即時坐標軸"},
    {"Landmarks", "特徵點"},
    {"Place Landmarks", "放置特徵點"},
    {"Auto Landmarks", "自動放置特徵點"},
    {"Clear Landmarks", "清除特徵點"},
    {"Place by hand", "手動放置"},
    {"Good enough - stop", "夠好了 - 停止"},
    {"Try a test head", "試用測試頭部"},
    {"Face test", "臉部測試"},
    {"From scan", "從掃描"},
    {"Keep scan", "保留掃描"},
    {"Generate from a photo at once", "立即從照片生成"},
    {"Capture from view", "從檢視擷取"},
    {"Pairing code", "配對碼"},
    {"QR code", "QR 碼"},
    {"Unreal is on another computer", "Unreal 在另一台電腦上"},
    {"Receive automatically", "自動接收"},
    {"Update after each stroke", "每次筆觸後更新"},
    {"Record a video of the run (needs ffmpeg)", "錄製執行過程影片（需要 ffmpeg）"},
    {"Choose Brush", "選擇 Brush"},
    {"Choose COLMAP", "選擇 COLMAP"},
    {"Photos Folder", "照片資料夾"},
    {"Find again", "重新尋找"},
    // --- VDM, Kelvinlet, brush editing ---
    {"Create VDM", "建立 VDM"},
    {"Load VDM", "載入 VDM"},
    {"New VDM Plane", "新增 VDM 平面"},
    {"VDM name", "VDM 名稱"},
    {"Plane Res", "平面解析度"},
    {"Normal tilt", "法線傾斜"},
    {"Detail Clone", "細節複製"},
    {"Clone Spacing", "複製間距"},
    {"Clone follows stroke", "複製跟隨筆畫"},
    {"Refine the selected brush", "調整選取的筆刷"},
    {"Preset amount", "預設數量"},
    {"preset amount", "預設數量"},
    {"Reset to 1", "重置為 1"},
    {"Scatter", "散佈"},
    {"Loop", "循環"},
    {"Letter index", "字母索引"},
    {"Load image", "載入圖片"},
    {"Invert loaded image", "反轉載入的圖片"},
    {"Dark is high", "暗部高"},
    // --- App.cpp (tools/i18n/coverage.py) ---
    {"Strokes go into the base shape, not a layer.", "筆畫會進入基底形狀，而不是圖層。"},
    {"Select to sculpt into this layer. With subdivision levels, record at any level: a layer keeps its own detail on "
     "every level and follows Lower Level / Higher Level and Divide.",
     "選取後即可雕刻到此圖層。有細分層級時，可在任何層級錄製：圖層在每個層級都保有自己的細節，並跟隨降低層級／提高層"
     "級與細分。"},
    {"Animate this blend shape 0 -> 1 -> 0 (Stop sets it back to 1).",
     "播放此混合形狀 0 -> 1 -> 0 的動畫（停止會將它設回 1）。"},
    {"Delete this layer on every subdivision level (undoable).", "在每個細分層級刪除此圖層（可復原）。"},
    {"How strongly the layer shows: 0 = off, 1 = as sculpted. One weight for every subdivision level.",
     "圖層顯示的強度：0 = 關閉，1 = 雕刻時的樣子。所有細分層級共用一個權重。"},
    {"Add a sculpt layer with the name typed beside it; strokes go into it.",
     "以旁邊輸入的名稱新增雕刻圖層；筆畫會進入此圖層。"},
    {"Hugging Face token", "Hugging Face 權杖"},
    {"Free at huggingface.co/settings/tokens (read access). Raises the free daily allowance in Generate 3D, and "
     "TRELLIS.2 needs it. Empty = the HF_TOKEN environment variable.",
     "可在 huggingface.co/settings/tokens 免費取得（讀取權限）。可提高「生成 3D 模型」的每日免費額度，TRELLIS.2 也需"
     "要它。留空 = 使用 HF_TOKEN 環境變數。"},
    {"Load a built-in test capture (a stylised head as Gaussian splats, with a few floaters) to try Make sculptable "
     "without a scan of your own. splat_test_capture kind=object loads a mug instead.",
     "載入內建的測試擷取（以高斯 Splat 呈現的風格化頭部，帶有少量浮點），不需要自己的掃描即可試用「轉為可雕刻」。"
     "splat_test_capture kind=object 則載入馬克杯。"},
    // --- Config.cpp (tools/i18n/coverage.py) ---
    {"Close Holes", "封閉孔洞"},
    {"Cap every open boundary of the active Part (a hole, an open bottom, the rim of an extracted shell) with a new "
     "patch sized like the polygons around it, so remeshing and sculpting see a closed surface. The rest of the Part "
     "stays as it is; the patches get a face group of their own (#103).",
     "以新的面片封閉目前部件的每個開放邊界（孔洞、開放的底部、擷取外殼的邊緣），面片大小與周圍多邊形相近，讓重新網格"
     "化與雕刻看到封閉的表面。部件的其餘部分保持不變；面片會有自己的面群組 (#103)。"},
    {"Add a subdivision level: four times the polygons, and the lower levels are kept, so you can step down to shape "
     "big forms and back up to the detail. Quads stay quads (Catmull-Clark). Smt off divides without smoothing.",
     "新增一個細分層級：多邊形變為四倍，並保留較低層級，讓你能回到低層級塑造大形，再回到細節。四邊面維持四邊面 "
     "(Catmull-Clark)。關閉 Smt 時細分不會平滑。"},
    {"Smooth the next Divide (on by default). Off: the new level keeps the hard edges and flat faces of the one below.",
     "平滑下一次細分（預設開啟）。關閉：新層級保留下層的硬邊與平面。"},
    {"Lower Level", "降低層級"},
    {"Step one subdivision level down (fewer polygons, for big forms). The detail of the higher levels is kept.",
     "降低一個細分層級（較少多邊形，用於大形）。較高層級的細節會保留。"},
    {"Higher Level", "提高層級"},
    {"Step one subdivision level up (more polygons, for detail). Edits made below carry the detail along.",
     "提高一個細分層級（較多多邊形，用於細節）。在下層所做的編輯會帶著細節一起變化。"},
    {"Delete Lower", "刪除較低層級"},
    {"Delete Higher", "刪除較高層級"},
    {"Store the surface above the highest level as micro displacements: hundreds of millions of sculptable triangles "
     "in a fraction of the memory (300M take about 0.4 GB). The Micro slider sets how fine.",
     "將最高層級之上的表面儲存為微細分位移：數億個可雕刻三角面，只佔一小部分記憶體（3 億個約 0.4 GB）。「微細分」滑桿"
     "設定精細程度。"},
    {"Turn the micro level into a regular mesh in a new Part (for export or tools that need real geometry).",
     "將微細分層級轉為新部件中的一般網格（用於匯出或需要真實幾何的工具）。"},
    {"Write the micro level as a 32-bit EXR displacement map over the base UVs (next to the project).",
     "將微細分層級寫成基底 UV 上的 32 位元 EXR 位移貼圖（放在專案旁）。"},
    {"Duplicate Part", "複製部件"},
    {"Copy the active Part, with its layers and attributes, and make the copy active (also in the Parts list's "
     "right-click menu).",
     "複製目前的部件及其圖層與屬性，並將複本設為作用中（部件清單的右鍵選單中也有）。"},
    {"Make a shell from the masked part of the Part (a helmet, a strap, clothing), shown as a preview below it. "
     "Change Thick, E Smt or Double and it updates; Accept keeps it as a new Part.",
     "從部件的遮罩區域建立外殼（頭盔、帶子、衣物），並在下方顯示預覽。變更厚度、E Smt 或雙倍時會即時更新；按「接受」"
     "即保留為新部件。"},
    {"Keep the Extract preview as a new Part below this one. Ctrl+Z takes it away again.",
     "將擷取預覽保留為此部件下方的新部件。按 Ctrl+Z 可再移除。"},
    {"Grow the extracted shell both inward and outward from the surface, instead of outward only.",
     "讓擷取的外殼從表面同時向內與向外長出，而不只是向外。"},
    {"Triangle corners: where the extracted edge turns sharply, the rim is made of triangles instead of quads.",
     "三角形角落：擷取邊緣急轉之處，邊緣改用三角面而非四邊面。"},
    {"Triangle border: the whole rim of the extracted shell is made of triangles instead of quads.",
     "三角形邊緣：擷取外殼的整圈邊緣都改用三角面而非四邊面。"},
    {"Mirror the + side of the Part onto the - side across the chosen axis and weld the seam into one watertight "
     "mesh. Face Groups and vertex paint come along. To keep the other side, Deformation > Mirror first.",
     "沿所選軸將部件的 + 側鏡像到 - 側，並將接縫焊接成一個封閉網格。面群組與頂點繪製也會一併處理。若要保留另一側，請"
     "先使用變形 > 鏡像。"},
    {"Flip the Part across the chosen axis, through the origin (the plane symmetry and Mirror And Weld use).",
     "沿所選軸經由原點翻轉部件（對稱與鏡像並焊接所用的平面）。"},
    {"An axis Mirror And Weld mirrors across (several: one after the other).",
     "鏡像並焊接所沿的軸（多個時：依序處理）。"},
    {"An axis Mirror flips along (several: one after the other).", "鏡像翻轉所沿的軸（多個時：依序處理）。"},
    {"New from Starter Mesh...", "從起始網格新增..."},
    {"Start from a base mesh: human head, bust, full body, hand, foot, ear, eye, stylised and creature heads, torso, "
     "or a bevelled box, cylinder, capsule or ring (Library > Tool > Starter meshes).",
     "從基礎網格開始：人頭、半身像、全身、手、腳、耳朵、眼睛、風格化與生物頭部、軀幹，或倒角方塊、圓柱、膠囊或圓環（"
     "資源庫 > 工具 > 起始網格）。"},
    {"Open a project (.sfproj), a model (.obj, .glb, .fbx, .stl, .ply, .usd ...) or splats.",
     "開啟專案 (.sfproj)、模型 (.obj、.glb、.fbx、.stl、.ply、.usd ...) 或 Splat。"},
    {"Save the project (.sfproj: every Part, layer, brush and setting). Asks for a name the first time.",
     "儲存專案（.sfproj：每個部件、圖層、筆刷與設定）。第一次會詢問名稱。"},
    {"Save the project under a new name.", "以新名稱儲存專案。"},
    {"Export the model as glTF binary, with sculpt layers as blend shapes (morph targets).",
     "將模型匯出為二進位 glTF，雕刻圖層會成為混合形狀 (morph target)。"},
    {"Save the visible Parts as one binary STL, for 3D printing.", "將可見部件儲存為單一二進位 STL，用於 3D 列印。"},
    {"Export a game-ready FBX and import it into the running Unreal Editor (Python remote execution).",
     "匯出適用於遊戲的 FBX，並匯入執行中的 Unreal Editor（Python 遠端執行）。"},
    {"Send the visible Parts back to Blender (the SculptForge add-on): objects that came from Blender update in "
     "place, keeping their transform, materials and modifiers; new Parts become new objects.",
     "將可見部件送回 Blender（SculptForge 附加元件）：來自 Blender 的物件會就地更新，保留其變換、材質與修改器；新部件"
     "則成為新物件。"},
    {"Load what Blender last sent with the add-on's Send to SculptForge. Each object updates its Part (undoable) or "
     "becomes a new one; this happens by itself while Receive automatically is on.",
     "載入 Blender 最後透過附加元件的 Send to SculptForge 傳送的內容。每個物件會更新對應的部件（可復原）或成為新部件"
     "；開啟「自動接收」時會自動進行。"},
    {"Send to Maya", "傳送到 Maya"},
    {"Send the visible Parts to Maya (OBJ with quads and UVs) through the exchange folder you chose; Get from "
     "SculptForge on Maya's SculptForge shelf loads them, replacing the objects they came from by name.",
     "透過你選擇的交換資料夾將可見部件傳送到 Maya（含四邊面與 UV 的 OBJ）；Maya 的 SculptForge 工具架上的 Get from "
     "SculptForge 會載入它們，並依名稱取代原本的物件。"},
    {"Get from Maya", "從 Maya 取得"},
    {"Load what Maya last sent with the shelf's Send to SculptForge. Each object replaces the Part with its name "
     "(undoable) or becomes a new one.",
     "載入 Maya 最後透過工具架的 Send to SculptForge 傳送的內容。每個物件會取代同名的部件（可復原）或成為新部件。"},
    {"Exchange folder...", "交換資料夾..."},
    {"Choose the folder SculptForge and Maya exchange meshes through (pick the same one in Maya's shelf script). "
     "SculptForge reads and writes only there, and only when you send or get.",
     "選擇 SculptForge 與 Maya 交換網格的資料夾（在 Maya 的工具架指令碼中選同一個）。SculptForge 只在該處讀寫，且只在"
     "你傳送或取得時進行。"},
    {"Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file.",
     "加入剪貼簿中的筆刷：別人分享的筆刷代碼，或 .sfbrush 檔案的文字。"},
    {"Every brush keeps its own Size, Intensity, Falloff, Colour intensity, stroke and alpha while you work. This "
     "saves the current brush's, so it starts with them every time SculptForge opens.",
     "工作時每個筆刷都保有自己的大小、強度、衰減、色彩強度、筆畫與 Alpha。此功能會儲存目前筆刷的設定，讓它在每次開啟 "
     "SculptForge 時都以此開始。"},
    {"Put the current brush's Size, Intensity, Falloff, Colour intensity, stroke and alpha back to the values "
     "SculptForge ships with, and forget what Save Brush Settings stored for it.",
     "將目前筆刷的大小、強度、衰減、色彩強度、筆畫與 Alpha 還原為 SculptForge 的預設值，並捨棄「儲存筆刷設定」為它儲"
     "存的內容。"},
    {"On: Size and Intensity stay as they are when you pick another brush (as before 0.10.8); Falloff, Colour "
     "intensity, the stroke and the alpha still follow each brush. Off (the default): every brush keeps its own.",
     "開啟：切換到其他筆刷時大小與強度維持不變（與 0.10.8 之前相同）；衰減、色彩強度、筆畫與 Alpha 仍跟隨各筆刷。關閉"
     "（預設）：每個筆刷保有自己的設定。"},
    {"Make the brush 10% smaller (hold to repeat; Shift+[ steps one pixel).",
     "將筆刷縮小 10%（按住可重複；Shift+[ 每次一個像素）。"},
    {"Make the brush 10% larger (hold to repeat; Shift+] steps one pixel).",
     "將筆刷放大 10%（按住可重複；Shift+] 每次一個像素）。"},
    {"Mirror strokes across the X axis (left / right).", "沿 X 軸鏡像筆畫（左／右）。"},
    {"Show the mesh's polygons (quads and triangles) over the surface.",
     "在表面上顯示網格的多邊形（四邊面與三角面）。"},
    {"Fit the whole model in the view.", "將整個模型置入視圖。"},
    {"Take the paint colour from the model under the cursor.", "從游標下的模型取用繪製色彩。"},
    {"Every shortcut and mouse gesture, generated from the commands themselves.",
     "所有快速鍵與滑鼠手勢，直接由命令本身產生。"},
    {"The SculptForge user guide: every tool explained, with search. Hold Ctrl over a control and press F1 to open "
     "the chapter about it.",
     "SculptForge 使用指南：說明每項工具，並可搜尋。按住 Ctrl 停在控制項上再按 F1，即可開啟相關章節。"},
    {"Your first sculpt (10 min)", "你的第一件雕刻（10 分鐘）"},
    {"A step-by-step lesson for complete beginners: a cute owl head from the default sphere, painted and rendered, in "
     "about ten minutes.",
     "給完全新手的逐步教學：從預設球體做出可愛的貓頭鷹頭部，上色並算繪，約十分鐘完成。"},
    {"Quick tour", "快速導覽"},
    {"Ten short slides for sculptors who know another app: where remeshing, dynamic topology, retopology, levels, "
     "masking, cut, brushes and baking are, each with a Show me button.",
     "給熟悉其他軟體的雕刻者的十張簡短投影片：重新網格化、動態拓樸、重新拓樸、層級、遮罩、切割、筆刷與烘焙在哪裡，每"
     "張都有「示範給我看」按鈕。"},
    {"What changed in this version and the ones before it (opens once by itself after an update).",
     "此版本與先前版本的變更（更新後會自動開啟一次）。"},
    {"Ask the SculptForge website whether a newer version is out. SculptForge also checks by itself once a day "
     "(Preferences > Updates).",
     "向 SculptForge 網站確認是否有新版本。SculptForge 每天也會自動檢查一次（偏好設定 > 更新）。"},
    {"Version, licence and the open-source components SculptForge is built with.",
     "版本、授權，以及 SculptForge 所使用的開放原始碼元件。"},
    {"SculptForge is free and stays free. A membership adds betas and early builds, Member Requests (ask for a brush "
     "or an alpha), a vote on what we build next and your name in the credits: opens sculptforge.app/support.html in "
     "your browser. Nothing in the app changes either way.",
     "SculptForge 免費且將一直免費。成為會員可獲得 Beta 版與搶先版本、Member Requests（請求筆刷或 Alpha）、投票決定下"
     "一步開發內容，以及名列致謝名單：會在瀏覽器中開啟 sculptforge.app/support.html。無論如何，應用程式本身都不會有任"
     "何改變。"},
    {"The official SculptForge community on Discord: questions, tips and work in progress. Opens the invite in your "
     "browser. Bugs still go to Report a problem (GitHub issues).",
     "SculptForge 官方 Discord 社群：提問、技巧與進行中的作品。會在瀏覽器中開啟邀請連結。錯誤回報仍請使用「回報問題」"
     "(GitHub issues)。"},
    {"The welcome note shown the first time SculptForge starts.", "第一次啟動 SculptForge 時顯示的歡迎訊息。"},
    {"Open the SculptForge website and user guide in your browser.", "在瀏覽器中開啟 SculptForge 網站與使用指南。"},
    {"Open the issue tracker in your browser.", "在瀏覽器中開啟問題追蹤系統。"},
    {"Open the folder with sculptforge.log: what SculptForge did and, after a crash, what crashed. "
     "sculptforge-previous.log beside it is the last session's. Attach both to a problem report.",
     "開啟含 sculptforge.log 的資料夾：記錄 SculptForge 做了什麼，當機後則記錄是什麼當機。旁邊的 "
     "sculptforge-previous.log 是上一次工作階段的記錄。回報問題時請附上這兩個檔案。"},
    {"Record a performance trace", "錄製效能追蹤"},
    {"While on, sculptforge-trace.log in the log folder records what makes SculptForge slow: every frame over 33 ms "
     "with where its time went, strokes, uploads to the GPU, remeshes and other background jobs, the mesh size and "
     "levels, and the GPU and driver. Turn it on, do what is slow, turn it off and attach the file to your report. A "
     "small file on this computer only; off by default and costs nothing when off.",
     "開啟時，記錄資料夾中的 sculptforge-trace.log 會記錄讓 SculptForge 變慢的原因：每個超過 33 ms 的影格及其時間花在"
     "哪裡、筆畫、上傳到 GPU、重新網格化與其他背景工作、網格大小與層級，以及 GPU 與驅動程式。開啟它、執行變慢的操作、"
     "關閉後將檔案附加到回報中。檔案很小且只存在這台電腦；預設關閉，關閉時不會有任何負擔。"},
    {"Submit a request for a fix or a suggestion: opens the bug report and feature request forms in your browser.",
     "提交修正請求或建議：會在瀏覽器中開啟錯誤回報與功能請求表單。"},
    {"Search every command, brush and setting by name and run it. Commands that need parameters open in the console.",
     "依名稱搜尋任何命令、筆刷與設定並執行。需要參數的命令會在主控台中開啟。"},
    {"The script console: every command you run appears as a script line, and you can type commands yourself.",
     "指令碼主控台：你執行的每個命令都會以一行指令碼顯示，你也可以自行輸入命令。"},
    {"Run a text file of script statements, one per line (# starts a comment).",
     "執行包含指令碼陳述式的文字檔，每行一個（# 開頭為註解）。"},
    {"Put every command run this session on the clipboard as a script, ready to replay (a macro recorder).",
     "將本次工作階段執行過的所有命令以指令碼形式放到剪貼簿，可直接重播（巨集錄製器）。"},
    {"Empty the console log (the session script is kept).", "清空主控台記錄（保留工作階段指令碼）。"},
    {"Save Layout", "儲存版面配置"},
    {"Keep the current layout (shelves, trays, open palettes) for future sessions. Preferences and shortcuts save "
     "themselves.",
     "保留目前的版面配置（工具列、工具匣、開啟的調色盤）供之後的工作階段使用。偏好設定與快速鍵會自動儲存。"},
    {"Save Preferences Now", "立即儲存偏好設定"},
    {"Write the preferences and shortcuts to disk now. Not needed: they save themselves a moment after every change.",
     "立即將偏好設定與快速鍵寫入磁碟。通常不需要：每次變更後它們會在片刻後自動儲存。"},
    {"Reset Layout and Shortcuts", "重設版面配置與快速鍵"},
    {"Go back to the built-in layout, shortcuts and radial menus. The shortcuts save themselves; Save Layout keeps "
     "the layout.",
     "回到內建的版面配置、快速鍵與圓餅選單。快速鍵會自動儲存；「儲存版面配置」會保留版面配置。"},
    {"Revert to Saved Layout", "還原為已儲存的版面配置"},
    {"Go back to the layout the last Save Layout kept.", "回到上一次「儲存版面配置」所保留的版面配置。"},
    {"Import Interface...", "匯入界面..."},
    {"Load a layout and shortcuts file someone shared (or you saved with Export Interface).",
     "載入別人分享（或你用「匯出界面」儲存）的版面配置與快速鍵檔案。"},
    {"Export Interface...", "匯出界面..."},
    {"Save the layout, shortcuts and interface preferences to a file you can share.",
     "將版面配置、快速鍵與界面偏好設定儲存為可分享的檔案。"},
    {"Rearrange the shelves: Ctrl+Alt+drag any button onto a shelf to add or move it, or off the shelves to remove it.",
     "重新排列工具列：按住 Ctrl+Alt 將任何按鈕拖到工具列上即可新增或移動，拖出工具列即可移除。"},
    {"The short notes SculptForge shows over the 3D view (a mode that is on, the next step of a tool). Off hides them "
     "all; each also has its own Don't show again.",
     "SculptForge 在 3D 視圖上顯示的簡短提示（已開啟的模式、工具的下一步）。關閉會全部隱藏；每則提示也各有「不再顯示"
     "」。"},
    {"Customize Shelves", "自訂工具列"},
    {"Preferences and shortcuts save themselves. Layout changes last until you quit; Save Layout keeps them.",
     "偏好設定與快速鍵會自動儲存。版面配置的變更會持續到你結束程式為止；「儲存版面配置」會保留它們。"},
    {"The language of menus, palettes, shelves and messages. Brush and tool names (Quad Remesh, Forge Spheres, Clay "
     "Block...) stay as they are.",
     "選單、調色盤、工具列與訊息所使用的語言。筆刷與工具名稱（Quad Remesh、Forge Spheres、Clay Block...）保持原樣。"},
    {"Ctrl+hover help", "Ctrl+停留說明"},
    {"Help box opacity %.2f", "說明框不透明度 %.2f"},
    {"Help box opacity", "說明框不透明度"},
    {"How solid the Ctrl-help box is.", "Ctrl 說明框的不透明程度。"},
    {"Hover and focus highlights change colour only: no easing, and tiles don't lift or grow.",
     "停留與焦點的醒目提示只改變顏色：沒有緩動，圖塊也不會浮起或放大。"},
    {"Off: File and Edit come first in the palette bar and Help last, the rest A to Z. On: every palette A to Z.",
     "關閉：調色盤列中「檔案」與「編輯」在最前、「說明」在最後，其餘依 A 至 Z 排列。開啟：所有調色盤依 A 至 Z 排列。"},
    {"Size of text and controls, applied at once. Automatic follows the display the window is on (Windows: Settings > "
     "Display > Scale, per monitor).",
     "文字與控制項的大小，立即套用。「自動」會依視窗所在的顯示器而定（Windows：設定 > 顯示器 > 縮放，依各顯示器）。"},
    {"Free: dragging turns the model about the screen's axes and can roll it over the top; press Shift while rotating "
     "to snap to the nearest front, side, top or bottom view. Turntable: the model stays upright and turns about its "
     "vertical axis, as in Blender or Maya.",
     "自由：拖曳會以螢幕的軸旋轉模型，並可從上方翻轉；旋轉時按住 Shift 可貼齊最近的正面、側面、頂部或底部視圖。轉盤："
     "模型保持直立並繞垂直軸旋轉，如同 Blender 或 Maya。"},
    {"Keeps the interface below the camera notch of MacBooks (and other laptops) when SculptForge fills the screen. "
     "Automatic asks the system (macOS) or recognises a notched MacBook display (Linux).",
     "SculptForge 全螢幕時，讓界面保持在 MacBook（及其他筆電）相機瀏海下方。「自動」會詢問系統 (macOS) 或辨識有瀏海的"
     " MacBook 螢幕 (Linux)。"},
    {"Click, then press the new key. Esc cancels, Backspace clears. Orange: changed from the default.",
     "按一下，再按下新的按鍵。Esc 取消，Backspace 清除。橘色：已與預設值不同。"},
    {"Undo every shortcut change.", "復原所有快速鍵變更。"},
    {"Open Library on the Project tab when SculptForge starts.", "啟動 SculptForge 時在「專案」分頁開啟資源庫。"},
    {"How many rows of thumbnails the Library strip shows.", "資源庫列顯示幾列縮圖。"},
    {"Library opens along the top edge of the canvas.", "資源庫沿畫布上緣開啟。"},
    {"Library opens above the bottom shelf.", "資源庫在底部工具列上方開啟。"},
    {"Where your projects, tools, brushes, alphas, images, splats and Snapshots live. Library shows it as User.",
     "你的專案、工具、筆刷、Alpha、影像、Splat 與快照存放的位置。資源庫中顯示為「使用者」。"},
    {"Colours", "色彩"},
    {"Forge Terminal's colour scheme, from well-known open-source terminal themes.",
     "Forge Terminal 的配色，取自知名的開放原始碼終端機佈景主題。"},
    {"Uses your Omarchy theme's colours when Omarchy is installed (read from its theme folder; SculptForge never "
     "changes it). After switching themes, toggle this to pick up the new colours.",
     "安裝 Omarchy 時使用你的 Omarchy 佈景主題色彩（從其佈景主題資料夾讀取；SculptForge 不會修改它）。切換佈景主題後"
     "，重新切換此選項即可套用新色彩。"},
    {"Faint scanlines and a glow around the focused panel, on the interface only (never on the 3D view). Off by "
     "default.",
     "淡淡的掃描線與焦點面板周圍的光暈，只套用於界面（不會出現在 3D 視圖）。預設關閉。"},
    {"The borders around the panels in Forge Terminal, Phosphor and Blueprint. Subtle (the default): faint, and "
     "nothing lights up under the pointer. Off: no borders, the gaps alone separate the panels. On: clear borders, "
     "the panel under the pointer a faint step lighter.",
     "Forge Terminal、Phosphor 與 Blueprint 中面板周圍的邊框。細微（預設）：淡淡的，指標下不會亮起。關閉：沒有邊框，"
     "僅以間隙分隔面板。開啟：清楚的邊框，指標下的面板略亮一階。"},
    {"How strong the panel borders are (0-100%), on top of Outlines: 100% draws them as Subtle or On does, lower "
     "fades them into the panels (#98). Ctrl+click to type.",
     "面板邊框的強度 (0-100%)，以「輪廓線」為基礎：100% 會以細微或開啟的樣子繪製，較低則淡入面板 (#98)。按住 Ctrl 並"
     "按一下即可輸入。"},
    {"SculptForge layout", "SculptForge 版面配置"},
    {"Menus by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help), a status bar and a one-row tool shelf. "
     "Every palette is a section of one menu, and F3 finds them all.",
     "依工作分類的選單（檔案、編輯、雕刻、繪製、網格、檢視、算繪、說明）、狀態列與單列工具列。每個調色盤都是某個選單"
     "中的一節，按 F3 可找到全部。"},
    {"Classic", "經典"},
    {"Classic layout", "經典版面配置"},
    {"The alphabetical palette bar and the two-row top shelf.", "依字母排列的調色盤列與雙列頂部工具列。"},
    // --- Console.cpp (tools/i18n/coverage.py) ---
    {"Hide the console. The log and the session script are kept.", "隱藏主控台。記錄與工作階段指令碼會保留。"},
    {"Runs SculptForge script: one command per line or separated by ';', parameters as name=value (quote text with "
     "spaces). Tab completes, Up/Down recalls, help lists every command.",
     "執行 SculptForge 指令碼：每行一個命令或以 ';' 分隔，參數寫成 名稱=值（含空格的文字請加引號）。Tab 自動完成，上"
     "／下鍵叫回先前命令，help 列出所有命令。"},
    // --- Gizmo.cpp (tools/i18n/coverage.py) ---
    {"Draw mode: brushes sculpt and paint (leaves the Gizmo).", "繪製模式：筆刷進行雕刻與繪製（離開操縱器）。"},
    {"Gizmo, Move: drag an arrow to move along an axis, a square to move in a plane, the centre to move in the view. "
     "Masked parts stay; Shift snaps to the grid; Alt+drag moves only the gizmo. W again: back to Draw.",
     "操縱器，移動：拖曳箭頭沿軸移動，拖曳方塊在平面上移動，拖曳中心在視圖中移動。遮罩部分保持不動；Shift 貼齊格線；"
     "Alt+拖曳只移動操縱器。再按 W：回到繪製。"},
    {"Gizmo, Scale: drag a box to stretch along an axis or the centre to scale evenly (right or up: bigger). Shift "
     "snaps to 10% steps. E again: back to Draw.",
     "操縱器，縮放：拖曳方塊沿軸拉伸，拖曳中心等比例縮放（向右或向上：放大）。Shift 以 10% 為一級貼齊。再按 E：回到繪"
     "製。"},
    {"Gizmo, Rotate: drag a ring to turn about that axis, the outer ring to turn in the view. Mask the body and put "
     "the pivot at a joint to pose a limb. Shift snaps to 15 degrees. R again: back to Draw.",
     "操縱器，旋轉：拖曳圓環繞該軸旋轉，拖曳外環在視圖中旋轉。遮罩身體並將樞紐點放在關節上即可擺出肢體姿勢。Shift 以 "
     "15 度貼齊。再按 R：回到繪製。"},
    {"Put the gizmo back at the centre of what moves (the Part, or its unmasked part) with world axes.",
     "將操縱器放回移動對象（部件或其未遮罩部分）的中心，並使用世界軸。"},
    {"Put the pivot on the surface: click this, then click the model (or Alt+click the model at any time). With "
     "Surface orientation the axes follow the surface there (Y along the normal).",
     "將樞紐點放到表面上：按一下此處，再按一下模型（或隨時 Alt+按一下模型）。使用「表面」方向時，軸會跟隨該處的表面（"
     "Y 沿法線）。"},
    {"The gizmo's axes are the world's X, Y and Z.", "操縱器的軸為世界的 X、Y 與 Z。"},
    {"The gizmo's axes follow the surface where the pivot was snapped (Y along the normal) and turn with your "
     "rotations, for moving a part along itself.",
     "操縱器的軸會跟隨樞紐點貼齊處的表面（Y 沿法線），並隨你的旋轉而轉動，方便沿部件本身移動。"},
    {"Masked parts stay where they are and partly masked parts move partly (on by default). Off: the whole Part moves "
     "whatever the mask.",
     "遮罩部分維持原位，部分遮罩的區域則部分移動（預設開啟）。關閉：無論遮罩如何，整個部件都會移動。"},
    {"Transform every visible Part together. Each Part gets its own undo step.",
     "一起變換所有可見部件。每個部件各有自己的復原步驟。"},
    {"Where the gizmo sits: rotation and scaling turn about this point. It follows the centre of what moves until you "
     "place it: Alt+drag the gizmo, Alt+click the model, To Surface, or type here.",
     "操縱器所在的位置：旋轉與縮放以此點為中心。在你放置它之前，它會跟隨移動對象的中心：Alt+拖曳操縱器、Alt+按一下模"
     "型、「到表面」，或在此輸入。"},
    {"A soft joint for posing: the movement eases in over this distance from the masked part (a share of the Part's "
     "size), so a hard mask edge bends smoothly instead of creasing. 0: the mask as it is.",
     "擺姿勢用的柔和關節：移動會在距遮罩部分這段距離內逐漸生效（以部件大小的比例表示），讓生硬的遮罩邊緣平順彎曲而不"
     "產生摺痕。0：維持遮罩原樣。"},
    {"Distance to move along X, Y and Z (the gizmo's axes) when you press Apply.",
     "按「套用」時沿 X、Y、Z（操縱器的軸）移動的距離。"},
    {"Degrees to turn about X, then Y, then Z (the gizmo's axes, through the pivot) when you press Apply.",
     "按「套用」時依序繞 X、Y、Z（操縱器的軸，通過樞紐點）旋轉的角度。"},
    {"Size to scale to about the pivot when you press Apply (100% = unchanged).",
     "按「套用」時以樞紐點為中心縮放到的大小（100% = 不變）。"},
    {"Move, rotate and scale by the numbers above, about the pivot, as one undo step (the transform command). Masked "
     "parts stay; with symmetry on, a masked transform is mirrored.",
     "依上方數值以樞紐點為中心移動、旋轉與縮放，作為單一復原步驟（transform 命令）。遮罩部分保持不動；開啟對稱時，遮"
     "罩變換會鏡像。"},
    {"Set Move and Rotate back to 0 and Scale to 100%.", "將移動與旋轉設回 0，比例設回 100%。"},
    // --- Input.cpp (tools/i18n/coverage.py) ---
    {"An Intensity slider under the cursor: drag it, or type a number (1-100) and press Enter. Hold Shift (or pick "
     "Smooth) to set Smooth's own Intensity.",
     "游標下的強度滑桿：拖曳它，或輸入數字 (1-100) 後按 Enter。按住 Shift（或選取 Smooth）可設定 Smooth 自己的強度。"},
    {"Colour intensity at cursor", "游標處的色彩強度"},
    {"An Colour intensity slider under the cursor: how strongly painting colours the surface (1-100).",
     "游標下的色彩強度滑桿：繪製為表面上色的強度 (1-100)。"},
    {"A Falloff slider under the cursor: below 0 a broad, hard brush tip, above 0 a soft point (-100..100). The inner "
     "ring of the brush cursor shows it.",
     "游標下的衰減滑桿：低於 0 為寬而硬的筆尖，高於 0 為柔和的尖端 (-100..100)。筆刷游標的內環會顯示它。"},
    {"The Quick Menu under the cursor: brush, stroke, alpha, material, colour, Size, Falloff and the intensities. "
     "Hold Space and let go to close it, or tap Space to keep it open.",
     "游標下的快速選單：筆刷、筆畫、Alpha、材質、色彩、大小、衰減與各項強度。按住空白鍵後放開即可關閉，或輕按空白鍵讓"
     "它保持開啟。"},
    {"Steady Stroke", "穩定筆畫"},
    {"Steady Stroke on or off, with every stroke type (Dots, Drag Stamp, Slide Stamp, Spray): the brush trails the "
     "cursor on a string and only moves once the string is taut, for steady lines without hand shake.",
     "開啟或關閉穩定筆畫，適用所有筆畫類型（Dots、Drag Stamp、Slide Stamp、Spray）：筆刷以一條線跟在游標後方，線拉緊"
     "時才移動，畫出不受手抖影響的穩定線條。"},
    {"Catch up on release", "放開時追上"},
    {"Steady Stroke: when you let go, the line runs on to where the cursor is, instead of stopping a string's length "
     "behind it.",
     "穩定筆畫：放開時，線條會延伸到游標所在處，而不是停在落後一條線長的位置。"},
    {"Keep the brush size in model units (mm) instead of screen pixels: it stays the same on the surface as you zoom "
     "in and out, for pores, wrinkles and trims of an exact width.",
     "以模型單位 (mm) 而非螢幕像素保持筆刷大小：縮放時在表面上維持相同大小，適合精確寬度的毛孔、皺紋與飾邊。"},
    {"Make the brush one pixel smaller (half a pixel below 2 px), down to 0.5 px.",
     "將筆刷縮小一個像素（低於 2 px 時為半個像素），最小 0.5 px。"},
    {"Make the brush one pixel larger (half a pixel below 2 px).", "將筆刷放大一個像素（低於 2 px 時為半個像素）。"},
    {"Continuous on or off: while you hold the brush still on the model it keeps adding, 30 dabs a second (build up a "
     "bump without moving). Not for Move, Tendril or the drag strokes.",
     "開啟或關閉連續：筆刷在模型上靜止時持續加入，每秒 30 個筆觸（不移動也能堆出凸起）。不適用於 Move、Tendril 與拖曳"
     "類筆畫。"},
    {"Swap the main and the secondary colour.", "交換主要色彩與次要色彩。"},
    {"Mask the whole visible Part.", "遮罩整個可見部件。"},
    {"Unmask everything (also: Ctrl+drag on the background, away from the model).",
     "取消所有遮罩（也可以：在遠離模型的背景上 Ctrl+拖曳）。"},
    {"Mask what is unmasked and unmask what is masked (also: Ctrl+click the background).",
     "遮罩未遮罩的部分並取消已遮罩的部分（也可以：Ctrl+按一下背景）。"},
    {"Group from Mask", "從遮罩建立群組"},
    {"Turn the masked part into a new face group and clear the mask. With nothing masked, everything visible becomes "
     "one group.",
     "將遮罩部分轉為新的面群組並清除遮罩。若沒有遮罩，所有可見部分會成為一個群組。"},
    {"One face group per UV island: triangles that touch without a UV seam share a group. Only the visible part is "
     "regrouped. Needs UVs (Tool > Texture Map > Auto UV makes some).",
     "每個 UV 島一個面群組：相鄰且無 UV 接縫的三角面屬於同一群組。只重新分組可見部分。需要 UV（工具 > 貼圖 > 自動 UV "
     "可產生）。"},
    {"Classic sculpting keys", "經典雕刻按鍵"},
    {"The classic sculpting keys and gestures: I is Colour intensity, 9 saves a snapshot, the number keys don't pick "
     "brushes, and Ctrl+drag on the background only clears the mask.",
     "經典的雕刻按鍵與手勢：I 為色彩強度，9 儲存快照，數字鍵不選取筆刷，在背景上 Ctrl+拖曳只會清除遮罩。"},
    {"SculptForge's own keys (the default): 1-9 and 0 pick the first ten brushes, I is Falloff (as is O), U "
     "Intensity, and Ctrl+drag on the background zooms when nothing is masked.",
     "SculptForge 自己的按鍵（預設）：1-9 與 0 選取前十個筆刷，I 為衰減（O 亦同），U 為強度，沒有遮罩時在背景上 "
     "Ctrl+拖曳會縮放。"},
    {"Blender's navigation and sculpt-mode keys: middle-drag orbits (Shift pans, Ctrl zooms), Alt+left-drag does the "
     "same for pens and trackpads, Ctrl inverts a stroke, Shift smooths, F / Shift+F set radius and strength, V S P "
     "I G C K pick brushes and M the mask brush, Home frames everything. Rotation switches to Turntable.",
     "Blender 的導覽與雕刻模式按鍵：中鍵拖曳環繞（Shift 平移，Ctrl 縮放），Alt+左鍵拖曳在繪圖筆與觸控板上效果相同，"
     "Ctrl 反轉筆畫，Shift 平滑，F／Shift+F 設定半徑與強度，V S P I G C K 選取筆刷，M 為遮罩筆刷，Home 將全部置入視圖"
     "。旋轉會切換為轉盤。"},
    {"Maya's view controls: Alt+left-drag orbits, Alt+middle-drag pans, Alt+right-drag zooms, anywhere on the canvas; "
     "hold Alt, then add Ctrl, and a left drag rolls the view. Alt+F frames the Part, Alt+H resets the view, the "
     "wheel zooms toward the pointer. Ctrl inverts a stroke, Ctrl+Alt masks (Ctrl+Alt+drag on the background clears "
     "the mask), Shift smooths, Ctrl+Shift hides and shows. The Classic sculpting keys stay for everything else.",
     "Maya 的視圖控制：在畫布任何位置 Alt+左鍵拖曳環繞、Alt+中鍵拖曳平移、Alt+右鍵拖曳縮放；按住 Alt 再加 Ctrl，左鍵"
     "拖曳可滾轉視圖。Alt+F 將部件置入視圖，Alt+H 重設視圖，滾輪朝指標縮放。Ctrl 反轉筆畫，Ctrl+Alt 遮罩（在背景上 "
     "Ctrl+Alt+拖曳清除遮罩），Shift 平滑，Ctrl+Shift 隱藏與顯示。其餘一切沿用經典雕刻按鍵。"},
    {"Blender style: the number keys above the letters work as the numpad (1 front, 3 right, 7 top, 5 "
     "orthographic...), for keyboards without one.",
     "Blender 風格：字母上方的數字鍵當作數字鍵台使用（1 正面、3 右側、7 頂部、5 正交...），適用於沒有數字鍵台的鍵盤。"},
    {"A right click on the canvas, without dragging, opens the Quick Menu under the cursor (Space does too). A right "
     "drag still turns, pans or zooms the view. A pen button set to Right click works the same.",
     "在畫布上按一下右鍵（不拖曳）會在游標下開啟快速選單（空白鍵也可以）。右鍵拖曳仍會旋轉、平移或縮放視圖。設為「右"
     "鍵按一下」的繪圖筆按鈕效果相同。"},
    {"While Space is held, a drag on the canvas moves the camera: left-drag turns it, right- or middle-drag pans, and "
     "with Ctrl held too a drag zooms (#43). Off by default: with the Classic sculpting keys, Space opens the Quick "
     "Menu (with this on, a right click still does). Typing a space in a text field is never taken.",
     "按住空白鍵時，在畫布上拖曳會移動相機：左鍵旋轉，右鍵或中鍵平移，同時按住 Ctrl 則縮放 (#43)。預設關閉：使用經典"
     "雕刻按鍵時，空白鍵會開啟快速選單（開啟此選項後，右鍵仍可開啟）。在文字欄位中輸入的空格不會被攔截。"},
    {"Emulate 3 Button Mouse", "模擬三鍵滑鼠"},
    {"Blender style: Alt+left-drag always turns the view (Shift pans, Ctrl zooms), as Blender's Emulate 3 Button "
     "Mouse preference does, for pens and trackpads. Off (as Blender's default keymap, 5.1 and later): Alt+left-drag "
     "on the model masks and Ctrl+Alt+left-drag unmasks; beside the model it still turns the view (#77).",
     "Blender 風格：Alt+左鍵拖曳一律旋轉視圖（Shift 平移，Ctrl 縮放），如同 Blender 的 Emulate 3 Button Mouse 偏好設"
     "定，適用於繪圖筆與觸控板。關閉（同 Blender 5.1 以後的預設按鍵配置）：在模型上 Alt+左鍵拖曳為遮罩，Ctrl+Alt+左鍵"
     "拖曳為取消遮罩；在模型旁邊仍會旋轉視圖 (#77)。"},
    {"A left-drag that starts on the background begins a brush stroke instead of turning the view; the brush touches "
     "the model once the cursor reaches it (#62). Ctrl (mask) and Shift (smooth) work the same way, and a Ctrl+click "
     "on the background still inverts the mask. The view still turns with a right-drag or a pen button set to Right "
     "click, pans with Alt+drag or a middle-drag, and moves with Space held if Hold Space is on; with Maya keys, "
     "Alt+left-drag orbits, with Blender keys beside the model. Shift+L (View > Lock camera) turns it on and off.",
     "從背景開始的左鍵拖曳會開始筆畫而非旋轉視圖；游標碰到模型時筆刷才接觸模型 (#62)。Ctrl（遮罩）與 Shift（平滑）也"
     "一樣，在背景上 Ctrl+按一下仍會反轉遮罩。視圖仍可用右鍵拖曳或設為「右鍵按一下」的繪圖筆按鈕旋轉，用 Alt+拖曳或中"
     "鍵拖曳平移，開啟「按住空白鍵」時按住空白鍵移動；使用 Maya 按鍵時 Alt+左鍵拖曳環繞，使用 Blender 按鍵時在模型旁"
     "環繞。Shift+L（檢視 > 鎖定相機）可切換開關。"},
    {"Lock camera", "鎖定相機"},
    {"Lock the camera for left-drags: a drag that starts on the background sculpts once it reaches the model instead "
     "of turning the view (Preferences > Input > Start strokes off the model, #62). The status bar shows CAMERA "
     "LOCKED while it is on. Right-drag or Alt+drag still move the view. Shift+L turns it on and off.",
     "鎖定相機以處理左鍵拖曳：從背景開始的拖曳在碰到模型時會開始雕刻，而不是旋轉視圖（偏好設定 > 輸入 > 在模型外開始"
     "筆畫，#62）。開啟時狀態列會顯示「相機已鎖定」。右鍵拖曳或 Alt+拖曳仍可移動視圖。Shift+L 可切換開關。"},
    {"The brush ring lies on the model under the pointer: a circle at the brush's real size there, turned to the form "
     "of the surface (averaged over the brush, so fine bumps don't make it wobble), with a short line along the "
     "surface's direction. With symmetry on, a dot shows where each mirrored or radial copy lands. Off the model, "
     "and with this off, the ring is a flat circle on the screen.",
     "筆刷圓環貼在指標下的模型上：以該處筆刷實際大小畫出的圓，依表面形狀傾斜（以筆刷範圍平均，細小凹凸不會讓它晃動）"
     "，並有一條沿表面方向的短線。開啟對稱時，會以圓點顯示每個鏡像或放射複本的落點。在模型外或關閉此選項時，圓環是螢"
     "幕上的平面圓。"},
    {"Cursor", "游標"},
    {"While a stroke runs, show only the dot where the brush touches (and the symmetry dots), not the ring: nothing "
     "between you and the surface you are shaping. Off by default: the ring shows the brush size throughout.",
     "筆畫進行時只顯示筆刷接觸點（與對稱點），不顯示圓環：你與正在塑造的表面之間不會有任何遮擋。預設關閉：圓環會一直"
     "顯示筆刷大小。"},
    {"Use the keys from a keymap you exported in Blender (Preferences > Keymap > Export, a .py file). Switches to the "
     "Blender style, then takes the navigation and sculpt keys SculptForge has an equivalent for. The file is only "
     "read, never run. Saved automatically.",
     "使用你從 Blender 匯出的按鍵配置（偏好設定 > Keymap > Export，.py 檔案）。會切換到 Blender 風格，並採用 "
     "SculptForge 有對應功能的導覽與雕刻按鍵。檔案只會讀取，絕不執行。自動儲存。"},
    {"Paint the mask with a plain left drag, Ctrl+drag to unmask (Blender's Mask brush). Press it again, or pick "
     "another brush, to go back.",
     "以左鍵拖曳繪製遮罩，Ctrl+拖曳取消遮罩（Blender 的 Mask 筆刷）。再按一次或選取其他筆刷即可返回。"},
    {"Hide the face group under the cursor (Blender: hide the face set).",
     "隱藏游標下的面群組（Blender：隱藏 face set）。"},
    {"Show only the face group under the cursor; again, with it isolated, shows everything (Blender: toggle "
     "visibility).",
     "只顯示游標下的面群組；在已獨立顯示時再按一次則顯示全部（Blender：切換可見性）。"},
    {"Show every hidden part of the mesh.", "顯示網格所有隱藏的部分。"},
    {"Look at the model from the front (orthographic).", "從正面看模型（正交）。"},
    {"Look at the model from behind (orthographic).", "從背面看模型（正交）。"},
    {"Look at the model's right side (orthographic).", "看模型的右側（正交）。"},
    {"Look at the model's left side (orthographic).", "看模型的左側（正交）。"},
    {"Look down on the model (orthographic).", "從上方俯視模型（正交）。"},
    {"Look up at the model from below (orthographic).", "從下方仰視模型（正交）。"},
    {"Gizmo size: how big the X / Y / Z axes and the View Figure are in the corner of the canvas. Grows with the UI "
     "scale.",
     "操縱器大小：畫布角落的 X／Y／Z 軸與視圖人偶的大小。隨界面縮放而放大。"},
    {"Switch between a perspective and an orthographic view (no foreshortening: sizes don't shrink with depth).",
     "在透視與正交視圖之間切換（無透視縮短：大小不隨深度縮小）。"},
    {"Turn the view 15 degrees to the left around the model.", "繞模型將視圖向左旋轉 15 度。"},
    {"Turn the view 15 degrees to the right around the model.", "繞模型將視圖向右旋轉 15 度。"},
    {"Turn the view 15 degrees up over the model.", "將視圖向上越過模型旋轉 15 度。"},
    {"Turn the view 15 degrees down under the model.", "將視圖向下繞到模型下方旋轉 15 度。"},
    {"Look at the model from the other side.", "從另一側看模型。"},
    {"Move the view closer.", "將視圖拉近。"},
    {"Move the view back.", "將視圖拉遠。"},
    {"Fit the active Part in the view (Blender: View Selected; Maya keys: Alt+F).",
     "將目前的部件置入視圖（Blender：View Selected；Maya 按鍵：Alt+F）。"},
    {"Reset view", "重設視圖"},
    {"Back to the view a new model opens with: from the front, in perspective, the whole model in view (Maya keys: "
     "Alt+H).",
     "回到開啟新模型時的視圖：正面、透視、整個模型在畫面中（Maya 按鍵：Alt+H）。"},
    {"Zoom drag", "縮放拖曳"},
    {"Sideways (Maya)", "左右（Maya）"},
    {"Up and down", "上下"},
    {"Alt+right drag zooms. Sideways (the default, as Maya's own dolly): drag right to zoom in, left to zoom out. Up "
     "and down: drag up to zoom in, as the SculptForge style.",
     "Alt+右鍵拖曳可縮放。左右（預設，如 Maya 的推拉）：向右拖曳拉近，向左拖曳拉遠。上下：向上拖曳拉近，如 "
     "SculptForge 風格。"},
    {"Invert zoom", "反轉縮放"},
    {"Turn zoom drags around (the mouse wheel stays as it is).", "反轉縮放拖曳方向（滑鼠滾輪維持不變）。"},
    {"Wheel zooms toward the pointer", "滾輪朝指標縮放"},
    {"The mouse wheel zooms toward the point of the model under the pointer, which stays under it, instead of the "
     "middle of the view. On with Maya keys (choosing a style sets it), off with the others.",
     "滑鼠滾輪會朝指標下的模型位置縮放，該點維持在指標下，而不是朝視圖中央。使用 Maya 按鍵時開啟（選擇風格時會自動設"
     "定），其他風格則關閉。"},
    {"Along the normal", "沿法線"},
    {"Always", "一律"},
    {"Never", "永不"},
    {"Move, Tendril, Elastic Tendril and KelvinGrab: with the invert key held as the stroke begins (Alt; Ctrl in the "
     "Blender and Maya styles), the surface moves along the average normal under the brush instead of across the "
     "screen, so it pulls straight out of the surface or pushes straight in. Always: every stroke of those brushes "
     "does; Never: the key does nothing for them.",
     "Move、Tendril、Elastic Tendril 與 KelvinGrab：筆畫開始時按住反轉鍵（Alt；在 Blender 與 Maya 風格中為 Ctrl），表"
     "面會沿筆刷下的平均法線移動，而非沿螢幕移動，因此會從表面直直拉出或推入。一律：這些筆刷的每一筆都如此；永不：該"
     "按鍵對它們沒有作用。"},
    {"Share size and intensity across brushes", "筆刷之間共用大小與強度"},
    {"Free", "自由"},
    {"Free: the model turns about the screen's axes and can roll over the top. Turntable: it stays upright, as in "
     "Blender or Maya.",
     "自由：模型以螢幕的軸旋轉，並可從上方翻轉。轉盤：模型保持直立，如同 Blender 或 Maya。"},
    {"The pointer over the 3D view: a crosshair (the default, as in most sculpting apps; it stays while the view "
     "turns) or the arrow. Panels, menus and popups always show the arrow (or their own resize and text cursors). "
     "Saved automatically.",
     "3D 視圖上的指標：十字線（預設，與大多數雕刻軟體相同；旋轉視圖時仍保留）或箭頭。面板、選單與彈出視窗一律顯示箭頭"
     "（或它們自己的調整大小與文字游標）。自動儲存。"},
    {"Brush radius in screen pixels (0.5 to 800), and in mm on the surface.",
     "以螢幕像素表示的筆刷半徑（0.5 到 800），以及表面上的 mm。"},
    {"Below 0: a broad, hard tip. Above 0: a soft point.", "低於 0：寬而硬的筆尖。高於 0：柔和的尖端。"},
    {"How strongly the brush changes the surface.", "筆刷改變表面的強度。"},
    {"Colour intensity %.0f", "色彩強度 %.0f"},
    {"Colour intensity", "色彩強度"},
    {"How strongly painting colours the surface.", "繪製為表面上色的強度。"},
    // --- LibraryBrowser.cpp (tools/i18n/coverage.py) ---
    {"Library tabs", "資源庫分頁"},
    {"What the Library shows: projects, models, brushes, alphas, materials, images, splats, snapshots, recent files "
     "and packs.",
     "資源庫顯示的內容：專案、模型、筆刷、Alpha、材質、影像、Splat、快照、最近使用的檔案與套件。"},
    {"Hide the Library (comma toggles it; Esc closes it).", "隱藏資源庫（逗號鍵切換；Esc 關閉）。"},
    {"Open the Library on the Project tab when SculptForge starts.", "啟動 SculptForge 時在「專案」分頁開啟資源庫。"},
    {"Rows of thumbnails in the Library strip.", "資源庫列中的縮圖列數。"},
    {"Show the Library along the top or the bottom of the canvas.", "在畫布上方或下方顯示資源庫。"},
    {"Show or hide the Library: thumbnails of your projects, models, brushes, alphas, materials, images, splats and "
     "Snapshots along the top of the canvas. Double-click to load.",
     "顯示或隱藏資源庫：沿畫布上緣排列你的專案、模型、筆刷、Alpha、材質、影像、Splat 與快照的縮圖。按兩下即可載入。"},
    {"Pop up every brush under the cursor. Type the first letter of a brush, then the orange letter on its tile (B S "
     "T = Standard).",
     "在游標下彈出所有筆刷。輸入筆刷的第一個字母，再輸入其圖塊上的橘色字母（B S T = Standard）。"},
    {"Pop up the stroke types: Dots, Drag Stamp, Slide Stamp, Spray and Lazy Stroke.",
     "彈出筆畫類型：Dots、Drag Stamp、Slide Stamp、Spray 與 Lazy Stroke。"},
    {"Pop up the brush alphas: built-in stamps and images from your library.",
     "彈出筆刷 Alpha：內建印章與資源庫中的影像。"},
    {"Pop up the materials under the cursor. Hover one to preview it on the model; click to keep it.",
     "在游標下彈出材質。停在某個材質上即可在模型上預覽；按一下即可套用。"},
    {"Size at cursor", "游標處的大小"},
    {"A Size slider under the cursor: drag it, or type a number and press Enter.",
     "游標下的大小滑桿：拖曳它，或輸入數字後按 Enter。"},
    {"Save a timestamped snapshot of the whole scene to your library (the last 10 are kept). Your project file is not "
     "changed. Find them in the Library's Snapshots tab.",
     "將整個場景加上時間戳記的快照儲存到資源庫（保留最近 10 個）。你的專案檔不會變更。可在資源庫的「快照」分頁找到。"},
    {"Save Render", "儲存算繪"},
    {"Render the view (without the interface) to an image in your library: Library > Image lists it, and the status "
     "bar names the file. Material sets its size (twice the view by default), shape (the view's, 1:1, 16:9, 9:16, "
     "4:3) and format (PNG or JPG).",
     "將視圖（不含界面）算繪為資源庫中的影像：資源庫 > 影像會列出它，狀態列會顯示檔名。「材質」設定其大小（預設為視圖"
     "的兩倍）、比例（視圖、1:1、16:9、9:16、4:3）與格式（PNG 或 JPG）。"},
    // --- MemoryConfirm.cpp (tools/i18n/coverage.py) ---
    {"Not enough free memory", "可用記憶體不足"},
    {"If the computer runs out of memory it can stop responding and SculptForge may close without saving. Close other "
     "apps (or save first) before you go on.",
     "如果電腦記憶體耗盡，可能會停止回應，SculptForge 也可能未儲存就關閉。繼續之前請關閉其他應用程式（或先儲存）。"},
    {"Continue anyway", "仍要繼續"},
    // --- Palettes.cpp (tools/i18n/coverage.py) ---
    {"Brushes subtract instead of add. Holding Alt flips it for one stroke.",
     "筆刷改為減去而非增加。按住 Alt 可在單一筆畫中反轉。"},
    {"Connected only", "僅限相連"},
    {"View falloff", "檢視衰減"},
    {"The Smooth brush (and Shift) removes bumps without shrinking the form, instead of the plain smooth that also "
     "flattens it. Either way, let go of Shift while still pressing a Shift-smooth stroke to switch to the other "
     "smooth for the rest of the stroke.",
     "Smooth 筆刷（與 Shift）會去除凹凸而不使形體縮小，而非同時壓平形體的一般平滑。無論哪種，在 Shift 平滑筆畫進行中"
     "放開 Shift，筆畫剩餘部分就會切換為另一種平滑。"},
    {"The Smooth brush (and Shift) straightens the borders between face groups and leaves everything else alone: a "
     "jagged clothing edge, a cuff or a panel line becomes a clean line along the surface. Corners where three "
     "groups meet stay where they are.",
     "Smooth 筆刷（與 Shift）會拉直面群組之間的邊界，其餘部分不變：鋸齒狀的衣物邊緣、袖口或面板線會變成沿表面的乾淨線"
     "條。三個群組交會的角落保持原位。"},
    {"Brush radius in screen pixels (0.5 to 800), and in mm on the surface under the cursor. [ and ] change it by "
     "10%, Shift+[ and Shift+] by 1 px; Ctrl+Shift+right-drag scrubs it finely.",
     "以螢幕像素表示的筆刷半徑（0.5 到 800），以及游標下表面上的 mm。[ 與 ] 每次調整 10%，Shift+[ 與 Shift+] 每次 1 "
     "px；Ctrl+Shift+右鍵拖曳可細微調整。"},
    {"The brush falloff: below 0 a broad, hard tip, above 0 a soft point. The brush cursor's inner ring shows where "
     "the brush works at half strength.",
     "筆刷衰減：低於 0 為寬而硬的筆尖，高於 0 為柔和的尖端。筆刷游標的內環顯示筆刷以一半強度作用的位置。"},
    {"Off (the default): the triangle size follows the brush size on screen, as in Dynamic Topology. On: one size for "
     "the whole mesh, set by Detail. A preference (saved automatically).",
     "關閉（預設）：三角面大小跟隨螢幕上的筆刷大小，如同動態拓樸。開啟：整個網格使用單一大小，由「細節」設定。這是偏"
     "好設定（自動儲存）。"},
    {"Smooth remesh", "平滑時重新網格化"},
    {"How much Smooth (Shift) changes the topology while Dynamic is on. 100 (the default): it also simplifies, "
     "merging triangles toward twice the size. Lower: it remeshes less. 0: Smooth only smooths and the triangles "
     "stay as they are. A preference (saved automatically).",
     "開啟「動態」時，Smooth (Shift) 改變拓樸的程度。100（預設）：也會簡化，將三角面合併至約兩倍大小。較低：重新網格"
     "化較少。0：Smooth 只平滑，三角面保持不變。這是偏好設定（自動儲存）。"},
    {"The viewport's lens, as on a full-frame camera: short (15-35 mm) exaggerates perspective, long (85-300 mm) "
     "flattens it, which helps judge proportions. Saved with the project; Ctrl+click to type.",
     "視埠的鏡頭，如同全片幅相機：短焦 (15-35 mm) 會誇大透視，長焦 (85-300 mm) 則壓平透視，有助於判斷比例。隨專案儲存"
     "；按住 Ctrl 並按一下即可輸入。"},
    {"Show the X / Y / Z directions in the corner of the canvas.", "在畫布角落顯示 X／Y／Z 方向。"},
    {"View Figure", "視圖人偶"},
    {"Show a small shaded head in the corner of the canvas that turns with the view. Click it to look from the "
     "nearest side (front, back, left, right, top or bottom).",
     "在畫布角落顯示隨視圖轉動的小型著色頭部。按一下即可從最近的一側觀看（正面、背面、左、右、上或下）。"},
    {"Gizmo size", "操縱器大小"},
    {"AI features are off (Preferences > AI): Relief, Inflate and Stamp work offline.",
     "AI 功能已關閉（偏好設定 > AI）：浮雕、膨脹與壓印可離線使用。"},
    {"A sketch, concept, logo, photo or height map (or drop one on the window).",
     "草圖、概念圖、標誌、照片或高度圖（或拖放到視窗中）。"},
    {"Light areas rise: logos, coins, emblems, height maps.", "亮部會隆起：標誌、硬幣、徽章、高度圖。"},
    {"Inflates the outline into a rounded blockout.", "將輪廓膨脹成圓潤的粗胚。"},
    {"Projects the image onto the model from this view as surface detail.",
     "從此視角將影像投射到模型上，成為表面細節。"},
    {"Integrations", "整合"},
    {"Support", "支持"},
    {"X-Ray", "X 光"},
    {"View resolution", "檢視解析度"},
    {"Decimate leaves masked vertices where they are, so masked areas keep their triangles exactly; the rest is "
     "decimated around them.",
     "減面會讓遮罩頂點維持原位，因此遮罩區域完整保留其三角面；其餘部分則在其周圍減面。"},
    {"Remeshing in the background (with a painted texture: Auto UV of the new surface, then the texture projected "
     "onto it). The result replaces the Part when it's done.",
     "正在背景中重新網格化（有繪製貼圖時：先為新表面自動 UV，再將貼圖投射上去）。完成後結果會取代部件。"},
    {"Tool > Remesh", "工具 > 重新網格化"},
    {"Cut", "切割"},
    {"Selection Sets", "選取集"},
    {"Backface culling", "背面剔除"},
    {"Don't draw the back faces of the model: faster on dense meshes, and you see through the openings of open meshes "
     "(a scan, a garment) instead of their inside. Brushes then work only on faces you can see. Off by default; "
     "saved automatically, projects keep their own.",
     "不繪製模型的背面：在高密度網格上更快，且能透過開放網格（掃描、衣物）的開口看出去，而不是看到其內部。此時筆刷只"
     "作用於看得到的面。預設關閉；自動儲存，專案會保有自己的設定。"},
    {"Radial Copies %d", "放射複本 %d"},
    {"Radial Copies", "放射複本"},
    {"How many copies radial symmetry makes of every stroke, turned evenly around the axis (2 to 64). With X, Y or Z "
     "on too, each mirror image gets its copies.",
     "放射對稱為每一筆畫建立的複本數量，平均分布在軸周圍（2 到 64）。同時開啟 X、Y 或 Z 時，每個鏡像也會有自己的複本"
     "。"},
    // --- PanelAi.cpp (tools/i18n/coverage.py) ---
    {"Enable AI features", "啟用 AI 功能"},
    {"Brush Lab: describe a brush in words and AI builds it; tweak the recipe with sliders.",
     "Brush Lab：用文字描述筆刷，AI 就會建立它；再以滑桿微調配方。"},
    {"Turn an image or a description into a sculptable model (Hugging Face, fal.ai, Meshy, ComfyUI or offline).",
     "將影像或描述轉為可雕刻的模型（Hugging Face、fal.ai、Meshy、ComfyUI 或離線）。"},
    // --- PanelBake.cpp (tools/i18n/coverage.py) ---
    {"Bake the chosen maps from the source Part(s), or this Part's own top subdivision or micro level, onto its UVs, "
     "in the background. Maps are saved as PNGs (displacement also as a 32-bit EXR, position as a 16-bit PNG) in the "
     "output folder, one set per UDIM tile when the UVs use them, and the normal map shows on the model.",
     "在背景中將所選貼圖從來源部件，或此部件自己的最高細分層級或微細分層級，烘焙到其 UV 上。貼圖以 PNG 儲存在輸出資料"
     "夾（位移另存為 32 位元 EXR，位置為 16 位元 PNG），UV 使用 UDIM 時每個圖塊一組，法線貼圖會顯示在模型上。"},
    {"Stop the bake that is running. Nothing is written.", "停止執行中的烘焙。不會寫入任何檔案。"},
    {"Show the baked tangent-space normal map on the low-poly in the viewport (decoded with the same MikkTSpace "
     "tangents a game engine uses), to check it against the high-poly. Turn it off to see the bare low-poly.",
     "在視埠中將烘焙好的切線空間法線貼圖顯示在低模上（使用與遊戲引擎相同的 MikkTSpace 切線解碼），以便與高模比對。關"
     "閉即可看到原始低模。"},
    {"Open the folder the last bake wrote its maps to.", "開啟上次烘焙寫入貼圖的資料夾。"},
    {"Choose the folder baked maps are written to. Empty: beside your last export, else the project's folder, else "
     "Bakes in your library folder.",
     "選擇烘焙貼圖的寫入資料夾。留空：上次匯出的位置旁，否則為專案資料夾，再否則為資源庫資料夾中的 Bakes。"},
    {"Maps are baked onto the active Part's UVs: make the low-poly active (it needs UVs from import or from Auto UV "
     "in Tool > Texture Map), and keep the high-poly Part(s) visible.",
     "貼圖會烘焙到目前部件的 UV 上：將低模設為作用中（需要匯入時的 UV，或工具 > 貼圖中的自動 UV），並讓高模部件保持可"
     "見。"},
    {"The high-poly to bake from: one Part, or every visible Part except the active one (the low-poly), or this "
     "Part's own top subdivision level baked onto its lowest level, or its micro level baked onto its base. Hidden "
     "faces of the source are ignored. UVs outside 0..1 bake as UDIM tiles (name.1001.png, ...).",
     "要烘焙的高模來源：單一部件，或除作用中部件（低模）外的所有可見部件，或將此部件的最高細分層級烘焙到最低層級，或"
     "將其微細分層級烘焙到基底。來源的隱藏面會被忽略。0..1 以外的 UV 會烘焙為 UDIM 圖塊（name.1001.png, ...）。"},
    {"Width and height of the maps: 1K, 2K (the default), 4K or 8K. Bake time and memory grow with the area (an 8K "
     "bake holds about 200 MB per map while it runs).",
     "貼圖的寬度與高度：1K、2K（預設）、4K 或 8K。烘焙時間與記憶體隨面積增加（8K 烘焙執行時每張貼圖約佔 200 MB）。"},
    {"AA %.0fx", "抗鋸齒 %.0fx"},
    {"Supersampling: n x n rays per texel, averaged. 1 is fastest; 2 (the default) gives clean edges; 4 costs 16 rays "
     "per texel.",
     "超取樣：每個紋素 n x n 條光線取平均。1 最快；2（預設）邊緣乾淨；4 每個紋素需 16 條光線。"},
    {"Measure how far the high-poly lies in front of and behind the low-poly before baking, and cast rays just that "
     "far. Turn it off to set Front and Rear yourself (e.g. when rays hit a nearby part).",
     "烘焙前先測量高模位於低模前後的距離，並只投射到該距離。關閉即可自行設定前方與後方（例如光線碰到附近的部件時）。"},
    {"How far in front of the low-poly rays start, as a percentage of the model's size (its bounding-box diagonal). "
     "Raise it when details that stick out come out flat or missing.",
     "光線從低模前方多遠開始，以模型大小（其邊界框對角線）的百分比表示。凸出的細節變平或遺失時請調高。"},
    {"How far behind the low-poly rays look, as a percentage of the model's size. Raise it when carved detail is "
     "missing; lower it when rays reach the far side of thin parts.",
     "光線往低模後方看多遠，以模型大小的百分比表示。雕刻凹陷的細節遺失時請調高；光線碰到薄部件另一側時請調低。"},
    {"Texels filled around each UV island with the island's edge colours, so seams don't show once the maps are "
     "mipmapped in an engine. 8 is plenty for 2K; use more for 4K and 8K.",
     "以 UV 島邊緣色彩填滿每個島周圍的紋素，讓貼圖在引擎中產生 mipmap 後不會露出接縫。2K 用 8 就夠；4K 與 8K 請用更多"
     "。"},
    {"OpenGL (green up) suits Blender, Unity, Godot and Substance. DirectX (green down) is Unreal's native "
     "convention; Send to Unreal flips an OpenGL map on import, so either works there.",
     "OpenGL（綠色朝上）適用於 Blender、Unity、Godot 與 Substance。DirectX（綠色朝下）是 Unreal 的原生慣例；「傳送至 "
     "Unreal」會在匯入時翻轉 OpenGL 貼圖，所以兩者在那裡都可用。"},
    {"Rays per texel for ambient occlusion: more is smoother and slower.",
     "環境光遮蔽每個紋素的光線數：越多越平滑也越慢。"},
    {"How far away geometry still occludes, as a percentage of the model's size. Short: only creases darken. Long: "
     "whole cavities and the insides of shapes darken too.",
     "多遠的幾何仍會產生遮蔽，以模型大小的百分比表示。短：只有摺痕變暗。長：整個凹洞與形體內側也會變暗。"},
    {"Where the maps are written, as <Part>_normal.png, _ao.png, _displacement.png (+ .exr) and so on. Empty: beside "
     "your last export, else the project's folder, else Bakes in your library folder.",
     "貼圖的寫入位置，檔名如 <Part>_normal.png、_ao.png、_displacement.png（+ .exr）等。留空：上次匯出的位置旁，否則"
     "為專案資料夾，再否則為資源庫資料夾中的 Bakes。"},
    {"Also write the displacement as a 32-bit float OpenEXR in world units (for renderers that take real distances). "
     "The 16-bit PNG is always written.",
     "另將位移以世界單位寫成 32 位元浮點 OpenEXR（適用於使用實際距離的算繪器）。16 位元 PNG 一律會寫入。"},
    {"Baking on every core in the background; you can keep working.", "正在背景中使用所有核心烘焙；你可以繼續工作。"},
    // --- PanelBlender.cpp (tools/i18n/coverage.py) ---
    {"Load what Blender sends as soon as it arrives (checked once a second). Off: use Receive from Blender.",
     "Blender 傳來的內容一到就載入（每秒檢查一次）。關閉：請使用「從 Blender 接收」。"},
    // --- PanelBrushes.cpp (tools/i18n/coverage.py) ---
    {"Follow stroke direction", "跟隨筆畫方向"},
    {"The alpha turns with the stroke: its top points the way you are drawing, for scales, stitches and hair that "
     "line up along the stroke. Small wobbles are smoothed out (the direction is taken every 20 pixels, as Blender's "
     "Rake does). Angle still adds its turn.",
     "Alpha 會隨筆畫轉動：其頂端指向你繪製的方向，適合沿筆畫排列的鱗片、縫線與毛髮。細小的晃動會被平滑（每 20 像素取"
     "一次方向，如同 Blender 的 Rake）。「角度」仍會加上其旋轉。"},
    {"Turn the alpha by a fixed angle in degrees (counter-clockwise); Ctrl+click to type.",
     "將 Alpha 旋轉固定角度（逆時針，單位為度）；按住 Ctrl 並按一下即可輸入。"},
    {"Angle jitter %.0f", "角度抖動 %.0f"},
    {"The stamp turns with you: with Drag Stamp its top points the way you drag; in a dots stroke it turns along "
     "the stroke. Off, Drag Stamp only sets the size and the stamp keeps Angle's turn. Slide Stamp never turns "
     "while you slide it.",
     "印章會跟著你轉動：使用 Drag Stamp 時，其頂端指向你拖曳的方向；在點狀筆畫中則沿筆畫轉動。關閉時，Drag Stamp 只"
     "設定大小，印章保持「角度」的旋轉。Slide Stamp 滑動時永不轉動。"},
    {"Angle jitter", "角度抖動"},
    {"Turn each dab by a random angle, up to this many degrees either way: breaks up repeats in skin, rock and noise "
     "alphas.",
     "將每個筆觸隨機旋轉，向兩側最多此角度：打破皮膚、岩石與雜訊 Alpha 的重複感。"},
    {"Tile across %.0f", "橫向重複 %.0f"},
    {"Tile across", "橫向重複"},
    {"Repeat the alpha this many times across the brush (1 = once).", "在筆刷橫向重複 Alpha 的次數（1 = 一次）。"},
    {"Tile down %.0f", "縱向重複 %.0f"},
    {"Tile down", "縱向重複"},
    {"Repeat the alpha this many times down the brush (1 = once).", "在筆刷縱向重複 Alpha 的次數（1 = 一次）。"},
    // --- PanelCut.cpp (tools/i18n/coverage.py) ---
    {"Depth", "深度"},
    // --- PanelExtract.cpp (tools/i18n/coverage.py) ---
    {"Smooths the extracted edge, which otherwise follows the mask's triangles in steps (0 = as masked).",
     "平滑擷取的邊緣，否則它會沿著遮罩三角面呈階梯狀（0 = 維持遮罩原樣）。"},
    {"Thickness of the shell, as a fraction of the Part's size (0.015 is 1.5%). Negative grows it inward; 0 makes a "
     "single-sided surface.",
     "外殼的厚度，以部件大小的比例表示（0.015 為 1.5%）。負值向內長；0 會建立單面表面。"},
    {"Deformation > Smooth smooths without shrinking the form (Taubin smoothing) instead of the plain smooth, which "
     "also shrinks it.",
     "變形 > 平滑會在不縮小形體的情況下平滑（Taubin 平滑），而非同時使形體縮小的一般平滑。"},
    // --- PanelFace.cpp (tools/i18n/coverage.py) ---
    {"Give the active Part (a humanoid head, looking down +Z in the front view) the 52 Apple ARKit blend shapes as "
     "sculpt layers: eyeBlinkLeft, jawOpen, mouthSmileRight and the rest, fitted to this head's proportions. Then "
     "fix any of them by sculpting into its layer (Tool > Layers), test them with the presets below, export them as "
     "morph targets (GLB, USD, FBX, Blender) or drive them live from your phone. The landmarks you placed (at least "
     "3) guide the fit; otherwise it finds the face by itself. Eyes, teeth and tongue Parts (by name) and Parts on "
     "the face get the shapes they need. Running it again replaces the 52 layers (undo brings back the old ones and "
     "the fixes sculpted into them).",
     "為目前的部件（人形頭部，在正視圖中朝向 +Z）加入 52 個 Apple ARKit 混合形狀作為雕刻圖層：eyeBlinkLeft、jawOpen、"
     "mouthSmileRight 等，並依此頭部的比例擬合。之後可在其圖層中雕刻修正任何形狀（工具 > 圖層），用下方的預設測試，匯"
     "出為 morph target（GLB、USD、FBX、Blender），或用手機即時驅動。你放置的特徵點（至少 3 個）會引導擬合；否則會自"
     "動找出臉部。眼睛、牙齒與舌頭部件（依名稱）以及臉上的部件會得到所需的形狀。再次執行會取代這 52 個圖層（復原可找"
     "回舊圖層及其中雕刻的修正）。"},
    {"Guide the fit for heads it gets wrong (stylised proportions, a turned head, a bust with big shoulders): click "
     "the eye corners, the nose tip, the mouth corners and the chin on the sculpt, in the order the canvas asks. "
     "Right and left are the character's own (its right eye is on your left in the front view). Click a dot again "
     "later to move it. 3 are enough to start; all 8 fit best. Auto places them all for you to check.",
     "為擬合失準的頭部提供引導（風格化比例、轉向的頭、肩膀寬大的半身像）：依畫布要求的順序，在雕刻上按一下眼角、鼻尖"
     "、嘴角與下巴。左右以角色本身為準（正視圖中角色的右眼在你的左邊）。之後再按一下圓點即可移動。3 個就能開始；8 個"
     "全放擬合最佳。「自動」會替你全部放好供你檢查。"},
    {"Find the landmarks automatically (the same fit Generate makes without landmarks) and show them, so you can "
     "check where the eye and mouth corners landed and move any that are off before generating.",
     "自動找出特徵點（與沒有特徵點時「產生」所做的擬合相同）並顯示，讓你在產生前檢查眼角與嘴角的位置，並移動偏離的點"
     "。"},
    {"Remove the landmarks: Generate then finds the face by itself.", "移除特徵點：之後「產生」會自動找出臉部。"},
    {"Play a recorded face take on the facial shapes: the JSON frames the phone link records ({\"type\":\"face\", "
     "\"t\":ms, \"weights\":{...}} as an array, {\"frames\":[...]} or JSON Lines).",
     "在臉部形狀上播放錄製的臉部動作：手機連結錄製的 JSON 影格（{\"type\":\"face\", \"t\":ms, \"weights\":{...}} 陣列"
     "、{\"frames\":[...]} 或 JSON Lines）。"},
    {"Play or pause the loaded take (it loops while Loop is on).",
     "播放或暫停已載入的動作（開啟「循環」時會重複播放）。"},
    {"Bake the loaded take into keyframes at 30 fps: Export GLB writes them as a glTF animation of the morph target "
     "weights, Export USD as time samples (Blender, Maya, Unreal, Omniverse play them). Saved with the project.",
     "將已載入的動作以 30 fps 烘焙為關鍵影格：「匯出 GLB」會寫成 morph target 權重的 glTF 動畫，「匯出 USD」則寫成時"
     "間取樣（Blender、Maya、Unreal、Omniverse 可播放）。隨專案儲存。"},
    {"Remove the baked take, so exports are a still pose again.", "移除已烘焙的動作，讓匯出再次成為靜態姿勢。"},
    {"Auto: Left and Right shapes are made exact mirror images when the sculpt is symmetric about X = 0. On: always "
     "(asymmetric spots take the average). Off: each side as fitted.",
     "自動：雕刻相對 X = 0 對稱時，Left 與 Right 形狀會成為完全鏡像。開啟：一律如此（不對稱處取平均）。關閉：每側維持"
     "擬合結果。"},
    {"Eyes (Parts named Eye...) turn for the eyeLook shapes, Teeth / Tongue follow the jaw below the bite (and "
     "tongueOut), and other Parts on the face (brows, lashes, a beard) follow the skin.",
     "眼睛（名為 Eye... 的部件）會隨 eyeLook 形狀轉動，Teeth／Tongue 會在咬合線下方跟隨下顎（以及 tongueOut），臉上的"
     "其他部件（眉毛、睫毛、鬍子）會跟隨皮膚。"},
    {"How strongly the presets above are applied (click a preset again to see it).",
     "上方預設套用的強度（再按一下預設即可查看）。"},
    {"This ARKit shape's weight (it drives the same-named layer on every Part). To fix the shape, select its layer in "
     "Tool > Layers, set the weight to 1 and sculpt.",
     "此 ARKit 形狀的權重（驅動每個部件上同名的圖層）。若要修正形狀，請在工具 > 圖層中選取其圖層，將權重設為 1 後雕刻"
     "。"},
    {"Play the take again from the start when it ends.", "動作結束時從頭再播放一次。"},
    // --- PanelFill.cpp (tools/i18n/coverage.py) ---
    {"Fill Layers", "填充圖層"},
    {"New Fill Layer", "新增填充圖層"},
    {"Add a Fill layer: a material over the whole Part through a mask you build from effects (Edge Wear, Dirt, "
     "Gradient, Thickness, Light Direction, Noise, a painted mask, Levels, Blur, Invert). Change it any time; it "
     "follows the sculpt.",
     "新增填充圖層：透過你以效果建立的遮罩，將材質覆蓋整個部件（Edge Wear、Dirt、Gradient、Thickness、Light "
     "Direction、Noise、繪製遮罩、Levels、Blur、Invert）。可隨時修改；會跟隨雕刻。"},
    {"Material Recipe", "材質配方"},
    {"Lay a ready-made stack of Fill layers on the Part: Worn Metal, Painted Edges, Dirt and Dust, Rusted Iron. They "
     "read the Part's Mesh Maps, so each model wears where it would. Undoable.",
     "在部件上套用現成的填充圖層堆疊：Worn Metal、Painted Edges、Dirt and Dust、Rusted Iron。它們讀取部件的 Mesh "
     "Maps，讓每個模型在該磨損的地方磨損。可復原。"},
    {"Baking Mesh Maps...", "正在烘焙 Mesh Maps..."},
    {"No Mesh Maps yet", "尚無 Mesh Maps"},
    {"Mesh Maps", "Mesh Maps"},
    {"Ambient occlusion, curvature, thickness, position, world normal and ID of the Part on its texture's UVs, which "
     "Fill layers read. Baked in the background when a Fill layer needs them; with Live on, the tiles near what you "
     "sculpt are baked again after each stroke.",
     "部件在其貼圖 UV 上的環境光遮蔽、曲率、厚度、位置、世界法線與 ID，供填充圖層讀取。填充圖層需要時會在背景中烘焙；"
     "開啟「即時」時，每一筆畫後會重新烘焙你雕刻處附近的圖塊。"},
    {"Fill colour", "填充色彩"},
    {"The base colour the Fill layer lays down.", "填充圖層鋪上的基礎色彩。"},
    {"Paint on the model with this layer active (Alt erases)", "以此圖層為作用中在模型上繪製（Alt 擦除）"},
    {"Add mask effect", "新增遮罩效果"},
    {"Add an effect to the bottom of the mask stack. Generators read the Mesh Maps: Edge Wear (convex edges), Dirt "
     "(crevices), Gradient, Thickness (thin parts), Light Direction (surfaces facing up). Noise is in 3D, so it has "
     "no UV seams. Paint lets you paint the mask with the Paint brush.",
     "在遮罩堆疊底部加入效果。產生器讀取 Mesh Maps：Edge Wear（凸出邊緣）、Dirt（縫隙）、Gradient、Thickness（薄的部"
     "分）、Light Direction（朝上的表面）。Noise 是 3D 的，因此沒有 UV 接縫。Paint 讓你用 Paint 筆刷繪製遮罩。"},
    // --- PanelForgeSpheres.cpp (tools/i18n/coverage.py) ---
    {"Add a Forge Sphere Part: one root sphere to block out a body, creature or plant from. Drag on a sphere to grow "
     "limbs from it, Move and Scale them into proportion, check the skin with Preview (A), then Make Mesh.",
     "新增 Forge Sphere 部件：一個根球體，用來打出身體、生物或植物的粗胚。在球體上拖曳即可長出肢體，用移動與縮放調整"
     "比例，以預覽 (A) 檢查外皮，再「建立網格」。"},
    {"Drag on a sphere to grow a new sphere from it (how far you drag sets its size). Click a link between two "
     "spheres to add a joint there, and drag to place it. Alt+click a sphere to delete it.",
     "在球體上拖曳即可從它長出新球體（拖曳距離決定大小）。按一下兩球體間的連結可在該處加入關節，拖曳即可放置。Alt+按"
     "一下球體即可刪除。"},
    {"Drag a sphere to move it; the links stretch to follow. Symmetry moves its mirror twin too.",
     "拖曳球體即可移動；連結會伸展跟隨。對稱時其鏡像也會一起移動。"},
    {"Drag a sphere up or right to make it bigger, down or left to make it smaller.",
     "將球體向上或向右拖曳可放大，向下或向左拖曳可縮小。"},
    {"Drag places child", "拖曳放置子球"},
    {"Draw: dragging on a sphere moves the new sphere away from it (one drag places a limb joint) instead of sizing "
     "it. Off, hold Ctrl while you drag for the same.",
     "繪製：在球體上拖曳會讓新球體遠離它（一次拖曳放置一個肢體關節），而不是調整大小。關閉時，拖曳時按住 Ctrl 效果相"
     "同。"},
    {"Delete Selected", "刪除所選"},
    {"Delete the selected sphere (the one you last clicked or added, outlined in orange) and everything growing from "
     "it. The root stays.",
     "刪除所選球體（你最後按一下或新增的那個，以橘色外框標示）及從它長出的一切。根球體保留。"},
    {"Insert Between", "插入中間"},
    {"Add a joint halfway along the link from the selected sphere to the one it grows from.",
     "在所選球體與其母球體之間連結的中點加入關節。"},
    {"Set as Root", "設為根"},
    {"Make the selected sphere the root: the armature keeps its shape and grows from there (the links on the way turn "
     "around).",
     "將所選球體設為根：骨架保持形狀並從該處長出（途中的連結會反向）。"},
    {"Show the skin Make Mesh would build instead of the spheres. Turn it off to edit the spheres again.",
     "顯示「建立網格」將建立的外皮，而非球體。關閉即可再次編輯球體。"},
    {"Build the skin as a new, sculptable Part right below (watertight quads, one face group per link), like Make "
     "Adaptive Skin. The Forge Sphere Part is hidden and kept: show it to change the proportions and make a new mesh.",
     "在正下方將外皮建立為新的可雕刻部件（封閉四邊面，每個連結一個面群組），類似 Make Adaptive Skin。Forge Sphere 部"
     "件會被隱藏並保留：顯示它即可修改比例並建立新網格。"},
    {"How dense Make Mesh's skin is: cells across the model's longest side. 128 suits a blockout; raise it for thin "
     "fingers or tails.",
     "「建立網格」外皮的密度：沿模型最長邊的格數。128 適合粗胚；細手指或尾巴請調高。"},
    {"Forge Sphere insert", "插入 Forge Sphere"},
    {"Forge Sphere add", "新增 Forge Sphere"},
    // --- PanelGeometry.cpp (tools/i18n/coverage.py) ---
    {"The subdivision level to sculpt at: 1 is the base cage, the highest has the finest detail. D / Shift+D step up "
     "/ down. Detail is kept on every level.",
     "要雕刻的細分層級：1 為基礎籠，最高層級細節最精細。D／Shift+D 提高／降低。每個層級都會保留細節。"},
    {"Micro levels above the highest subdivision level: each 4x the triangles, stored as one 16-bit displacement per "
     "point (no mesh), so 300 million triangles take about 0.4 GB and sculpt in real time. Move reshapes the level "
     "below and the detail rides along.",
     "最高細分層級之上的微細分層級：每級三角面為 4 倍，每個點以一個 16 位元位移儲存（沒有網格），因此 3 億個三角面約"
     "佔 0.4 GB，並可即時雕刻。Move 會改變下層形狀，細節隨之移動。"},
    // --- PanelIntegrations.cpp (tools/i18n/coverage.py) ---
    {"Show SculptForge on Discord", "在 Discord 上顯示 SculptForge"},
    {"Your Discord profile shows SculptForge, its icon and how long you've been sculpting. Sent only to the Discord "
     "app on this computer, which shows it to your friends. Off by default.",
     "你的 Discord 個人資料會顯示 SculptForge、其圖示與你已雕刻多久。只傳送給這台電腦上的 Discord 應用程式，由它顯示"
     "給你的朋友。預設關閉。"},
    {"Show the monthly challenge", "顯示每月挑戰"},
    {"Adds \"Monthly challenge: <theme>\", with the theme you type below.",
     "加入「Monthly challenge: <theme>」，主題為你在下方輸入的內容。"},
    {"Show the file name", "顯示檔案名稱"},
    {"Adds the name of the file you're working on (never its folder). Off by default.",
     "加入你正在處理的檔案名稱（絕不含資料夾）。預設關閉。"},
    // --- PanelKelvinlet.cpp (tools/i18n/coverage.py) ---
    {"How the Kelvinlet brushes' material keeps its volume (Poisson's ratio): 0 is rubbery (it stretches without "
     "bulging), 100 is incompressible clay (pressing in bulges the surface up around the thumb, pulling out draws "
     "the sides in).",
     "Kelvinlet 筆刷材質保持體積的方式（帕松比）：0 像橡膠（拉伸時不隆起），100 是不可壓縮的黏土（壓下時拇指周圍表面"
     "隆起，拉出時兩側內縮）。"},
    {"A tighter, more pointed peak at the brush centre with the same soft reach (a smaller inner scale of the "
     "multi-scale Kelvinlet): for pulling points and tips.",
     "筆刷中心更集中、更尖的峰值，柔和範圍不變（多尺度 Kelvinlet 的內部尺度較小）：用於拉出尖端與末梢。"},
    {"Detail Clone: the distance between stamps along the stroke, in stamp widths (1 = edge to edge).",
     "Detail Clone：沿筆畫的印章間距，以印章寬度為單位（1 = 邊緣相接）。"},
    {"Detail Clone: each stamp's top turns with the stroke (a row of scales or rivets along a curve); off, every "
     "stamp keeps the screen's up.",
     "Detail Clone：每個印章的頂端隨筆畫轉動（沿曲線排列的一排鱗片或鉚釘）；關閉時，每個印章都保持螢幕的上方。"},
    {"Alpha and VDM from the viewport: drag a square over any sculpted detail (Shift for a circle) and capture it as "
     "a grayscale height alpha and as a vector displacement brush, ready to stamp with the VDM brush or along "
     "strokes with Detail Clone, and to save in your library.",
     "從視埠擷取 Alpha 與 VDM：在任何雕刻細節上拖出方形（Shift 為圓形），擷取為灰階高度 Alpha 與向量位移筆刷，可用 "
     "VDM 筆刷壓印或以 Detail Clone 沿筆畫使用，也可儲存到資源庫。"},
    {"What counts as the detail: the capture is the surface minus a smoothed copy of itself. 0 measures from a flat "
     "plane (the whole shape); higher takes away more of the broad form, so a detail on a curved arm or cheek "
     "captures flat. The VDM keeps each point's sideways offset too (overhangs).",
     "何者算是細節：擷取結果為表面減去其平滑後的複本。0 從平面量起（整個形狀）；越高去除越多大形，因此彎曲手臂或臉頰"
     "上的細節會擷取成平的。VDM 也會保留每個點的側向位移（懸垂）。"},
    {"Fades the capture out toward its border, so stamps blend into any surface.",
     "讓擷取結果向邊緣淡出，使印章能融入任何表面。"},
    {"Pixels a side of the alpha and the map (512 or less suits most details).",
     "Alpha 與貼圖每邊的像素數（大多數細節 512 以下即可）。"},
    {"Height alpha: measure from a plane fitted to the surface (a tilted surface captures level) instead of the "
     "screen's plane.",
     "高度 Alpha：從貼合表面的平面量起（傾斜表面會擷取成水平），而非螢幕平面。"},
    {"Height alpha: white = deepest (for carving brushes).", "高度 Alpha：白色 = 最深（用於雕刻凹陷的筆刷）。"},
    {"What the alpha and the VDM are called in your library (the same name replaces).",
     "Alpha 與 VDM 在資源庫中的名稱（同名會取代）。"},
    {"Make the height capture the brush alpha (it shapes every brush's stamp).",
     "將高度擷取設為筆刷 Alpha（決定每個筆刷印章的形狀）。"},
    {"Make the capture the VDM brush's map and pick the VDM brush (Drag Stamp stamps it).",
     "將擷取設為 VDM 筆刷的貼圖並選取 VDM 筆刷（以 Drag Stamp 壓印）。"},
    {"Paint copies of the captured detail along your strokes, each turned to follow the stroke.",
     "沿著你的筆畫繪製擷取細節的複本，每個都轉向跟隨筆畫。"},
    {"Keep both: the alpha as a PNG in your Library Alphas folder, the VDM in your brush library (Alpha picker, Brush "
     "> Vector Displacement and the Library's Brush tab), with thumbnails.",
     "兩者都保留：Alpha 以 PNG 存入資源庫的 Alphas 資料夾，VDM 存入筆刷資源庫（Alpha 選擇器、筆刷 > 向量位移與資源庫"
     "的「筆刷」分頁），並附縮圖。"},
    // --- PanelMasking.cpp (tools/i18n/coverage.py) ---
    {"Shapes", "形狀"},
    {"Lasso Mask and Rectangle Mask mask only what faces you: the far side and folds turned away stay as they are.",
     "Lasso Mask 與 Rectangle Mask 只遮罩面向你的部分：背面與背向你的皺褶保持不變。"},
    {"Mask by", "遮罩依據"},
    {"By peaks", "依凸起"},
    {"Mask ridges, peaks and sharp edges (replaces the mask; uses the mask strength)",
     "遮罩稜線、凸起與銳利邊緣（取代遮罩；使用遮罩強度）"},
    {"By colour", "依色彩"},
    {"Mask where the vertex paint is close to the main colour (the left shelf's swatch); Tolerance sets how close.",
     "遮罩頂點繪製接近主要色彩（左側工具列的色票）之處；「容差」設定接近程度。"},
    {"By facing", "依朝向"},
    {"Mask what faces the camera (or faces up or down; Facing below), within the angle: e.g. the front of a face, or "
     "the top of a rock.",
     "在角度範圍內遮罩面向相機（或朝上、朝下；見下方「朝向」）的部分：例如臉的正面或岩石頂部。"},
    {"By border", "依邊界"},
    {"Mask the open edges of the mesh and the edge rings inside them, so sculpting never moves the border. Replaces "
     "the mask and uses the mask strength. A closed mesh has no open edges.",
     "遮罩網格的開放邊緣及其內側的邊環，讓雕刻絕不會移動邊界。會取代遮罩並使用遮罩強度。封閉網格沒有開放邊緣。"},
    {"Adjust", "調整"},
    {"Mask by options", "遮罩依據選項"},
    {"Cavity / peaks threshold %.2f", "凹陷／凸起門檻 %.2f"},
    {"How deep a crease (By cavity) or how sharp a ridge (By peaks) must be: 0 takes anything, higher only the "
     "strongest.",
     "摺痕需要多深（依凹陷）或稜線需要多銳利（依凸起）：0 全部納入，越高只取最明顯的。"},
    {"Face group %d", "面群組 %d"},
    {"Colour tolerance %.2f", "色彩容差 %.2f"},
    {"Border rings %d", "邊界環數 %d"},
    {"Face Group borders", "面群組邊界"},
    {"By border also masks where face groups meet.", "「依邊界」也會遮罩面群組交會處。"},
    {"Soft", "柔和"},
    {"By border fades the mask from the border towards the innermost ring.",
     "「依邊界」會讓遮罩從邊界向最內側的環淡出。"},
    // --- PanelMaya.cpp (tools/i18n/coverage.py) ---
    {"Get automatically", "自動取得"},
    {"Load what Maya sends as soon as it arrives: SculptForge checks the exchange folder's manifest once a second "
     "(its modification time and size only). Off by default; off: use Get from Maya.",
     "Maya 傳來的內容一到就載入：SculptForge 每秒檢查一次交換資料夾的清單檔（只看修改時間與大小）。預設關閉；關閉時："
     "請使用「從 Maya 取得」。"},
    {"Turn the Maya bridge off", "關閉 Maya 橋接"},
    {"Forget the exchange folder; nothing in it is deleted.", "忘記交換資料夾；其中的內容不會被刪除。"},
    // --- PanelPacks.cpp (tools/i18n/coverage.py) ---
    {"Install Pack...", "安裝套件..."},
    {"Install a SculptForge pack (.sfpack): brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, pie menus "
     "and macros. A review shows exactly what's inside, its license and its author before anything is installed. "
     "Packs hold data only: nothing in one ever runs.",
     "安裝 SculptForge 套件 (.sfpack)：筆刷、Alpha、Lit Sphere、材質配方、VDM、按鍵配置、圓餅選單與巨集。安裝前會先檢"
     "視，清楚顯示內容、授權與作者。套件只含資料：其中的任何內容都不會執行。"},
    {"New Pack...", "新增套件..."},
    {"Make a pack from your own brushes, alphas, Lit Spheres, Material Recipe, VDMs, pie menus, shortcuts and macros, "
     "to share as one .sfpack file.",
     "以你自己的筆刷、Alpha、Lit Sphere、材質配方、VDM、圓餅選單、快速鍵與巨集建立套件，以單一 .sfpack 檔案分享。"},
    {"Packs", "套件"},
    {"Library > Packs: the installed packs. Switch one off or remove it, apply its keymap, add its pie menus.",
     "資源庫 > 套件：已安裝的套件。可停用或移除、套用其按鍵配置、加入其圓餅選單。"},
    {"Record Macro", "錄製巨集"},
    {"Start recording: the commands you run (menus, buttons, the console) are kept as a macro until you press it "
     "again. Files inside the project folder are kept as relative paths, any other file is asked for when the macro "
     "runs.",
     "開始錄製：你執行的命令（選單、按鈕、主控台）會儲存為巨集，直到你再按一次。專案資料夾內的檔案以相對路徑保存，其"
     "他檔案則在執行巨集時詢問。"},
    {"Macro Editor", "巨集編輯器"},
    {"Write or change a macro: SculptForge commands, one per line, checked as you type. Only SculptForge's own "
     "commands, nothing that reaches the network, and files only inside the project folder or picked when it runs.",
     "撰寫或修改巨集：SculptForge 命令，每行一個，輸入時即時檢查。只限 SculptForge 自己的命令，不含任何連網動作，檔案"
     "只限專案資料夾內或執行時選取的檔案。"},
    {"Macros", "巨集"},
    {"Install Pack", "安裝套件"},
    {"This pack can't be installed", "無法安裝此套件"},
    {"Nothing was installed. SculptForge only installs packs that pass every check: plain data files of the kinds a "
     "pack may hold, listed in its pack.json, within safe sizes, with nothing that could leave the pack's folder.",
     "未安裝任何內容。SculptForge 只安裝通過所有檢查的套件：套件允許類型的純資料檔，列於其 pack.json 中，大小安全，且"
     "沒有任何可能離開套件資料夾的內容。"},
    {"Member pack", "會員套件"},
    {"Author:", "作者："},
    {"License:", "授權："},
    {"Replaces the installed version", "取代已安裝的版本"},
    {"Contains:", "內容："},
    {"No license text in the pack (see License above).", "套件中沒有授權文字（請見上方的授權）。"},
    {"Packs hold data only: nothing in this one runs on install or when a project opens. Its macros list their "
     "commands and ask before they first run.",
     "套件只含資料：此套件的任何內容都不會在安裝或開啟專案時執行。其巨集會列出命令，並在第一次執行前詢問。"},
    {"Keymaps and pie menus are only applied when you choose to (Library > Packs).",
     "按鍵配置與圓餅選單只在你選擇時才套用（資源庫 > 套件）。"},
    {"Install", "安裝"},
    {"Copy the pack into SculptForge's packs folder and add its items to the lists.",
     "將套件複製到 SculptForge 的套件資料夾，並將其項目加入清單。"},
    {"Stop Recording", "停止錄製"},
    {"Safe mode (--no-packs): no pack is loaded this session", "安全模式 (--no-packs)：本次工作階段不載入任何套件"},
    {"No packs installed. Drop a .sfpack on the window or use Install Pack...",
     "尚未安裝套件。將 .sfpack 拖放到視窗中，或使用「安裝套件...」"},
    {"Pack on", "啟用套件"},
    {"Off: its items leave the lists until you switch it on again (nothing is deleted).",
     "關閉：其項目會從清單中移除，直到你再次啟用（不會刪除任何內容）。"},
    {"Details", "詳細資料"},
    {"Remove", "移除"},
    {"Delete the pack and everything it brought. Your own files are never touched.",
     "刪除套件及其帶來的所有內容。你自己的檔案絕不會被動到。"},
    {"Apply keymap", "套用按鍵配置"},
    {"Add pie menu", "加入圓餅選單"},
    {"Run", "執行"},
    {"Your macros", "你的巨集"},
    {"Pack keymaps", "套件按鍵配置"},
    {"Take the keys of this keymap from an installed pack (only actions SculptForge knows; a key another action had "
     "moves here). Saved automatically.",
     "從已安裝套件的此按鍵配置取用按鍵（只限 SculptForge 認得的動作；原屬其他動作的按鍵會移到這裡）。自動儲存。"},
    {"New Pack", "新增套件"},
    {"The pack's name, shown next to every item it brings.", "套件名稱，會顯示在它帶來的每個項目旁。"},
    {"Id", "ID"},
    {"Its folder name: lower-case letters, digits, '.', '_' or '-'. A newer version keeps the same id.",
     "其資料夾名稱：小寫字母、數字、'.'、'_' 或 '-'。新版本沿用相同的 ID。"},
    {"Version", "版本"},
    {"Author", "作者"},
    {"Who made it (shown when it's installed).", "製作者（安裝時顯示）。"},
    {"License", "授權"},
    {"For example CC-BY-4.0, CC0-1.0 or your own terms.", "例如 CC-BY-4.0、CC0-1.0 或你自己的條款。"},
    {"Member pack (insider channel)", "會員套件（Insider 頻道）"},
    {"Marks the pack as a members' pack (Member Requests). Display only: anyone can install it.",
     "將套件標示為會員套件 (Member Requests)。僅供顯示：任何人都能安裝。"},
    {"Nothing of your own yet: save brushes, put alphas and Lit Spheres in your Library folders, record a macro...",
     "還沒有你自己的內容：儲存筆刷、將 Alpha 與 Lit Sphere 放進資源庫資料夾、錄製巨集..."},
    {"Save Pack...", "儲存套件..."},
    {"Save Pack", "儲存套件"},
    {"Write the .sfpack, checked exactly as an install would check it.",
     "寫出 .sfpack，並以與安裝時完全相同的方式檢查。"},
    {"Run Macro?", "要執行巨集嗎？"},
    {"Your macro", "你的巨集"},
    {"Files it uses stay inside the project folder.", "它使用的檔案都在專案資料夾內。"},
    {"Run it now. You won't be asked again for this macro unless it changes.",
     "立即執行。除非此巨集有變更，否則不會再詢問你。"},
    {"One command per line, as in the console (# starts a comment). Files: a path inside the project folder, or ? to "
     "pick one when it runs.",
     "每行一個命令，與主控台相同（# 開頭為註解）。檔案：專案資料夾內的路徑，或用 ? 在執行時選取。"},
    {"Recording macro", "正在錄製巨集"},
    // --- PanelPaintLayers.cpp (tools/i18n/coverage.py) ---
    {"Lock the layer: the Paint brush, fills, clears and merges leave it alone.",
     "鎖定圖層：Paint 筆刷、填滿、清除與合併都不會影響它。"},
    {"Show or hide the layer. Hidden layers don't show or export.", "顯示或隱藏圖層。隱藏的圖層不會顯示或匯出。"},
    {"How the active layer mixes with the layers below, with Photoshop's maths: Multiply darkens, Screen lightens, "
     "Overlay adds contrast (50% grey changes nothing), Add brightens, ...",
     "目前圖層與下方圖層的混合方式，採用 Photoshop 的運算：色彩增值變暗、濾色變亮、覆蓋增加對比（50% 灰不產生變化）、"
     "相加變亮，..."},
    {"How strongly the active layer shows (0-100%). Painting keeps its own coverage.",
     "目前圖層的顯示強度 (0-100%)。繪製保有其自身的覆蓋度。"},
    {"Add a transparent paint layer above the active one and paint on it.",
     "在目前圖層上方新增透明繪製圖層並在上面繪製。"},
    {"Copy the active layer (Base becomes an opaque layer) to a new layer above it.",
     "將目前圖層複製到其上方的新圖層（基底會變成不透明圖層）。"},
    {"Delete the active layer (undoable). Base can't be deleted.", "刪除目前圖層（可復原）。基底無法刪除。"},
    {"Move the active layer up the stack (it then covers the layer it passed).",
     "將目前圖層在堆疊中上移（之後會覆蓋它越過的圖層）。"},
    {"Move the active layer down the stack (Base stays at the bottom).",
     "將目前圖層在堆疊中下移（基底維持在最底部）。"},
    {"Merge the active layer into the one below it (Base for the lowest layer), keeping how it looks.",
     "將目前圖層合併到下方圖層（最下層則合併到基底），並保留其外觀。"},
    {"Flatten", "平面化"},
    {"Bake every visible layer into Base and remove the layers (hidden layers are discarded).",
     "將所有可見圖層烘焙到基底並移除圖層（隱藏的圖層會被捨棄）。"},
    {"Clear the active layer: a layer becomes transparent, Base white. Undoable.",
     "清除目前圖層：一般圖層變為透明，基底變為白色。可復原。"},
    {"Fill the active layer with the current colour (respects the mask, like Color > Fill).",
     "以目前色彩填滿目前圖層（遵守遮罩，如同色彩 > 填滿）。"},
    // --- PanelParts.cpp (tools/i18n/coverage.py) ---
    {"Draw the other Parts solid again", "將其他部件恢復為實體顯示"},
    {"See through the other Parts to the active one", "透過其他部件看到目前部件"},
    // --- PanelPbr.cpp (tools/i18n/coverage.py) ---
    {"Write the texture's channels as image files for an engine or DCC, with the export preset chosen beside the "
     "button (Unreal, Unity URP / HDRP, glTF, Godot, Blender, USD).",
     "將貼圖的各通道寫成影像檔，供引擎或 DCC 使用，採用按鈕旁所選的匯出預設（Unreal、Unity URP／HDRP、glTF、Godot、"
     "Blender、USD）。"},
    {"Show one channel on its own on the model: roughness, metallic, height, AO and opacity in grey, the normal as "
     "its colours (with the height's normal in it), emission in colour. Painting updates it live.",
     "在模型上單獨顯示一個通道：粗糙度、金屬度、高度、AO 與不透明度以灰階顯示，法線以其色彩顯示（含高度產生的法線），"
     "自發光以彩色顯示。繪製時即時更新。"},
    {"Which files Export Maps writes and how channels are packed. Your own presets are JSON files (export_maps "
     "preset=path/to/preset.json; see the guide's Export presets page).",
     "「匯出貼圖」會寫出哪些檔案，以及通道如何封裝。你自己的預設是 JSON 檔（export_maps preset=path/to/preset.json；"
     "請參閱指南的 Export presets 頁面）。"},
    {"Set the brush's colour, roughness and metallic at once, and make it paint those channels.",
     "一次設定筆刷的色彩、粗糙度與金屬度，並讓它繪製這些通道。"},
    {"Which way painted normals lean (x, y); 0, 0 paints a flat normal.",
     "繪製法線的傾斜方向 (x, y)；0, 0 繪製平坦法線。"},
    {"Whether this layer affects the channel. Right-click for the channel's blend mode (Normal Combine lays normal "
     "detail onto the normals below) and opacity.",
     "此圖層是否影響該通道。按右鍵可設定通道的混合模式（Normal Combine 會將法線細節疊加到下方的法線上）與不透明度。"},
    // --- PanelPerformance.cpp (tools/i18n/coverage.py) ---
    {"While you rotate, pan or zoom a big scene (over the hardware profile's motion proxy threshold, which the frame "
     "governor adjusts; 10 million triangles without a profile), draw a lighter version without antialiasing, so the "
     "view keeps up; it is full detail again as soon as the view stops.",
     "旋轉、平移或縮放大型場景時（超過硬體設定檔的運動代理門檻，由影格調節器調整；沒有設定檔時為 1000 萬個三角面），"
     "繪製不含抗鋸齒的較輕版本，讓視圖保持流暢；視圖一停止就恢復完整細節。"},
    {"Run brushes, normal and BVH updates and project saving on every core. Turn it off only to compare speeds or "
     "rule it out when troubleshooting: the results are the same either way.",
     "在所有核心上執行筆刷、法線與 BVH 更新及專案儲存。只在比較速度或排除問題時關閉：兩種方式結果都相同。"},
    {"Sculpt strokes work out the brush on their own thread while the view keeps drawing at full speed, and show the "
     "dabs still being worked out as a preview on the surface, with the cursor and the stroke's head drawn where the "
     "pen is about to be. On a heavy mesh the surface moves under the pen at once and lifting the pen is instant. "
     "The finished sculpt is exactly the same either way.",
     "雕刻筆畫在獨立執行緒上計算筆刷，視圖則持續全速繪製，並將仍在計算中的筆觸以預覽方式顯示在表面上，游標與筆畫前端"
     "會畫在筆即將到達的位置。在大型網格上，表面會立即在筆下移動，抬筆也是即時的。兩種方式最終的雕刻結果完全相同。"},
    {"Live Move (Experimental)", "即時移動（實驗性）"},
    {"Experimental. The Move brush's drag is drawn by the graphics card from where the surface started, so it follows "
     "the pen at the view's frame rate whatever the mesh size; the move itself is applied once, exactly, when you "
     "lift the pen. Not with masks, hidden parts, sculpt layers, micro levels, Connected Only or View falloff (Move "
     "works as before there).",
     "實驗性功能。Move 筆刷的拖曳由顯示卡從表面起始狀態繪製，因此無論網格多大，都能以視圖的影格率跟上筆；實際移動會在"
     "抬筆時精確套用一次。不適用於遮罩、隱藏部分、雕刻圖層、微細分層級、僅限相連或檢視衰減（這些情況下 Move 與以往相"
     "同）。"},
    {"Background Preview (Experimental)", "背景預覽（實驗性）"},
    {"Experimental. On meshes of 2 million vertices or more, big brushes are worked out on their own thread; with "
     "this on, the dabs still being worked out are shown as a preview on the surface (as Predictive Strokes does), "
     "so the stroke stays under the pen. The finished sculpt is exactly the same either way.",
     "實驗性功能。在 200 萬個頂點以上的網格上，大筆刷會在獨立執行緒上計算；開啟此選項時，仍在計算中的筆觸會以預覽方式"
     "顯示在表面上（如同預測筆畫），讓筆畫保持在筆下。兩種方式最終的雕刻結果完全相同。"},
    {"Fast Upload (Experimental)", "快速上傳（實驗性）"},
    {"Experimental. While you sculpt, the changed part of the mesh goes to the graphics card through memory both can "
     "see, and the card copies it into place itself, instead of the driver copying it during the frame. The sculpt "
     "and the view are exactly the same either way; needs a driver with buffer storage (OpenGL 4.4).",
     "實驗性功能。雕刻時，網格中變更的部分會透過雙方都能存取的記憶體傳到顯示卡，由顯示卡自行複製到位，而不是在影格中"
     "由驅動程式複製。兩種方式的雕刻與視圖完全相同；需要支援 buffer storage 的驅動程式 (OpenGL 4.4)。"},
    {"With Predictive Strokes: while you sculpt, the brush only moves the surface and shades the moved part from its "
     "facets; the smooth shading, the cavities and the rest are worked out once when you lift the pen. Lighter on "
     "laptops and on battery: the hardware profile turns it on there by itself (its Draft brushes setting); this "
     "turns it on everywhere. Brushes that follow the surface's direction (Inflate, the plane brushes) use its "
     "direction from the stroke's start, so the result can differ slightly from a full stroke.",
     "搭配預測筆畫：雕刻時筆刷只移動表面，並以其面片為移動部分著色；平滑著色、凹陷等其餘部分會在抬筆時計算一次。在筆"
     "電與使用電池時較省資源：硬體設定檔會在這些情況自動開啟（其草稿筆刷設定）；此選項則在所有情況開啟。跟隨表面方向"
     "的筆刷（Inflate、平面類筆刷）使用筆畫起點的方向，因此結果可能與完整筆畫略有不同。"},
    {"Fast Big Brushes (Experimental)", "快速大筆刷（實驗性）"},
    {"Experimental. Big brushes on dense meshes: a dab that reaches more than 150,000 vertices finds its plane from a "
     "sample of them and turns the shading along with the move it makes, and the dabs waiting for a frame are placed "
     "together in one pass. Every vertex still moves by the brush itself; the exact shading follows behind the brush "
     "and when you lift the pen. Small brushes, Smooth, Layer, Inflate, Nudge, Clay Block, alphas and masked areas "
     "are placed dab by dab as before. Try it on a heavy mesh and tell us how it feels.",
     "實驗性功能。高密度網格上的大筆刷：影響超過 150,000 個頂點的筆觸會從取樣的頂點求出平面，並隨其移動一併旋轉著色，"
     "等待影格的筆觸會在一次處理中一起放置。每個頂點仍由筆刷本身移動；精確著色會在筆刷之後及抬筆時跟上。小筆刷、"
     "Smooth、Layer、Inflate、Nudge、Clay Block、Alpha 與遮罩區域仍與以往一樣逐筆放置。請在大型網格上試試，並告訴我們"
     "感覺如何。"},
    {"Samples per pixel in the 3D view: more smooths edges, fewer is faster on huge meshes.",
     "3D 視圖中每個像素的取樣數：較多邊緣較平滑，較少在超大網格上較快。"},
    {"Draw the 3D view at a share of the window's resolution and scale it up: 75% draws about half the pixels, 50% a "
     "quarter. For slow or integrated GPUs and big screens; Save Render stays sharp (#80).",
     "以視窗解析度的一部分繪製 3D 視圖再放大：75% 約繪製一半的像素，50% 約四分之一。適用於較慢或內建的 GPU 與大螢幕；"
     "「儲存算繪」仍保持清晰 (#80)。"},
    {"What the last brush stroke cost, per dab (one step of the brush, all symmetry sides): finding and moving the "
     "vertices, updating their normals and the BVH (the picking structure), and sending the change to the GPU (per "
     "frame). Under 16 ms keeps up with a 60 Hz display.",
     "上一筆畫每個筆觸（筆刷的一步，含所有對稱側）的耗時：尋找並移動頂點、更新其法線與 BVH（選取結構），以及將變更傳"
     "送到 GPU（每影格）。低於 16 ms 即可跟上 60 Hz 顯示器。"},
    {"What the scene holds: the meshes with their topology, the BVHs, the subdivision levels kept for stepping "
     "between them, the undo history (Edit keeps up to 1 GB, dropping the oldest steps), painted textures with their "
     "layers and material channels (only painted 64x64 tiles of a channel use memory), and the GPU buffers of the "
     "visible Parts.",
     "場景包含的內容：網格與其拓樸、BVH、為了切換而保留的細分層級、復原記錄（編輯最多保留 1 GB，會捨棄最舊的步驟）、"
     "含圖層與材質通道的繪製貼圖（通道中只有繪製過的 64x64 圖塊會佔用記憶體），以及可見部件的 GPU 緩衝區。"},
    // --- PanelPerformanceProfile.cpp (tools/i18n/coverage.py) ---
    {"Measure this computer again (about 2 seconds): the GPU's triangle throughput and upload speed, brush speed and "
     "memory bandwidth, then pick the performance profile. Your overrides are kept.",
     "重新測量這台電腦（約 2 秒）：GPU 的三角面處理量與上傳速度、筆刷速度與記憶體頻寬，然後選擇效能設定檔。你的自訂值"
     "會保留。"},
    {"Hold the view at the target frame rate (60 fps, or 120 on a 120 Hz display) by drawing big scenes' lighter "
     "motion proxy sooner or later while the view moves. Only what you see changes, never the model.",
     "視圖移動時，透過提早或延後使用大型場景的較輕運動代理，讓視圖維持目標影格率（60 fps，120 Hz 顯示器為 120）。只改"
     "變你看到的畫面，絕不改變模型。"},
    {"SculptForge measures this computer on first launch (and when the graphics card, its driver or the version "
     "changes) and picks settings to match: how many triangles the view draws per frame, when big scenes switch to "
     "their lighter motion proxy, GPU memory and upload budgets, texture and subdivision limits, threads. Nothing is "
     "sent anywhere.",
     "SculptForge 會在第一次啟動時（以及顯示卡、驅動程式或版本變更時）測量這台電腦，並選擇相符的設定：視圖每影格繪製"
     "多少三角面、大型場景何時切換為較輕的運動代理、GPU 記憶體與上傳預算、貼圖與細分上限、執行緒數。不會傳送任何資料"
     "。"},
    // --- PanelPhone.cpp (tools/i18n/coverage.py) ---
    {"Use your phone as SculptForge's camera: scan the QR code to send scans, photos and splat files, see the sculpt "
     "on your desk in AR, and drive a face with your own. No app to install; phone and computer on the same Wi-Fi.",
     "把手機當作 SculptForge 的相機：掃描 QR 碼即可傳送掃描、照片與 Splat 檔案，在桌上以 AR 檢視雕刻，並用你的臉驅動"
     "臉部。無須安裝應用程式；手機與電腦連上同一個 Wi-Fi 即可。"},
    {"Record the live face feed into a take (Captures/<time>-face/take.jsonl, one frame of ARKit weights per line) "
     "for playback and export. Again: stop.",
     "將即時臉部訊號錄製為動作（Captures/<time>-face/take.jsonl，每行一個 ARKit 權重影格），以供播放與匯出。再按一次"
     "：停止。"},
    {"Point your phone's camera at it and tap the link: the capture page opens, paired with this window. A new code "
     "is made every time the window opens.",
     "用手機相機對準它並點選連結：擷取頁面會開啟，並與此視窗配對。每次開啟視窗都會產生新代碼。"},
    {"For a phone that can't scan: open the address in its browser and type these 6 digits. After 5 wrong tries the "
     "code changes.",
     "若手機無法掃描：在其瀏覽器中開啟網址並輸入這 6 位數字。輸入錯誤 5 次後代碼會變更。"},
    {"This computer has several network addresses. If the phone can't open the page, pick the one on the Wi-Fi the "
     "phone uses (often 192.168.x.x).",
     "這台電腦有多個網路位址。如果手機無法開啟頁面，請選擇手機所用 Wi-Fi 上的位址（通常是 192.168.x.x）。"},
    {"A photo sent from the phone starts Image to 3D straight away with the service chosen there (free Hugging Face "
     "by default). Off: it opens in Image to 3D and waits for Generate.",
     "從手機傳來的照片會立即以手機上選擇的服務開始「影像轉 3D」（預設為免費的 Hugging Face）。關閉：會在「影像轉 3D」"
     "中開啟並等待「產生」。"},
    {"While the phone shows the model, sculpting re-exports it (at most every few seconds); the phone offers the new "
     "version.",
     "手機顯示模型期間，雕刻會重新匯出模型（最多每幾秒一次）；手機會提供新版本。"},
    {"Swap the left and right expressions (and the head's turn), if the character winks with the wrong eye.",
     "交換左右表情（與頭部轉向），用於角色眨錯眼睛時。"},
    // --- PanelProject.cpp (tools/i18n/coverage.py) ---
    {"Members get betas early.", "會員可搶先取得 Beta 版。"},
    {"Become a member", "成為會員"},
    {"Once a day at startup, download a small file from the SculptForge website that names the newest version, and "
     "show a banner when there is a newer one. Nothing about you or your computer is sent.",
     "每天啟動時一次，從 SculptForge 網站下載一個標示最新版本的小檔案，有較新版本時顯示橫幅。不會傳送任何關於你或你電"
     "腦的資訊。"},
    {"Update channel", "更新頻道"},
    {"Stable", "穩定版"},
    {"Preview (betas)", "預覽（Beta 版）"},
    {"Stable offers only finished releases. Preview also offers the betas the update feed names. Betas are a member "
     "perk: members get them from the private members' repository and the #members channel on Discord. When the "
     "stable release passes a beta, Preview offers that.",
     "穩定版只提供正式發行版本。預覽另外也提供更新來源列出的 Beta 版。Beta 版是會員福利：會員可從私人會員儲存庫與 "
     "Discord 的 #members 頻道取得。當穩定版超越某個 Beta 版時，預覽會改為提供穩定版。"},
    // --- PanelRetopo.cpp (tools/i18n/coverage.py) ---
    {"Automatic quad retopology: rebuilds the Part as clean, all-quad topology whose edge loops follow the forms, "
     "close to Target Polygons. Runs in the background (Cancel stops it); undoable. Then divide it and use Part > "
     "Project > Project Detail to bring the detail back.",
     "自動四邊面重新拓樸：將部件重建為乾淨、全四邊面的拓樸，其邊環順著形體走，面數接近目標多邊形數量。在背景中執行（"
     "「取消」可停止）；可復原。之後細分它，再用部件 > 投射 > Project Detail 找回細節。"},
    {"The Quad Remesh Guides brush: drag on the model to draw a curve for Quad Remesh's edge loops to follow, around "
     "an eye, the mouth or a joint. A curve that ends where it started becomes a loop. Symmetry mirrors it; "
     "Alt+click a curve to delete it. Pick any brush to sculpt again.",
     "Quad Remesh Guides 筆刷：在模型上拖曳畫出曲線，讓 Quad Remesh 的邊環沿著它走，例如眼睛、嘴巴或關節周圍。終點回"
     "到起點的曲線會成為環。對稱會鏡像它；Alt+按一下曲線即可刪除。選取任何筆刷即可繼續雕刻。"},
    {"Delete the last Quad Remesh guide curve drawn (with its mirror copies).",
     "刪除最後畫的 Quad Remesh 導引曲線（含其鏡像複本）。"},
    {"Delete every Quad Remesh guide curve of this Part.", "刪除此部件的所有 Quad Remesh 導引曲線。"},
    {"Project Detail", "Project Detail"},
    {"Project the shape and vertex paint of every other visible Part onto this one: each vertex moves along its "
     "normal to the nearest surface within Dist. The mask holds vertices back. Typical use: Quad Remesh a copy, "
     "divide it a few times, show the original, Project Detail.",
     "將所有其他可見部件的形狀與頂點繪製投射到此部件上：每個頂點沿法線移動到距離範圍內最近的表面。遮罩可固定頂點。典"
     "型用法：對複本執行 Quad Remesh，細分幾次，顯示原始模型，再 Project Detail。"},
    {"Quick Remesh", "Quick Remesh"},
    {"Remesh after each stroke", "每筆畫後重新網格化"},
    {"After a stroke that stretched the surface (edges pulled to more than 1.6 times their length), the active Part "
     "is remeshed at the Resolution above in the background. Sculpting never waits: a stroke made meanwhile remeshes "
     "again. Each remesh is one undo step. Not on a Part with subdivision levels, sculpt layers or a painted texture.",
     "筆畫拉伸表面後（邊被拉長到原長的 1.6 倍以上），目前部件會在背景中以上方的解析度重新網格化。雕刻不需要等待：期間"
     "進行的筆畫會再次重新網格化。每次重新網格化為一個復原步驟。不適用於有細分層級、雕刻圖層或繪製貼圖的部件。"},
    {"Remeshing in the background. The result replaces the Part when it's done.",
     "正在背景中重新網格化。完成後結果會取代部件。"},
    {"Stop Quad Remesh; the Part stays as it is.", "停止 Quad Remesh；部件維持原樣。"},
    {"Quad Remesh 2 (default): a curvature-aligned layout solved as one integer grid over the whole model, so poles "
     "appear only where the form needs them and edge loops run unbroken; open guide curves become exact edge loops. "
     "Legacy: the method of SculptForge 0.9 (more poles, a split look).",
     "Quad Remesh 2（預設）：以整個模型為單一整數網格求解的曲率對齊佈線，極點只出現在形體需要之處，邊環不中斷；開放的"
     "導引曲線會成為精確的邊環。舊版：SculptForge 0.9 的方法（極點較多，看起來較零碎）。"},
    {"About how many quads Quad Remesh makes (Ctrl+click to type a number). Half / Same / Double set it from the "
     "Part's current polygon count.",
     "Quad Remesh 大約產生的四邊面數量（按住 Ctrl 並按一下可輸入數字）。一半／相同／雙倍會依部件目前的多邊形數量設定"
     "。"},
    {"Target half the Part's current polygon count.", "目標為部件目前多邊形數量的一半。"},
    {"Target the Part's current polygon count (to clean up topology without changing its density).",
     "目標為部件目前的多邊形數量（在不改變密度的情況下整理拓樸）。"},
    {"Target twice the Part's current polygon count.", "目標為部件目前多邊形數量的兩倍。"},
    {"0: quads all about the same size. Higher: smaller quads where the surface curves, bigger ones on flat areas "
     "(the count stays near the target).",
     "0：四邊面大小大致相同。較高：表面彎曲處四邊面較小，平坦處較大（數量維持在目標附近）。"},
    {"Edge loops follow the face group borders exactly and the result keeps the face groups. Paint groups where you "
     "want loops (around the eyes, the mouth) to guide the flow.",
     "邊環會精確沿著面群組邊界，結果也會保留面群組。在你想要邊環的地方（眼睛、嘴巴周圍）繪製群組，即可引導佈線。"},
    {"Hard edges get an edge loop, so they stay sharp (Quad Remesh 2: faces meeting at more than about 60 degrees "
     "along a line at least a few quads long; legacy: more than 50 degrees).",
     "硬邊會得到一條邊環，因此保持銳利（Quad Remesh 2：沿著至少數個四邊面長的線，以超過約 60 度相接的面；舊版：超過 "
     "50 度）。"},
    {"Paint where you want density: red areas get up to 4 times the quads, blue areas down to a quarter, white is "
     "unchanged.",
     "在你想要密度的地方繪製：紅色區域最多 4 倍四邊面，藍色區域最少四分之一，白色不變。"},
    {"Keep the Part as it was, hidden, right below the new one (\"<name> Original\"), ready to Project Detail its "
     "detail back. Off: replace it (undo brings it back).",
     "將原部件隱藏保留在新部件正下方（「<name> Original」），隨時可用 Project Detail 找回細節。關閉：直接取代（復原可"
     "找回）。"},
    {"How clean the new topology is: poles are vertices with other than four edges (fewer is cleaner), corners off "
     "square measure how far quad corners are from 90 degrees, and the distance is how far the new surface strays "
     "from the sculpt (as a percentage of its size; Project Detail brings fine detail back).",
     "新拓樸的乾淨程度：極點是邊數不為四的頂點（越少越乾淨），非直角角落衡量四邊面角落偏離 90 度的程度，距離則是新表"
     "面偏離雕刻的程度（以其大小的百分比表示；Project Detail 可找回細部細節）。"},
    {"How far around each guide curve the edge flow follows it: 0 = only the edge loop on the curve, 100 = about six "
     "quads to each side. The loop on the curve is always made.",
     "佈線在每條導引曲線周圍跟隨的範圍：0 = 只有曲線上的邊環，100 = 每側約六個四邊面。曲線上的邊環一定會建立。"},
    {"How far each vertex looks for the source surface, along its normal, as a fraction of the Part's size. Raise it "
     "if parts don't pick up the detail; lower it where surfaces are close together.",
     "每個頂點沿法線尋找來源表面的距離，以部件大小的比例表示。若有部分沒取得細節請調高；表面彼此靠近處請調低。"},
    {"Smooth the projected result this many times (softens noise from the source).",
     "將投射結果平滑這麼多次（減輕來源的雜訊）。"},
    {"Where the source is found on both sides of a vertex, settle halfway between instead of on the nearest.",
     "當來源位於頂點兩側時，停在中間而非最近的一側。"},
    {"Project the shape (into the active sculpt layer).", "投射形狀（到目前的雕刻圖層）。"},
    {"Vertex Paint", "頂點繪製"},
    {"Project the vertex paint (into the active paint layer) when a source has any.",
     "來源有頂點繪製時將其投射（到目前的繪製圖層）。"},
    // --- PanelSculptable.cpp (tools/i18n/coverage.py) ---
    {"Turn the capture into a clean, sculptable Part with its colours, in one go: a watertight surface (floaters "
     "dropped, holes filled), quad topology at the chosen polygon budget with edge loops round the eyes and mouth "
     "when it is a face, and vertex paint or a texture. The splats stay (hidden). Runs in the background; Cancel "
     "stops it.",
     "一次將擷取轉為乾淨、可雕刻且帶有色彩的部件：封閉表面（去除浮點、填補孔洞）、依所選多邊形預算的四邊面拓樸（臉部"
     "時在眼睛與嘴巴周圍有邊環），以及頂點繪製或貼圖。Splat 會保留（隱藏）。在背景中執行；「取消」可停止。"},
    {"Quad Remesh the scan along the face guides you checked, then colour it: the sculptable Part appears next to the "
     "scan (which is hidden).",
     "依你檢查過的臉部導引對掃描執行 Quad Remesh，然後上色：可雕刻部件會出現在掃描旁（掃描會被隱藏）。"},
    {"Place the face landmarks yourself, one click each: the eye corners, the nose tip, the mouth corners and the "
     "chin (the panel says which is next). For faces the automatic search misses.",
     "自行放置臉部特徵點，每點按一下：眼角、鼻尖、嘴角與下巴（面板會告訴你下一個是哪個）。適用於自動搜尋找不到的臉。"},
    {"Only the splats inside the box count: drag the box's face handles in the viewport to cut away the ground, the "
     "background or a stand.",
     "只計算方塊內的 Splat：在視埠中拖曳方塊各面的控制點，即可裁掉地面、背景或支架。"},
    {"Working in the background; the new Part appears when it's done.", "正在背景中處理；完成後會出現新部件。"},
    {"Stop Make sculptable; nothing changes.", "停止「轉為可雕刻」；不會有任何變更。"},
    {"Leave the review without remeshing (the scan Part stays as it is).",
     "不重新網格化即離開檢視（掃描部件維持原樣）。"},
    {"Look for the face again from the current view (turn the view to face it first).",
     "從目前視圖重新尋找臉部（請先轉動視圖正對臉部）。"},
    {"How finely the surface follows the capture: voxels across the subject. Higher keeps thinner parts and more "
     "detail but takes longer (about 5 s at 384 for a million splats).",
     "表面貼合擷取的精細程度：橫跨主體的體素數。越高保留越細的部分與越多細節，但耗時較長（100 萬個 Splat 在 384 時約 "
     "5 秒）。"},
    {"When the capture is a head, find the eyes, nose, mouth and chin as seen from the current view and give Quad "
     "Remesh edge loops round the eyes and mouth and along the nasolabial folds, mirrored across the face (animation "
     "topology). Turn the view to face it first.",
     "擷取為頭部時，從目前視圖找出眼睛、鼻子、嘴巴與下巴，並為 Quad Remesh 提供眼睛與嘴巴周圍及沿法令紋的邊環，左右鏡"
     "像（動畫用拓樸）。請先轉動視圖正對臉部。"},
    {"Stop after the scan to check the face landmarks and the loops: drag a dot to move it, or place them by hand, "
     "then Finish.",
     "掃描後暫停，以檢查臉部特徵點與邊環：拖曳圓點即可移動，或手動放置，再按「完成」。"},
    {"The capture's colours go into the vertices (vertex paint): more polygons, more colour detail.",
     "擷取的色彩會寫入頂點（頂點繪製）：多邊形越多，色彩細節越多。"},
    {"Auto UV the result and bake the capture's colours into a 2K texture (sharp colour on a light mesh, for games "
     "and exports).",
     "為結果自動 UV，並將擷取的色彩烘焙到 2K 貼圖（輕量網格上的清晰色彩，適用於遊戲與匯出）。"},
    {"Keep the dense voxel surface as a hidden \"<name> Scan\" Part, to Project Detail more detail from later.",
     "將高密度體素表面保留為隱藏的「<name> Scan」部件，以便之後用 Project Detail 取得更多細節。"},
    {"Splats fainter than this are ignored (haze and floaters are usually faint).",
     "比此值更淡的 Splat 會被忽略（霧氣與浮點通常很淡）。"},
    {"Seal gaps up to this many voxels between splats.", "封閉 Splat 之間最多這麼多體素的間隙。"},
    {"Fill big holes such as the open bottom of a subject standing on a table (space boxed in on five sides counts as "
     "inside). Turn off for open cavities like a cup.",
     "填補大型孔洞，例如放在桌上的主體開放的底部（五面封閉的空間視為內部）。開放的凹洞（如杯子）請關閉。"},
    {"Keep every solid piece instead of only the largest (floaters come back too).",
     "保留每個實心區塊，而不只是最大的（浮點也會回來）。"},
    // --- PanelSelectionSets.cpp (tools/i18n/coverage.py) ---
    {"Set name", "集合名稱"},
    {"Name of the set Store makes; storing again with the same name replaces it.",
     "「儲存」建立的集合名稱；以相同名稱再次儲存會取代它。"},
    {"Store", "儲存"},
    {"Store the checked channels of this Part as a named set (one undo step). Sets restore at any subdivision level "
     "and are saved in the project.",
     "將此部件勾選的通道儲存為具名集合（一個復原步驟）。集合可在任何細分層級還原，並儲存在專案中。"},
    {"From Visible", "從可見部分"},
    {"Quick set: store the faces shown right now (Visible, Visible 2, ...).",
     "快速集合：儲存目前顯示的面（Visible、Visible 2、...）。"},
    {"Restore mode", "還原模式"},
    {"How Restore meets the current mask and visibility: Replace them, or Union, Intersect or Subtract the set's. "
     "Combine uses the same mode (not Replace) to make a new set from two.",
     "「還原」與目前遮罩及可見性的結合方式：取代，或與集合的進行聯集、交集或相減。「合併」使用相同模式（取代除外）從"
     "兩個集合建立新集合。"},
    {"No sets yet: Store saves one.", "尚無集合：按「儲存」建立一個。"},
    {"Put the selected set's channels back (not its shape: Morph to Set does that), carried to the current "
     "subdivision level. One undo step.",
     "還原所選集合的通道（不含形狀：那是「變形至集合」的功能），並帶到目前的細分層級。一個復原步驟。"},
    {"Delete the selected set (undo brings it back).", "刪除所選集合（復原可找回）。"},
    {"Combine", "合併"},
    {"Make a new set from the selected one and this one by their mask and visibility, with the mode above (Union, "
     "Intersect or Subtract).",
     "依遮罩與可見性，以上方的模式（聯集、交集或相減）從所選集合與此集合建立新集合。"},
    {"Clear Morph Target", "清除 Morph 目標"},
    {"Morph Target", "Morph 目標"},
    {"Make this set's shape the Morph brush's target and pick the Morph brush: strokes pull the surface back toward "
     "the stored shape (Alt pushes away), at any subdivision level.",
     "將此集合的形狀設為 Morph 筆刷的目標並選取 Morph 筆刷：筆畫會將表面拉回儲存的形狀（Alt 則推開），可在任何細分層"
     "級使用。"},
    {"Morph to Set", "變形至集合"},
    {"Move the whole surface toward the set's shape by the amount below, within the mask. One undo step.",
     "在遮罩範圍內，依下方的量將整個表面移向集合的形狀。一個復原步驟。"},
    {"Amount %.2f", "量 %.2f"},
    {"Store with Shape checked to morph toward a set.", "勾選「形狀」再儲存，即可變形至集合。"},
    // --- PanelSplatTrain.cpp (tools/i18n/coverage.py) ---
    {"Train a Gaussian splat from a video of an object (walk slowly all the way around it). The sharpest frame of "
     "each moment is used. The splat shows in the viewport after a few hundred steps and sharpens live.",
     "從物體的影片訓練高斯 Splat（慢慢繞物體走一整圈）。每個時刻會使用最清晰的影格。數百步後 Splat 會出現在視埠中，並"
     "即時變得更清晰。"},
    {"Train a Gaussian splat from a folder: photos taken all around an object, a phone-link capture, or a COLMAP / "
     "nerfstudio dataset (which already has camera positions).",
     "從資料夾訓練高斯 Splat：環繞物體拍攝的照片、手機連結的擷取，或 COLMAP／nerfstudio 資料集（已含相機位置）。"},
    {"Stop training now and keep what it has learned (the newest checkpoint) as the result.",
     "立即停止訓練，並保留目前學到的內容（最新的檢查點）作為結果。"},
    {"Stop training. Nothing is saved; the last preview stays in the viewport until you Clear the splats.",
     "停止訓練。不會儲存任何內容；最後的預覽會留在視埠中，直到你清除 Splat。"},
    {"Use a Brush trainer you already have (brush_app): for example one you built yourself with cargo on Linux ARM, "
     "where there is no ready-made download.",
     "使用你已有的 Brush 訓練器 (brush_app)：例如你在 Linux ARM 上用 cargo 自行建置的版本，那裡沒有現成的下載。"},
    {"Use a COLMAP program you already have (it finds where each photo was taken).",
     "使用你已有的 COLMAP 程式（它會找出每張照片的拍攝位置）。"},
    {"Download it once from its GitHub release; nothing else is sent.",
     "從其 GitHub 發行版本下載一次；不會傳送其他任何資料。"},
    // --- PanelSupportCard.cpp (tools/i18n/coverage.py) ---
    {"I'm already a member", "我已經是會員"},
    {"Signs in with GitHub, as member builds do (Preferences > Updates): no more membership cards, and the member "
     "builds are on.",
     "以 GitHub 登入，與會員版本相同（偏好設定 > 更新）：不再顯示會員卡片，並開啟會員版本。"},
    // --- PanelTexture.cpp (tools/i18n/coverage.py) ---
    {"Paint into the vertices (vertex paint): the colour resolution is the mesh's. Shown when the Part has a texture "
     "(Tool > Texture Map).",
     "繪製到頂點（頂點繪製）：色彩解析度取決於網格。部件有貼圖時顯示（工具 > 貼圖）。"},
    {"Paint into the Part's texture: the colour resolution comes from the texture, not the mesh. The Paint brush, Rgb "
     "sculpting, fills and Paint Layers all work on it.",
     "繪製到部件的貼圖：色彩解析度來自貼圖而非網格。Paint 筆刷、Rgb 雕刻、填滿與繪製圖層都作用於貼圖。"},
    {"Start texture painting: give the Part a texture (the size chosen in Tool > Texture Map, 2K by default) so paint "
     "goes into texels instead of vertices, and the detail comes from the texture, not the mesh. Without UVs, Auto "
     "UV unwraps the model first (in the background, with its progress in the status bar).",
     "開始貼圖繪製：為部件加上貼圖（大小在工具 > 貼圖中選擇，預設 2K），讓繪製寫入紋素而非頂點，細節來自貼圖而非網格"
     "。沒有 UV 時，自動 UV 會先展開模型（在背景中進行，進度顯示在狀態列）。"},
    {"Show the painted texture on the model (multiplied over the material, like vertex paint).",
     "在模型上顯示繪製的貼圖（以色彩增值疊在材質上，與頂點繪製相同）。"},
    {"Unwrap the Part automatically: cut it into UV islands and pack them, so any sculpt can carry a texture. "
     "Replaces existing UVs (undoable). Runs in the background; big meshes can take a minute or more.",
     "自動展開部件：切割成 UV 島並排列，讓任何雕刻都能帶有貼圖。會取代現有的 UV（可復原）。在背景中執行；大型網格可能"
     "需要一分鐘以上。"},
    {"Give the Part a texture of the chosen size to paint into (white). Needs UVs: Auto UV makes them. Replaces an "
     "existing texture (undoable).",
     "為部件加上所選大小的貼圖以供繪製（白色）。需要 UV：自動 UV 可產生。會取代現有貼圖（可復原）。"},
    {"Show the texture with the UV layout drawn over it, below these buttons.",
     "在這些按鈕下方顯示疊有 UV 佈局的貼圖。"},
    {"Bake the vertex paint (all paint layers) into the active texture layer. Makes a 2K texture if there is none.",
     "將頂點繪製（所有繪製圖層）烘焙到目前的貼圖圖層。若沒有貼圖會建立 2K 貼圖。"},
    {"Copy the texture into the vertex paint of every vertex (detail finer than the mesh is lost).",
     "將貼圖複製到每個頂點的頂點繪製（比網格更細的細節會遺失）。"},
    {"Save the texture (every visible layer composited) as a PNG image.",
     "將貼圖（合成所有可見圖層）儲存為 PNG 影像。"},
    {"Load an image as the Part's texture (replacing its layers; undoable).",
     "載入影像作為部件的貼圖（取代其圖層；可復原）。"},
    {"Remove the Part's texture (undoable). Painting goes back to the vertices.",
     "移除部件的貼圖（可復原）。繪製會回到頂點。"},
    {"Voxel Remesh and Quad Remesh keep a painted texture: the new mesh gets Auto UVs and the texture (every layer) "
     "is projected onto it from the old surface, in the same undo step. Off: the texture is dropped. Merging Parts "
     "keeps textures too. Shown under Remesh and Quad Remesh when the Part has a texture.",
     "體素重新網格化與 Quad Remesh 會保留繪製貼圖：新網格會自動 UV，貼圖（所有圖層）會從舊表面投射上去，屬於同一個復"
     "原步驟。關閉：捨棄貼圖。合併部件也會保留貼圖。部件有貼圖時顯示於重新網格化與 Quad Remesh 下方。"},
    {"Keep the painted texture: the new mesh gets Auto UVs and the texture (every layer) is projected onto it from "
     "the old surface, in the same undo step (it adds the Auto UV time). Off: the texture is dropped. Scripts: "
     "keep_texture=false.",
     "保留繪製貼圖：新網格會自動 UV，貼圖（所有圖層）會從舊表面投射上去，屬於同一個復原步驟（會增加自動 UV 的時間）。"
     "關閉：捨棄貼圖。指令碼：keep_texture=false。"},
    {"Unwrapping the Part in the background; you can keep working.", "正在背景中展開部件；你可以繼續工作。"},
    {"Size of a new texture: 1K, 2K (the default), 4K or 8K texels square. Bigger holds finer paint and uses more "
     "memory (8K: 256 MB per layer).",
     "新貼圖的大小：邊長 1K、2K（預設）、4K 或 8K 紋素。越大可容納越細的繪製，也使用越多記憶體（8K：每個圖層 256 MB）"
     "。"},
    {"The texture with the Part's UV layout over it (island borders on big meshes). Paint on the model; the texture "
     "fills in here.",
     "疊有部件 UV 佈局的貼圖（大型網格只顯示島邊界）。在模型上繪製；貼圖會在此處填入。"},
    // --- PanelTimeline.cpp (tools/i18n/coverage.py) ---
    {"Show or hide the history strip along the top or bottom of the canvas (Preferences > Interface > Timeline "
     "position): every stroke and operation as a tick. Drag to see the model at any point, click to go there; "
     "sculpting from an earlier point keeps the old continuation as a branch. Right-click a stroke to change its "
     "strength.",
     "在畫布上方或下方顯示或隱藏歷史記錄列（偏好設定 > 界面 > 時間軸位置）：每個筆畫與操作都是一個刻度。拖曳即可查看"
     "任何時間點的模型，按一下即可前往；從較早的時間點繼續雕刻時，原本的後續會保留為分支。在筆畫上按右鍵可變更其強度"
     "。"},
    {"Timeline position: the Sculpt Timeline strip runs along the top of the viewport, below the top shelf and away "
     "from the brushes on the bottom shelf (the default). Saved automatically.",
     "時間軸位置：雕刻時間軸列位於視埠頂端，在頂部工具列下方，遠離底部工具列的筆刷（預設）。自動儲存。"},
    {"Timeline position: the Sculpt Timeline strip runs along the bottom of the viewport, above the brush shelf. "
     "Saved automatically.",
     "時間軸位置：雕刻時間軸列位於視埠底部，在筆刷工具列上方。自動儲存。"},
    {"Render the history of the active Part as a turntable timelapse video (1080p or 4K MP4, no captions).",
     "將目前部件的歷史記錄算繪為轉盤縮時影片（1080p 或 4K MP4，無字幕）。"},
    {"Measure how responsive sculpting is on this computer: the standard scene (a 15 or 33 million triangle head; "
     "Clay small, medium and large, Move, Smooth and a 240 px brush) through the same code a pen uses, with a live "
     "display of frame times and how far the stroke trails the pointer. Writes a video and a JSON result you can "
     "share. Replaces the scene.",
     "測量這台電腦上雕刻的反應速度：標準場景（1500 萬或 3300 萬個三角面的頭部；小、中、大 Clay、Move、Smooth 與 240 "
     "px 筆刷），使用與繪圖筆相同的程式碼，並即時顯示影格時間與筆畫落後指標的距離。會寫出可分享的影片與 JSON 結果。會"
     "取代場景。"},
    {"Open the history strip.", "開啟歷史記錄列。"},
    {"Also save the history beside the project as <name>.sfhist (strokes, masks, vertex paint, visibility; a topology "
     "change cuts it). Opening the project brings it back.",
     "另將歷史記錄以 <name>.sfhist 儲存在專案旁（筆畫、遮罩、頂點繪製、可見性；拓樸變更會使其中斷）。開啟專案時會一併"
     "載入。"},
    {"Render this history as a turntable timelapse video.", "將此歷史記錄算繪為轉盤縮時影片。"},
    {"Fold the timeline into a tab.", "將時間軸收合為分頁。"},
    {"How strongly this stroke shows: 1 = as sculpted, 0 = gone, 2 = twice as strong. Later strokes are not redone: "
     "where they overlap this one they keep their own displacement on top.",
     "此筆畫的顯示強度：1 = 雕刻時的樣子，0 = 消失，2 = 兩倍強。之後的筆畫不會重做：與此筆畫重疊之處會保留它們自己的"
     "位移在上方。"},
    // --- PanelUpdate.cpp (tools/i18n/coverage.py) ---
    {"Downloading the update...", "正在下載更新..."},
    {"It installs when you quit.", "結束程式時安裝。"},
    {"Restart now", "立即重新啟動"},
    {"Install it when you quit SculptForge; the next start is the new version.",
     "結束 SculptForge 時安裝；下次啟動即為新版本。"},
    {"Update banner", "更新橫幅"},
    {"Starting SculptForge %s...", "正在啟動 SculptForge %s..."},
    {"The update didn't work: %s.", "更新失敗：%s."},
    {"Nothing was changed. Download it from the website instead, or try again.",
     "未變更任何內容。請改從網站下載，或再試一次。"},
    {"Update with your package manager (%s)", "使用套件管理員更新 (%s)"},
    {"Copy", "複製"},
    {"Download the new package from the website and open it.", "從網站下載新的安裝套件並開啟。"},
    {"Try again", "再試一次"},
    {"Update", "更新"},
    {"Download the new version from SculptForge's GitHub releases, check its signature and checksum, and install it. "
     "The current version is kept until the new one has started.",
     "從 SculptForge 的 GitHub 發行版本下載新版本，檢查簽章與總和檢查碼後安裝。目前版本會保留到新版本成功啟動為止。"},
    {"Asking GitHub...", "正在詢問 GitHub..."},
    {"On GitHub, enter this code:", "請在 GitHub 輸入此代碼："},
    {"Open github.com/login/device", "開啟 github.com/login/device"},
    {"Waiting for GitHub...", "正在等待 GitHub..."},
    {"Member builds", "會員版本"},
    {"Member and Studio members get each release early. Sign in with the GitHub account that has the invitation to "
     "the member repository: SculptForge then also offers the member builds, checked with the same signature as "
     "every release. The sign-in can only read that repository's releases; it is kept in your system's keychain and "
     "Sign out removes it.",
     "Member 與 Studio 會員可搶先取得每個版本。請以收到會員儲存庫邀請的 GitHub 帳號登入：SculptForge 之後也會提供會員"
     "版本，並以與所有版本相同的簽章檢查。此登入只能讀取該儲存庫的發行版本；它保存在系統的鑰匙圈中，按「登出」即可移"
     "除。"},
    {"Signed in with GitHub.", "已以 GitHub 登入。"},
    {"Signed in as %s on GitHub.", "已以 %s 身分登入 GitHub。"},
    {"Sign out", "登出"},
    {"Sign in with GitHub", "以 GitHub 登入"},
    // --- PanelUvEditor.cpp (tools/i18n/coverage.py) ---
    {"UV Editor", "UV 編輯器"},
    {"Open the UV Editor: the Part's UV layout in 2D over the UDIM tiles. Select vertices, edges, faces or islands "
     "(here or on the model), move, rotate and scale them, mark seams, unwrap, pack, set texel density and lay "
     "islands into trim sheet strips. Stretch and texel density show live on the model while you edit.",
     "開啟 UV 編輯器：在 UDIM 圖塊上以 2D 顯示部件的 UV 佈局。可選取頂點、邊、面或島（在此或在模型上），移動、旋轉與"
     "縮放，標記接縫、展開、排列、設定紋素密度，並將島放入裝飾貼圖的條帶。編輯時，拉伸與紋素密度會即時顯示在模型上。"},
    {"Select UV vertices, edges, faces or whole islands (1-4 while the pointer is over the UV view). The selection "
     "converts when you switch.",
     "選取 UV 頂點、邊、面或整個島（指標在 UV 檢視上時按 1-4）。切換時選取範圍會跟著轉換。"},
    {"Unwrap", "展開"},
    {"Unwrap with the chosen method: the selected faces (fitted where they were), or the whole Part (packed). Least "
     "Stretch keeps angles and lengths along the marked seams; Conformal keeps angles; By Face Groups cuts along the "
     "face group borders too; Planar projects each piece; Unfold flattens the current islands again in place, "
     "keeping pins; Auto Seams finds its own. Closed pieces get cut automatically. Big meshes unwrap in the "
     "background.",
     "以所選方法展開：所選的面（在原位擬合），或整個部件（並排列）。Least Stretch 沿標記的接縫保留角度與長度；"
     "Conformal 保留角度；By Face Groups 也沿面群組邊界切割；Planar 投射每個區塊；Unfold 在原位重新攤平目前的島並保留"
     "釘選；Auto Seams 自行找出接縫。封閉的區塊會自動切開。大型網格會在背景中展開。"},
    {"Pack", "排列"},
    {"Pack the selected islands (or all) as large as they fit into the 0-1 tile, or across the Columns x Rows grid of "
     "UDIM tiles: turned to their smallest box, scaled to one texel density, with the margin between them.",
     "將所選的島（或全部）以能放下的最大尺寸排列到 0-1 圖塊，或排列到欄數 x 列數的 UDIM 圖塊格中：旋轉到最小外框，縮"
     "放到相同紋素密度，並保留其間的邊距。"},
    {"Mark Seam", "標記接縫"},
    {"Mark the selected edges (or the border of the selected faces) as seams: Unwrap cuts there.",
     "將所選的邊（或所選面的邊界）標記為接縫：展開時會在此切開。"},
    {"Clear Seam", "清除接縫"},
    {"Clear the seams on the selected edges (or the border of the selected faces).",
     "清除所選邊（或所選面的邊界）上的接縫。"},
    {"Mark every face group border as a seam.", "將每個面群組邊界標記為接縫。"},
    {"Island Borders", "島邊界"},
    {"Mark every current UV island border as a seam (keeps an imported layout's cuts for Unwrap).",
     "將目前每個 UV 島的邊界標記為接縫（為展開保留匯入佈局的切口）。"},
    {"Sharp Edges", "銳利邊"},
    {"Mark every edge sharper than the angle as a seam (hard-surface models).",
     "將比該角度更銳利的每條邊標記為接縫（硬表面模型）。"},
    {"Clear All", "全部清除"},
    {"Clear every marked seam.", "清除所有標記的接縫。"},
    {"Seam Tool", "接縫工具"},
    {"Draw seams on the model: drag along the edges you want cut (each stretch follows the shortest edge path); "
     "Alt+drag clears them. Seams show in red on the model while the UV Editor is open.",
     "在模型上繪製接縫：沿你要切開的邊拖曳（每段會沿最短的邊路徑）；Alt+拖曳可清除。UV 編輯器開啟時，接縫會以紅色顯示"
     "在模型上。"},
    {"Select in 3D", "在 3D 中選取"},
    {"Select on the model: click or drag over it to select faces, islands, vertices or edges (the UV Editor's mode); "
     "Alt removes. The selection shows in both views.",
     "在模型上選取：在模型上按一下或拖曳即可選取面、島、頂點或邊（依 UV 編輯器的模式）；Alt 取消選取。選取範圍會同時"
     "顯示在兩個檢視中。"},
    {"Pin", "釘選"},
    {"Pin the selected UVs: Unfold and Unwrap keep them where they are (P over the UV view).",
     "釘選所選的 UV：Unfold 與展開會讓它們維持原位（在 UV 檢視上按 P）。"},
    {"Unpin", "取消釘選"},
    {"Unpin the selected UVs (Alt+P over the UV view).", "取消釘選所選的 UV（在 UV 檢視上按 Alt+P）。"},
    {"Weld", "焊接"},
    {"Weld the selected vertices: where they meet across a seam they become one UV each (the seam goes). W over the "
     "UV view.",
     "焊接所選頂點：在接縫兩側相接處各自合為一個 UV（接縫消失）。在 UV 檢視上按 W。"},
    {"Split", "分離"},
    {"Split the selection off: the selected edges, or the border of the selected faces, become seams so the faces "
     "move on their own (Y over the UV view).",
     "分離選取範圍：所選的邊或所選面的邊界會成為接縫，讓這些面可以獨立移動（在 UV 檢視上按 Y）。"},
    {"Straighten", "拉直"},
    {"Line the selected vertices up on one horizontal or vertical line (an edge loop, a border).",
     "將所選頂點排成一條水平或垂直線（邊環、邊界）。"},
    {"Flip U", "翻轉 U"},
    {"Mirror the selected islands (or all) left to right.", "將所選的島（或全部）左右鏡像。"},
    {"Flip V", "翻轉 V"},
    {"Mirror the selected islands (or all) top to bottom.", "將所選的島（或全部）上下鏡像。"},
    {"Rotate 90", "旋轉 90"},
    {"Turn the selection (or everything) a quarter turn counter-clockwise.",
     "將選取範圍（或全部）逆時針旋轉四分之一圈。"},
    {"Set", "設定"},
    {"Scale the selected islands (or all) to the target texel density.", "將所選的島（或全部）縮放到目標紋素密度。"},
    {"Match", "統一"},
    {"Give the selected islands (or all) one texel density: their mean, so their sizes match on the surface.",
     "讓所選的島（或全部）使用相同的紋素密度：取其平均值，使它們在表面上的大小一致。"},
    {"Pick", "擷取"},
    {"Take the target texel density from the selected islands (or the whole Part).",
     "從所選的島（或整個部件）取得目標紋素密度。"},
    {"Pack at Density", "依密度排列"},
    {"Pack at exactly the target texel density, using as many UDIM tiles as it takes (1001, 1002, ..., Columns wide, "
     "then the next row up): every island keeps the same texels per unit across the tiles.",
     "精確依目標紋素密度排列，使用所需數量的 UDIM 圖塊（1001、1002、...，寬度為欄數，然後往上一列）：每個島在所有圖塊"
     "上保持相同的每單位紋素數。"},
    {"Assign to Strip", "指派到條帶"},
    {"Lay the selected islands into the chosen trim sheet strip: turned lengthwise, scaled to the strip's height and "
     "lined up along u. The texture stays put, so the islands pick up the strip's look.",
     "將所選的島放入所選的裝飾貼圖條帶：沿長邊轉向，縮放到條帶的高度並沿 u 對齊。貼圖不動，因此島會呈現該條帶的外觀。"},
    {"Fit the selection (or the whole layout) in the UV view (F over it).",
     "將選取範圍（或整個佈局）置入 UV 檢視（在其上按 F）。"},
    {"What the faces show, here and on the model: Islands (a colour each), Stretch (green: true to the surface; "
     "yellow to red: stretched or squashed; magenta: folded over), Density (texel density; blue: fewer texels than "
     "the mean, red: more) or Plain.",
     "面所顯示的內容（此處與模型上）：島（每個一種顏色）、拉伸（綠色：與表面一致；黃到紅：拉伸或擠壓；洋紅：翻折）、"
     "密度（紋素密度；藍色：低於平均，紅色：高於平均）或單色。"},
    {"Behind the layout: nothing, a checker, or the painted texture (tile 1001).",
     "佈局後方：無、棋盤格或繪製的貼圖（圖塊 1001）。"},
    {"Colours on the model", "在模型上顯示色彩"},
    {"Show the same colours on the model (islands, stretch, density and the selection), live while you edit.",
     "在模型上顯示相同的色彩（島、拉伸、密度與選取範圍），編輯時即時更新。"},
    {"Checker", "棋盤格"},
    {"Checker on the model", "模型上的棋盤格"},
    {"A checker on the model's UVs (tinted per UDIM tile): squares that stay square mean no stretch.",
     "模型 UV 上的棋盤格（依 UDIM 圖塊著色）：方格保持方正表示沒有拉伸。"},
    {"Click to select (Shift adds, Ctrl removes), drag a box; drag the box handles to move, scale (corners) or rotate "
     "(the knob). Keys over the view: 1-4 modes, A all, Alt+A none, Ctrl+I invert, G / R / S move, rotate, scale (X "
     "/ Y lock an axis, Ctrl snaps), F frame, P / Alt+P pin, W weld, Y split. Wheel zooms, right or middle drag pans.",
     "按一下即可選取（Shift 加選，Ctrl 減選），拖曳出方框；拖曳方框控制點可移動、縮放（角落）或旋轉（旋鈕）。檢視上的"
     "按鍵：1-4 模式，A 全選，Alt+A 全不選，Ctrl+I 反轉，G／R／S 移動、旋轉、縮放（X／Y 鎖定軸，Ctrl 貼齊），F 置入檢"
     "視，P／Alt+P 釘選，W 焊接，Y 分離。滾輪縮放，右鍵或中鍵拖曳平移。"},
    {"Least Stretch: angles and lengths, along the marked seams (the default). Conformal (LSCM): angles only. By Face "
     "Groups: also cuts along the face group borders. Planar: each piece projected flat. Unfold Islands: the current "
     "islands flattened again where they are, keeping pins. Auto Seams: finds its own seams.",
     "Least Stretch：沿標記的接縫保留角度與長度（預設）。Conformal (LSCM)：只保留角度。By Face Groups：也沿面群組邊界"
     "切割。Planar：每個區塊投射攤平。Unfold Islands：在原位重新攤平目前的島，並保留釘選。Auto Seams：自行找出接縫。"},
    {"Refine %.0f", "精修 %.0f"},
    {"Refine", "精修"},
    {"Least-stretch steps after the conformal start: more keeps lengths better (0 = conformal).",
     "在 Conformal 起始之後的最小拉伸步數：越多越能保持長度（0 = Conformal）。"},
    {"%.0f deg", "%.0f 度"},
    {"Sharp angle", "銳利角度"},
    {"Sharp Edges marks the edges whose faces meet at more than this angle.",
     "「銳利邊」會標記兩側面以超過此角度相接的邊。"},
    {"Margin %.0f px", "邊距 %.0f px"},
    {"Margin", "邊距"},
    {"Texels kept free between islands (half of it at the tile borders), at the texture's size.",
     "島之間保留的空白紋素（圖塊邊緣為一半），以貼圖大小計算。"},
    {"Columns %.0f", "欄數 %.0f"},
    {"Columns", "欄數"},
    {"UDIM tiles across (along u, 1001 onwards; ten make a full UDIM row). Packing fills Columns x Rows tiles and "
     "never goes wider, also when it packs at a fixed texel density.",
     "橫向的 UDIM 圖塊數（沿 u，從 1001 開始；十個為完整的一列 UDIM）。排列會填滿欄數 x 列數個圖塊，絕不會更寬，以固"
     "定紋素密度排列時也是如此。"},
    {"UDIM rows up (along v: 1011 sits above 1001). 10 Columns x 2 Rows fills 1001-1020; more tiles, more texels per "
     "island.",
     "往上的 UDIM 列數（沿 v：1011 位於 1001 上方）。10 欄 x 2 列會填滿 1001-1020；圖塊越多，每個島的紋素越多。"},
    {"Turn islands to their smallest box and allow quarter turns, so more fit.",
     "將島旋轉到最小外框並允許四分之一圈旋轉，以放入更多島。"},
    {"Equal density", "相同密度"},
    {"Scale the islands to one texel density before packing (off: they keep their sizes relative to each other).",
     "排列前將島縮放到相同的紋素密度（關閉：保持彼此的相對大小）。"},
    {"The texture size densities and margins are measured at (the Part's texture's when it has one).",
     "用來計算密度與邊距的貼圖大小（部件有貼圖時使用其大小）。"},
    {"Target density", "目標密度"},
    {"Texels per unit of surface length that Set and Pack at Density give the islands.",
     "「設定」與「依密度排列」為島設定的每單位表面長度紋素數。"},
    {"Strips", "條帶"},
    {"The sheet's horizontal strips: a count (4 equal strips), heights from the top (0.5,0.25,0.25), or v ranges "
     "(0-0.5;0.5-1). They show over tile 1001.",
     "貼圖上的水平條帶：數量（4 條等寬條帶）、從上方起算的高度 (0.5,0.25,0.25)，或 v 範圍 (0-0.5;0.5-1)。顯示在圖塊 "
     "1001 上。"},
    {"Fit width", "符合寬度"},
    {"Squeeze or stretch the row to fill u 0-1 exactly; off keeps the islands' proportions (trims repeat in u).",
     "壓縮或拉伸該列以剛好填滿 u 0-1；關閉時保持島的比例（裝飾在 u 方向重複）。"},
    {"Checker %.0f per tile", "每個圖塊 %.0f 格棋盤"},
    {"Paint stays on the model", "繪製保留在模型上"},
    {"When the Part has a painted texture, every UV edit moves the paint with it (re-projected onto the new layout in "
     "the same undo step), so the model looks the same. Off: the texture stays put and the model shows the UVs' new "
     "place on it (what trim sheets want).",
     "部件有繪製貼圖時，每次編輯 UV 都會連同繪製一起移動（在同一個復原步驟中重新投射到新佈局），讓模型外觀不變。關閉"
     "：貼圖不動，模型會顯示 UV 在貼圖上的新位置（裝飾貼圖需要這樣）。"},
    // --- PanelVdm.cpp (tools/i18n/coverage.py) ---
    {"Load a vector displacement map for the VDM brush: a 32- or 16-bit OpenEXR from Blender's VDM Brush Baker (or "
     "any EXR whose RGB are X right, Y up, Z out of the surface, in brush radii) or a .sfvdm. It is kept in your "
     "brush library. You can also drop an .exr on the window.",
     "為 VDM 筆刷載入向量位移貼圖：Blender 的 VDM Brush Baker 產生的 32 或 16 位元 OpenEXR（或任何 RGB 為 X 向右、Y "
     "向上、Z 朝表面外、以筆刷半徑為單位的 EXR），或 .sfvdm。會保存在筆刷資源庫中。也可以將 .exr 拖放到視窗中。"},
    {"Add a flat VDM plane to sculpt a new brush on: seen from the top, its border masked so it stays put; the other "
     "Parts are hidden. Sculpt anything (overhangs welcome), then Create VDM.",
     "新增平坦的 VDM 平面以雕刻新筆刷：從頂部觀看，邊界已遮罩以保持不動；其他部件會被隱藏。雕刻任何形狀（歡迎懸垂結構"
     "），再按「建立 VDM」。"},
    {"Turn the sculpted VDM plane into a vector displacement brush: how far every point moved from the flat grid "
     "becomes the map, saved in your brush library and picked as the VDM brush's map Needs the VDM plane (its UVs "
     "record where each point started).",
     "將雕刻好的 VDM 平面轉為向量位移筆刷：每個點相對平坦網格的位移成為貼圖，儲存在筆刷資源庫中，並設為 VDM 筆刷的貼"
     "圖。需要 VDM 平面（其 UV 記錄每個點的起始位置）。"},
    {"Save the VDM brush's map as a 32-bit float OpenEXR, for Blender: add it as the texture of a Draw brush with "
     "Area Plane mapping and Vector Displacement on (Help > User Guide > Sculpting > Vector displacement brushes).",
     "將 VDM 筆刷的貼圖儲存為 32 位元浮點 OpenEXR，供 Blender 使用：將其設為 Draw 筆刷的貼圖，使用 Area Plane 映射並"
     "開啟 Vector Displacement（說明 > 使用指南 > Sculpting > Vector displacement brushes）。"},
    {"For maps that store the surface normal in G instead of B (Y-up apps). Blender's maps and ours need nothing; "
     "maps from other apps depend on how they were exported, so try this if a stamp comes out lying on its side.",
     "適用於將表面法線存在 G 而非 B 的貼圖（Y 軸朝上的軟體）。Blender 與我們的貼圖不需要；其他軟體的貼圖取決於匯出方"
     "式，若印章側躺請試試此選項。"},
    {"Mirror the map's sideways (R) channel when loading: for a stamp that comes out mirrored.",
     "載入時鏡像貼圖的側向 (R) 通道：用於印章左右相反時。"},
    {"Mirror the map's up (G) channel when loading: for a stamp that leans the wrong way.",
     "載入時鏡像貼圖的向上 (G) 通道：用於印章傾向錯誤方向時。"},
    {"Negate the map's height (B) when loading: for a map that digs in where it should rise.",
     "載入時反轉貼圖的高度 (B)：用於該隆起卻凹陷的貼圖。"},
    {"Multiply a loaded map's vectors: for maps saved in other units than the brush radius (1 = as stored).",
     "將已載入貼圖的向量相乘：用於以筆刷半徑以外單位儲存的貼圖（1 = 依原樣）。"},
    {"Quads per side of a new VDM plane. 256 is a good start; divide it (Ctrl+D) for finer detail.",
     "新 VDM 平面每邊的四邊面數。256 是不錯的起點；細分 (Ctrl+D) 可得到更細的細節。"},
    {"What Create VDM calls the new map (a map of the same name is replaced).",
     "「建立 VDM」為新貼圖取的名稱（同名貼圖會被取代）。"},
    {"Pixels a side of the map Create VDM makes. 256 holds most shapes; 512 for fine detail (Blender suggests 512 or "
     "less).",
     "「建立 VDM」所產生貼圖每邊的像素數。256 足以應付大多數形狀；細節用 512（Blender 建議 512 以下）。"},
    // --- PanelVertexPaint.cpp (tools/i18n/coverage.py) ---
    {"The view's flat background colour. Choosing one switches the background to Flat Colour. It is saved "
     "automatically for the next session; projects keep their own.",
     "視圖的純色背景色彩。選擇顏色會將背景切換為純色。會自動儲存到下一次工作階段；專案會保有自己的設定。"},
    {"Sculpt brushes also paint the colour", "雕刻筆刷也會繪製色彩"},
    {"The paint layer the Paint brush and fills write into. Add and arrange layers in Tool > Paint Layers.",
     "Paint 筆刷與填滿所寫入的繪製圖層。可在工具 > 繪製圖層中新增與排列圖層。"},
    {"Remove all vertex paint from the Part, paint layers included (undoable).",
     "移除部件的所有頂點繪製，包括繪製圖層（可復原）。"},
    {"On a Part with no vertex paint, the colour above tints its Lit Sphere material, so you can try a skin, stone or "
     "metal tone without painting. Grey and black leave the material as it is. Vertex paint, once there, shows "
     "instead.",
     "在沒有頂點繪製的部件上，上方的色彩會為其 Lit Sphere 材質著色，讓你不必繪製就能試試皮膚、石頭或金屬的色調。灰色"
     "與黑色會讓材質維持原樣。有頂點繪製時則顯示頂點繪製。"},
    {"Use a Lit Sphere image of your own: a lit sphere filling a square PNG, JPG or EXR. It joins the list and is "
     "kept in Documents/SculptForge/Lit Spheres; images you put in that folder show up when a material list opens. "
     "Right-click one of your Lit Spheres to remove it.",
     "使用你自己的 Lit Sphere 影像：填滿正方形 PNG、JPG 或 EXR 的打光球體。它會加入清單並保存在 "
     "Documents/SculptForge/Lit Spheres；放進該資料夾的影像會在開啟材質清單時出現。在你的 Lit Sphere 上按右鍵即可移除"
     "。"},
    {"Viewport Shading", "視埠著色"},
    {"Environment", "環境"},
    {"The light around the model: Studio (generated), Photo Studio, Small Studio and Outdoor Sky. HDR files (.hdr, "
     ".exr) in the folders of SCULPTFORGE_HDRI_PATH are listed after them.",
     "模型周圍的光線：Studio（產生）、Photo Studio、Small Studio 與 Outdoor Sky。SCULPTFORGE_HDRI_PATH 資料夾中的 "
     "HDR 檔案 (.hdr、.exr) 會列在它們之後。"},
    {"Brightness of the light in stops: +1 doubles it.", "以級數表示的光線亮度：+1 為兩倍。"},
    {"Exposure %+.1f EV", "曝光 %+.1f EV"},
    {"Turns the environment about the up axis, moving the highlights.", "繞向上軸旋轉環境，移動高光位置。"},
    {"Tone map", "色調映射"},
    {"How bright light maps to the screen: ACES (filmic contrast), AgX (softer, keeps bright colours from going "
     "flat), Reinhard (gentle) or None.",
     "明亮光線映射到螢幕的方式：ACES（電影感對比）、AgX（較柔和，避免明亮色彩變平）、Reinhard（溫和）或無。"},
    {"Show environment", "顯示環境"},
    {"Show the environment behind the model instead of the background.", "在模型後方顯示環境，而非背景。"},
    {"How soft the environment behind the model is.", "模型後方環境的柔和程度。"},
    {"Roughness of Parts without a material texture (their colour comes from vertex paint or the base colour).",
     "沒有材質貼圖的部件的粗糙度（其色彩來自頂點繪製或基礎色）。"},
    {"Roughness %.2f", "粗糙度 %.2f"},
    {"Metallic of Parts without a material texture.", "沒有材質貼圖的部件的金屬度。"},
    {"Metallic %.2f", "金屬度 %.2f"},
    {"How much the sculpt's creases darken, so detail reads under even light. 0 is plain PBR.",
     "雕刻摺痕變暗的程度，讓細節在均勻光線下也清楚可見。0 為純 PBR。"},
    // --- PanelView.cpp (tools/i18n/coverage.py) ---
    {"See through the other Parts to the active one (again: solid). With only the active Part shown, it is "
     "see-through itself (#80, #91).",
     "透過其他部件看到目前部件（再按一次：實體）。只顯示目前部件時，它本身會變透明 (#80, #91)。"},
    {"Save Render writes a PNG (lossless).", "「儲存算繪」寫出 PNG（無損）。"},
    {"Save Render writes a JPG (smaller files, for sharing).", "「儲存算繪」寫出 JPG（檔案較小，方便分享）。"},
    {"Size", "大小"},
    {"Save Render's longer side: twice the view (at most 4096 pixels), or 1080, 1920, 2048 or 4096 pixels.",
     "「儲存算繪」的長邊：視圖的兩倍（最多 4096 像素），或 1080、1920、2048、4096 像素。"},
    {"Light", "光線"},
    {"Light direction", "光線方向"},
    {"Drag on the ball to move the light around the model (the dot is the light); double-click puts it back where the "
     "material has it. Lit Sphere and studio shading.",
     "在球上拖曳即可繞模型移動光線（圓點即光源）；按兩下可回到材質的原始位置。適用 Lit Sphere 與攝影棚著色。"},
    {"How bright the light is: 1 is the material as made.", "光線亮度：1 為材質原本的樣子。"},
    {"Ambient", "環境光"},
    {"Even light from everywhere that lifts the shadows, so the dark side still reads.",
     "來自各方向的均勻光線，提亮陰影，讓暗面仍清楚可見。"},
    {"Ambient %.2f", "環境光 %.2f"},
    {"The material's own light again: direction, intensity and no ambient.",
     "回到材質自己的光線：方向、強度，且無環境光。"},
    {"PBR: Viewport Shading's rotation and exposure", "PBR：視埠著色的旋轉與曝光"},
    // --- Pickers.cpp (tools/i18n/coverage.py) ---
    {"Sort", "排序"},
    {"quad remesh Guides: draw curves on the model for Quad Remesh's edge loops to follow (a curve that ends where it "
     "started makes a loop). Alt+click a curve to delete it.",
     "Quad Remesh 導引：在模型上畫曲線，讓 Quad Remesh 的邊環沿著它走（終點回到起點的曲線會形成環）。Alt+按一下曲線即"
     "可刪除。"},
    {"Brushes whose names start with this letter. Click to show only them.",
     "名稱以此字母開頭的筆刷。按一下即可只顯示這些筆刷。"},
    {"Brush packs", "筆刷套件"},
    {"How far spray dabs land from the cursor, as a fraction of the brush size.",
     "Spray 筆觸落在距游標多遠處，以筆刷大小的比例表示。"},
    {"Stamp this vector displacement map with the VDM brush (Drag Stamp).",
     "以 VDM 筆刷壓印此向量位移貼圖 (Drag Stamp)。"},
    {"Use any grayscale image as the alpha: white = full effect, black = none.",
     "使用任何灰階影像作為 Alpha：白色 = 完整效果，黑色 = 無效果。"},
    {"Load image alphas as their negative (black = full effect).", "以負片載入影像 Alpha（黑色 = 完整效果）。"},
    {"The vertex paint colour (C over the model picks one).", "頂點繪製色彩（在模型上按 C 可吸取）。"},
    {"Click to swap with the second colour.", "按一下即可與第二色彩交換。"},
    {"Click to swap it with the main colour.", "按一下即可與主要色彩交換。"},
    // --- Precision.cpp (tools/i18n/coverage.py) ---
    {"Steady radius %.0f px", "穩定半徑 %.0f px"},
    {"Steady radius", "穩定半徑"},
    {"Length of the string the brush trails on: longer = steadier lines.",
     "筆刷拖在後面的那條線的長度：越長線條越穩定。"},
    {"Precision Intensity %.0f", "精確強度 %.0f"},
    {"Precision Intensity", "精確強度"},
    {"Hold Shift+Alt while drawing: the brush moves at a quarter of the pen's speed for careful placement, and dabs "
     "at this intensity. Let go and it glides back under the pen.",
     "繪製時按住 Shift+Alt：筆刷以筆的四分之一速度移動以便精確放置，並以此強度落下筆觸。放開後會滑回筆下。"},
    {"Brush pressure curve", "筆刷壓力曲線"},
    {"How pen pressure (left to right) becomes this brush's pressure (bottom to top), after the tablet's curve. Each "
     "brush keeps its own. Drag a point; double-click the curve to add one (up to 5); right-click a point to remove "
     "it.",
     "筆壓（左到右）在經過繪圖板曲線後如何轉換為此筆刷的壓力（下到上）。每個筆刷各有自己的曲線。拖曳點即可調整；在曲"
     "線上按兩下可新增點（最多 5 個）；在點上按右鍵可移除。"},
    // --- RadialMenu.cpp (tools/i18n/coverage.py) ---
    {"Radial Menus", "圓餅選單"},
    {"Pie menus of your own: hold a menu's key (or its mouse or pen button), flick toward an item and let go to run "
     "it. Tap the key instead and the pie stays open: click an item, or beside the pie to close it. Up to 8 menus of "
     "8 items; an item is anything the command palette (F3) finds.",
     "你自己的圓餅選單：按住選單的按鍵（或其滑鼠、繪圖筆按鈕），朝項目方向一撥再放開即可執行。若只是輕按按鍵，選單會"
     "保持開啟：按一下項目，或按一下選單旁邊即可關閉。最多 8 個選單，每個 8 個項目；項目可以是命令面板 (F3) 能找到的"
     "任何東西。"},
    {"Add menu", "新增選單"},
    {"A new radial menu (up to 8). Give it a key, then items.", "新的圓餅選單（最多 8 個）。先指定按鍵，再加入項目。"},
    {"Key", "按鍵"},
    {"Click, then press the key that opens this menu (Esc cancels, Backspace clears). A key another action has moves "
     "here. Preferences > Hotkeys lists it too.",
     "按一下，再按下開啟此選單的按鍵（Esc 取消，Backspace 清除）。原屬其他動作的按鍵會移到這裡。偏好設定 > 快速鍵也會"
     "列出它。"},
    {"Button", "按鈕"},
    {"A mouse or pen button that opens this menu while held, besides the key. It then stops doing what it did before "
     "(a pen button's Preferences > Tablet mapping, the middle button's pan, the right button's turn). Mouse buttons "
     "open it only when no modifier key is held: Alt+middle still pans, Alt+right still zooms (Maya style), "
     "Shift+right still turns the view.",
     "按住時開啟此選單的滑鼠或繪圖筆按鈕，與按鍵並用。之後它不再執行原本的功能（繪圖筆按鈕在偏好設定 > 繪圖板中的對應"
     "、中鍵的平移、右鍵的旋轉）。滑鼠按鈕只在未按住修飾鍵時開啟選單：Alt+中鍵仍為平移，Alt+右鍵仍為縮放（Maya 風格）"
     "，Shift+右鍵仍會旋轉視圖。"},
    {"Item", "項目"},
    {"Click to change its label, icon or action.", "按一下即可變更其標籤、圖示或動作。"},
    {"Label", "標籤"},
    {"What the pie shows for it; empty: the action's own name.", "選單為它顯示的文字；留空：使用動作本身的名稱。"},
    {"Icon", "圖示"},
    {"The icon the pie shows for it.", "選單為它顯示的圖示。"},
    {"Save this menu as a .json file to share or keep (action names only).",
     "將此選單儲存為 .json 檔以分享或保留（只含動作名稱）。"},
    {"Load a menu someone exported. Only action names are read; ones this version doesn't have are left out. Goes "
     "into this menu if it's empty, otherwise into a new one.",
     "載入別人匯出的選單。只讀取動作名稱；此版本沒有的會略過。若此選單是空的就放入此選單，否則放入新選單。"},
    {"Empty this menu. Its key stays; an empty menu does nothing.", "清空此選單。按鍵會保留；空選單不會有任何作用。"},
    {"Restore default menus", "還原預設選單"},
    {"Back to the three built-in menus: Brushes (T), Masking (Y) and Strokes (J).",
     "回到三個內建選單：筆刷 (T)、遮罩 (Y) 與筆畫 (J)。"},
    // --- Shell.cpp (tools/i18n/coverage.py) ---
    {"Sculpt brushes also paint the colour.", "雕刻筆刷也會繪製色彩。"},
    {"Front faces only: brushes leave the far side of thin parts alone.", "僅限正面：筆刷不會影響薄部件的另一側。"},
    {"Brushes only reach the surface joined to the point under the cursor, within the brush size: the other leg, the "
     "next finger or a lip across a gap stay as they are (Blender's Connected Only). Saved with the preferences and "
     "the project.",
     "筆刷只影響與游標下的點相連、且在筆刷大小內的表面：另一條腿、旁邊的手指或隔著縫隙的嘴唇都保持不變（Blender 的 "
     "Connected Only）。隨偏好設定與專案儲存。"},
    {"The brush falloff is measured on screen: everything inside the brush circle is reached, through the model and "
     "out the back, as a cylinder along the view (Blender's Projected from View). Turn on Connected only too to keep "
     "it to one piece.",
     "筆刷衰減以螢幕測量：筆刷圓圈內的一切都會受影響，會穿過模型到背面，如同沿視線方向的圓柱（Blender 的 Projected "
     "from View）。同時開啟「僅限相連」即可限制在單一區塊。"},
    {"Ctrl+drag paints the mask with the brush; Ctrl+Alt+drag erases it. Ctrl+drag on the background clears it.",
     "Ctrl+拖曳以筆刷繪製遮罩；Ctrl+Alt+拖曳擦除遮罩。在背景上 Ctrl+拖曳可清除遮罩。"},
    {"Ctrl+drag draws a lasso and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.",
     "Ctrl+拖曳畫出套索並遮罩其中的一切；Ctrl+Alt+拖曳取消遮罩。包含背面。"},
    {"Ctrl+drag draws a rectangle and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.",
     "Ctrl+拖曳畫出矩形並遮罩其中的一切；Ctrl+Alt+拖曳取消遮罩。包含背面。"},
    {"Ctrl+Shift+drag a rectangle to keep what's inside and hide the rest; add Alt to hide the inside.",
     "按住 Ctrl+Shift 拖曳矩形，保留其中的部分並隱藏其餘；加按 Alt 則隱藏內部。"},
    {"Ctrl+Shift+drag a lasso to keep what's inside and hide the rest; add Alt to hide the inside.",
     "按住 Ctrl+Shift 拖曳套索，保留其中的部分並隱藏其餘；加按 Alt 則隱藏內部。"},
    {"Ctrl+Shift+drag a line: the shaded side is cut (Alt: the other side). Away removes it through the whole Part "
     "and closes the cut flat; In steps it down by Depth; Add raises it; Clip presses it flat onto the line.",
     "按住 Ctrl+Shift 拖曳一條線：切割陰影側（Alt：另一側）。移除會穿過整個部件去除並以平面封閉切口；凹入會依深度降低"
     "；增加會使其隆起；裁平會將其壓平到線上。"},
    {"Ctrl+Shift+drag a lasso: Away cuts a hole through the Part, In a pocket Depth deep, Add raises the shape, Clip "
     "presses what lies outside it onto its outline.",
     "按住 Ctrl+Shift 拖曳套索：移除會在部件上切出貫穿的孔，凹入會挖出深度為「深度」的凹槽，增加會讓形狀隆起，裁平會"
     "將外側部分壓平到其輪廓上。"},
    {"Ctrl+Shift+drag a rectangle: Away cuts a hole through the Part, In a pocket Depth deep, Add a block, Clip "
     "presses what lies outside it onto its edges.",
     "按住 Ctrl+Shift 拖曳矩形：移除會在部件上切出貫穿的孔，凹入會挖出深度為「深度」的凹槽，增加會加出方塊，裁平會將"
     "外側部分壓平到其邊緣上。"},
    {"Ctrl+Shift+click points: click the first point to cut the polygon, double-click or Enter to cut along an open "
     "bent line (Alt: the other side), Esc to drop it.",
     "按住 Ctrl+Shift 並按一下各點：按一下第一個點即可切割多邊形，按兩下或 Enter 可沿開放折線切割（Alt：另一側），"
     "Esc 放棄。"},
    {"Away", "移除"},
    {"Remove what the shape covers, through the whole Part, and close the cut with a flat cap.",
     "去除形狀覆蓋的部分（貫穿整個部件），並以平面封蓋封閉切口。"},
    {"In", "凹入"},
    {"Cut a flat-bottomed pocket Depth deep where the shape covers the Part.",
     "在形狀覆蓋部件之處切出平底、深度為「深度」的凹槽。"},
    {"Add", "增加"},
    {"Add the shape, extruded Depth out of the surface, joined to the Part.",
     "加上形狀，從表面擠出「深度」的距離，並與部件相連。"},
    {"Clip", "裁平"},
    {"Remove nothing: press the shaded side of a line, or everything outside a lasso or rectangle, flat onto the "
     "shape's outline, through the whole Part (#102). Hard-surface planes and clean silhouettes; the mask holds "
     "parts back.",
     "不移除任何東西：將線的陰影側，或套索、矩形外的一切，貫穿整個部件壓平到形狀的輪廓上 (#102)。適合硬表面平面與乾淨"
     "的剪影；遮罩可固定部分區域。"},
    {"Pen pressure scales the brush strength.", "筆壓調整筆刷強度。"},
    {"Pen pressure scales the brush size.", "筆壓調整筆刷大小。"},
    {"Mirror strokes across X (left and right).", "沿 X 鏡像筆畫（左與右）。"},
    {"Mirror strokes across Y (top and bottom).", "沿 Y 鏡像筆畫（上與下）。"},
    {"Mirror strokes across Z (front and back).", "沿 Z 鏡像筆畫（前與後）。"},
    {"Radial symmetry: repeat strokes around an axis (Radial Copies copies, Transform palette).",
     "放射對稱：繞軸重複筆畫（複本數為「放射複本」，在變形調色盤中）。"},
    {"Local symmetry: mirror and turn strokes about the active Part's centre instead of the world origin.",
     "區域對稱：以目前部件的中心而非世界原點鏡像與旋轉筆畫。"},
    {"Browse, search and install brushes other sculptors shared.", "瀏覽、搜尋並安裝其他雕刻者分享的筆刷。"},
    {"Wire", "線框"},
    {"Wireframe: show the polygons of the active Part (Shift+F).", "線框：顯示目前部件的多邊形 (Shift+F)。"},
    {"Colour each face group of the active Part.", "為目前部件的每個面群組上色。"},
    {"Show or hide the mask (masked areas drawn darker). Hiding it keeps the mask: masked areas stay protected from "
     "brushes until you clear it.",
     "顯示或隱藏遮罩（遮罩區域會畫得較暗）。隱藏時遮罩仍保留：在你清除之前，遮罩區域仍受保護不受筆刷影響。"},
    {"Show vertex paint colours (off: plain material).", "顯示頂點繪製色彩（關閉：純材質）。"},
    {"Draw the sculpt.", "繪製雕刻。"},
    {"Open this menu. Its sections are the palettes of the Classic layout.",
     "開啟此選單。其各節即經典版面配置的調色盤。"},
    {"Camera locked", "相機已鎖定"},
    {"Find any command, palette, brush or material by name and run it.",
     "依名稱尋找任何命令、調色盤、筆刷或材質並執行。"},
    {"Click to open or close the tray, drag to resize it. Palettes go in trays from their header icon.",
     "按一下即可開啟或關閉工具匣，拖曳可調整大小。調色盤可透過其標題圖示放入工具匣。"},
    {"Shell", "界面"},
    {"Brush radius in screen pixels (0.5 to 800), and in mm on the surface under the cursor. [ and ] change it by "
     "10%, Shift+[ and Shift+] by 1 px, Ctrl+Shift+right-drag scrubs it finely; Ctrl+click to type.",
     "以螢幕像素表示的筆刷半徑（0.5 到 800），以及游標下表面上的 mm。[ 與 ] 每次調整 10%, Shift+[ 與 Shift+] 每次 1 "
     "px，Ctrl+Shift+右鍵拖曳可細微調整；按住 Ctrl 並按一下即可輸入。"},
    {"Triangles you can see and sculpt (visible Parts, hidden parts left out) and in the whole scene. Micro: the "
     "micro triangles of the visible Parts' micro levels (Geometry > Micro).",
     "可見且可雕刻的三角面數（可見部件，不含隱藏部分）以及整個場景的三角面數。微細分：可見部件微細分層級中的微三角面"
     "（幾何 > 微細分）。"},
    {"Ctrl held: the mask brush (Ctrl+Shift: the selection brush). Click, or press B with Ctrl held, to pick Mask "
     "Pen, Lasso Mask or Rectangle Mask (Select Rectangle or Select Lasso).",
     "按住 Ctrl：遮罩筆刷（Ctrl+Shift：選取筆刷）。按一下，或按住 Ctrl 再按 B，即可選擇 Mask Pen、Lasso Mask 或 "
     "Rectangle Mask（Select Rectangle 或 Select Lasso）。"},
    {"Ctrl held: the mask's stroke, Dots (Mask Pen), Lasso or Rect (Ctrl+Shift: the selection's). Click with Ctrl "
     "held to choose.",
     "按住 Ctrl：遮罩的筆畫，Dots (Mask Pen)、Lasso 或 Rect（Ctrl+Shift：選取的筆畫）。按住 Ctrl 再按一下即可選擇。"},
    // --- SpaceMouse.cpp (tools/i18n/coverage.py) ---
    {"Copy device report", "複製裝置報告"},
    {"Copies what SculptForge sees (each joystick's name, USB ids and why it was or wasn't used) to paste into a bug "
     "report.",
     "複製 SculptForge 偵測到的內容（每個搖桿的名稱、USB ID 以及是否使用的原因），以貼到錯誤回報中。"},
    {"Experimental. A 3Dconnexion SpaceMouse moves the view with one hand while the other holds the pen: push, pull "
     "and twist the cap to pan, zoom and turn the model about the pivot. Read directly as a USB device, without "
     "3Dconnexion's driver; tested only with simulated input so far.",
     "實驗性功能。3Dconnexion SpaceMouse 讓你一手移動視圖、另一手握筆：推、拉與扭轉控制帽即可平移、縮放，並繞樞紐點旋"
     "轉模型。直接以 USB 裝置讀取，不需要 3Dconnexion 的驅動程式；目前只以模擬輸入測試過。"},
    {"Experimental. Off: the SpaceMouse is ignored.", "實驗性功能。關閉：忽略 SpaceMouse。"},
    {"How fast the view moves for the same push on the cap.", "對控制帽施加相同推力時視圖移動的速度。"},
    {"How far the cap must move before the view does, so a hand resting on it or a slightly off-centre cap doesn't "
     "drift the view.",
     "控制帽需移動多少視圖才會跟著動，讓放在上面的手或略微偏離中心的控制帽不會使視圖飄移。"},
    {"The model stays upright: twisting turns it about the world's up axis, tilting looks over or under it, and "
     "rolling the cap does nothing.",
     "模型保持直立：扭轉會繞世界的向上軸旋轉，傾斜可從上方或下方觀看，滾轉控制帽則不起作用。"},
    {"Hold during strokes", "筆畫期間保持"},
    {"While you sculpt, paint or mask, the SpaceMouse leaves the view alone and its buttons do nothing, so a hand "
     "resting on the cap never moves the model under the pen. Off: the view turns mid-stroke.",
     "在雕刻、繪製或遮罩時，SpaceMouse 不會移動視圖，其按鈕也不起作用，因此放在控制帽上的手絕不會讓筆下的模型移動。關"
     "閉：筆畫進行中視圖也會旋轉。"},
    {"Only the direction you push the most moves the view: a twist doesn't also pan a little.",
     "只有推力最大的方向會移動視圖：扭轉時不會順帶平移一點。"},
    {"Move the view the other way for this push or turn of the cap.", "對控制帽的此推力或扭轉，反向移動視圖。"},
    {"What the SpaceMouse reports now: Pan X, Zoom, Pan Y, Tilt, Roll, Spin. Grey: inside the dead zone.",
     "SpaceMouse 目前回報的值：Pan X、Zoom、Pan Y、Tilt、Roll、Spin。灰色：位於死區內。"},
    {"On, speed 1, dead zone 8%, nothing inverted, free rotation, held during strokes, all axes at once; left button "
     "Frame, right button Front view.",
     "開啟，速度 1，死區 8%, 不反轉，自由旋轉，筆畫期間保持，所有軸同時作用；左鍵為置入視圖，右鍵為正視圖。"},
    // --- StrokePopup.cpp (tools/i18n/coverage.py) ---
    {"Stroke popup", "筆畫彈出視窗"},
    {"A grid at the cursor with the stroke types, the modifiers (Add / Sub, Smooth, Mask, Select) and the mask and "
     "Cut shapes. Hold the key, point at one and let go; or tap the key and click. Smooth, Mask, Select and the "
     "shapes stay on without holding Shift or Ctrl, until you pick them again or another brush.",
     "在游標處顯示的方格，含筆畫類型、修飾（Add／Sub、Smooth、Mask、Select）與遮罩及切割形狀。按住按鍵、指向其中一個"
     "後放開；或輕按按鍵再按一下。Smooth、Mask、Select 與形狀不需按住 Shift 或 Ctrl 就會保持開啟，直到你再次選取它們"
     "或選取其他筆刷。"},
    // --- Tablet.cpp (tools/i18n/coverage.py) ---
    {"Pressure", "壓力"},
    {"No pressure? In Wacom Tablet Properties > Mapping, switch \"Use Windows Ink\" (on for most tablets, off for "
     "some older drivers), then bring the pen back. Without pressure the pen draws at full strength.",
     "沒有壓力？在 Wacom Tablet Properties > Mapping 中切換「Use Windows Ink」（大多數繪圖板開啟，部分較舊的驅動程式"
     "關閉），再將筆移回來。沒有壓力時筆會以全強度繪製。"},
    {"Whether the pen reports pressure. Without it the pen draws at full strength; check the tablet in your desktop's "
     "tablet settings.",
     "繪圖筆是否回報壓力。沒有壓力時筆會以全強度繪製；請在桌面環境的繪圖板設定中檢查繪圖板。"},
    {"The pen SculptForge last saw, and whether it is in range or touching.",
     "SculptForge 最後偵測到的繪圖筆，以及是否在感應範圍內或正在接觸。"},
    {"Turn on if the tablet driver is set to Mouse mode: the pen moves the cursor by how far it moves, and lifting it "
     "and putting it down elsewhere leaves the cursor where it was. Off (Pen mode): the cursor is where the pen is.",
     "若繪圖板驅動程式設為滑鼠模式請開啟：筆依移動距離移動游標，抬起後放到別處時游標維持原位。關閉（筆模式）：游標就"
     "在筆的位置。"},
    {"The pen's pressure as the tablet reports it, and after the pressure curve below (what brushes get).",
     "繪圖板回報的筆壓，以及經過下方壓力曲線後的值（筆刷收到的值）。"},
    {"The pen's tilt, as the tablet reports it (brushes don't use it yet).",
     "繪圖板回報的筆傾斜角度（筆刷尚未使用）。"},
    {"Draw here with the pen: the stroke is as thick as the brush pressure, and the line at the bottom plots it along "
     "the stroke.",
     "在此用筆繪製：筆畫粗細等於筆刷壓力，下方的線會沿筆畫繪出壓力變化。"},
    {"How pen pressure (left to right) becomes brush pressure (bottom to top), before each brush's own curve. The dot "
     "is the pen right now.",
     "筆壓（左到右）在套用各筆刷自己的曲線之前如何轉換為筆刷壓力（下到上）。圓點為筆目前的狀態。"},
    {"Below 1: soft, a light touch gives a lot. Above 1: firm, press harder.",
     "低於 1：較軟，輕觸就有很大效果。高於 1：較硬，需更用力按壓。"},
    {"Brush pressure at the lightest touch.", "最輕觸碰時的筆刷壓力。"},
    {"Brush pressure when pressing fully.", "完全按壓時的筆刷壓力。"},
    {"Pen pressure below this counts as the lightest touch (ignores a resting hand).",
     "低於此值的筆壓視為最輕觸碰（忽略放在上面的手）。"},
    {"Pen pressure that already counts as pressing fully (for a light hand).",
     "達到此值即視為完全按壓的筆壓（適合手勁較輕的人）。"},
    {"Eraser", "橡皮擦"},
    {"What the pen's eraser end does. Invert: brushes subtract, like holding Alt (with Ctrl it erases mask). Erase "
     "mask: removes mask wherever it draws.",
     "筆尾橡皮擦的功能。反轉：筆刷改為減去，如同按住 Alt（搭配 Ctrl 則擦除遮罩）。擦除遮罩：在繪製處移除遮罩。"},
    {"Eraser: Invert; lower button: Right-click; upper button: Invert (Alt); third button: Pan view. The curve and "
     "Mouse mode stay.",
     "橡皮擦：反轉；下方按鈕：右鍵按一下；上方按鈕：反轉 (Alt)；第三個按鈕：平移視圖。曲線與滑鼠模式保持不變。"},
    {"These settings save themselves.", "這些設定會自動儲存。"},
    // --- UserGuide.cpp (tools/i18n/coverage.py) ---
    {"Go back to the page you were reading.", "回到你正在閱讀的頁面。"},
    {"Back (Alt+Left)", "上一頁 (Alt+Left)"},
    {"Go forward again.", "再次前進。"},
    {"Forward (Alt+Right)", "下一頁 (Alt+Right)"},
    {"The guide's first page: every chapter.", "指南的第一頁：所有章節。"},
    {"Find every place in the guide that mentions all of these words.", "尋找指南中提到所有這些字詞的每個地方。"},
    {"Clear the search", "清除搜尋"},
    {"Read this page on the SculptForge website.", "在 SculptForge 網站上閱讀此頁面。"},
    // --- Shell.cpp (tools/i18n/coverage.py) ---
    {"Brushes change the shape. Turn on Rgb to paint colour at the same time.",
     "筆刷改變形狀。開啟 Rgb 即可同時繪製色彩。"},
    {"Paint colour without sculpting. Alt paints back to white, Shift still smooths.",
     "只繪製色彩而不雕刻。Alt 會繪回白色，Shift 仍為平滑。"},
    {"Brushes add to the surface. Hold Alt to subtract for one stroke.",
     "筆刷向表面增加。按住 Alt 可在單一筆畫中減去。"},
    {"Sub", "減少"},
    {"Brushes subtract from the surface. Hold Alt to add for one stroke.",
     "筆刷從表面減去。按住 Alt 可在單一筆畫中增加。"},
    {"Dynamic topology: brushes add triangles where they need detail and remove them where they don't, so you can "
     "pull out horns or fingers without stretching. Detail sets how small the triangles get; zoom in or shrink the "
     "brush for finer ones. Needs a mesh without subdivision levels; quads under the brush become triangles and UVs "
     "are dropped (Undo brings them back).",
     "動態拓樸：筆刷在需要細節處加入三角面，不需要處則移除，讓你拉出角或手指而不會拉伸。「細節」設定三角面的大小；拉"
     "近視圖或縮小筆刷可得到更細的三角面。需要沒有細分層級的網格；筆刷下的四邊面會變成三角面，UV 會被捨棄（復原可找回"
     "）。"},
    {"Lit Sphere shading (the default): the chosen material sphere or the studio lights, made for reading forms while "
     "sculpting.",
     "Lit Sphere 著色（預設）：所選的材質球或攝影棚燈光，便於雕刻時判讀形體。"},
    {"PBR shading: the texture's material channels (base colour, roughness, metallic, normal, height, AO, emissive) "
     "lit by an environment, as a game engine or renderer shows them. Meshes without a material use the roughness "
     "and metallic set here.",
     "PBR 著色：貼圖的材質通道（基礎色、粗糙度、金屬度、法線、高度、AO、自發光）由環境照亮，如同遊戲引擎或算繪器的呈"
     "現。沒有材質的網格使用此處設定的粗糙度與金屬度。"},
    {"Smooth", "平滑"},
    {"Smooth shading (the default): normals blend across polygons, so the surface reads as one form.",
     "平滑著色（預設）：法線在多邊形之間混合，讓表面看起來是一個整體形體。"},
    {"Flat shading: every polygon shows its own face, so the mesh's structure and each stroke's effect on it read "
     "clearly. Parts set to Smooth or Flat keep their own. A preference, saved automatically.",
     "平面著色：每個多邊形都顯示自己的面，讓網格結構與每一筆畫的效果清楚可見。設為平滑或平面的部件保有自己的設定。這"
     "是偏好設定，自動儲存。"},
    {"The view's background is the charcoal radial gradient (the default).", "視圖背景為炭灰色放射漸層（預設）。"},
    {"The view's background is one solid colour (pick it below), without the gradient. A preference, saved "
     "automatically; projects keep their own.",
     "視圖背景為單一純色（在下方選擇），沒有漸層。這是偏好設定，自動儲存；專案會保有自己的設定。"},
    {"Fill the screen with SculptForge (again: back to a window).", "讓 SculptForge 填滿螢幕（再按一次：回到視窗）。"},
    {"Show the Gaussian splat capture.", "顯示高斯 Splat 擷取。"},
    {"Show only the active Part (again: show the others).", "只顯示目前部件（再按一次：顯示其他部件）。"},
    {"Drag on this button to pan the view (or Alt+drag the background).",
     "在此按鈕上拖曳即可平移視圖（或 Alt+拖曳背景）。"},
    {"Drag up and down on this button to zoom (or use the mouse wheel).",
     "在此按鈕上上下拖曳即可縮放（或使用滑鼠滾輪）。"},
    {"Drag on this button to orbit the view (or drag the background).", "在此按鈕上拖曳即可環繞視圖（或拖曳背景）。"},
    {"Add a model file as a new Part.", "將模型檔加入為新部件。"},
    {"Save the visible Parts as Wavefront OBJ.", "將可見部件儲存為 Wavefront OBJ。"},
    {"One GLB per level of detail.", "每個細節層級一個 GLB。"},
    {"OpenUSD for Apple, Omniverse and film pipelines.", "適用於 Apple、Omniverse 與電影流程的 OpenUSD。"},
    {"FBX sized and named for Unreal Engine, with LODs.", "依 Unreal Engine 調整大小與命名的 FBX，含 LOD。"},
    {"Save the Gaussian splats (PLY, glTF or USD).", "儲存高斯 Splat（PLY、glTF 或 USD）。"},
    {"Close SculptForge (asks to save changes first).", "關閉 SculptForge（會先詢問是否儲存變更）。"},
    {"Rebuild as an even, watertight surface (like Voxel Remesh): merges parts, fixes scans and generated models.",
     "重建為均勻、封閉的表面（類似體素重新網格化）：合併各部分，修復掃描與生成的模型。"},
    {"With Tool > Remesh > Quick Remesh on: remesh the active Part at the Voxel Remesh resolution, in the background.",
     "開啟工具 > 重新網格化 > Quick Remesh 時：在背景中以體素重新網格化的解析度重新網格化目前部件。"},
    {"Quick Remesh (hold and drag)", "Quick Remesh（按住並拖曳）"},
    {"With Tool > Remesh > Quick Remesh on: hold this key and click or drag on the background (not on the model) to "
     "remesh the active Part.",
     "開啟工具 > 重新網格化 > Quick Remesh 時：按住此鍵並在背景上（不是模型上）按一下或拖曳，即可重新網格化目前部件。"},
    {"Turn the sculpt into Gaussian splats.", "將雕刻轉為高斯 Splat。"},
    {"Fill the unmasked area with the colour.", "以該色彩填滿未遮罩的區域。"},
    {"Hide or show the shelves and trays around the canvas.", "隱藏或顯示畫布周圍的工具列與工具匣。"},
    {"Show or hide the palette bar under the title.", "顯示或隱藏標題下方的調色盤列。"},
    {"Open or close the left tray (or click its divider).", "開啟或關閉左側工具匣（或按一下其分隔線）。"},
    {"Open or close the right tray (or click its divider).", "開啟或關閉右側工具匣（或按一下其分隔線）。"},
    {"Menus by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help), a status bar and a one-row tool shelf.",
     "依工作分類的選單（檔案、編輯、雕刻、繪製、網格、檢視、算繪、說明）、狀態列與單列工具列。"},
    {"The alphabetical palette bar and the two-row top shelf, as experienced sculptors expect.",
     "依字母排列的調色盤列與雙列頂部工具列，符合資深雕刻者的習慣。"},
    {"Palettes", "調色盤"},
    // --- Config.cpp (tools/i18n/coverage.py) ---
    {"Press a key for '%s'", "請按下「%s」的按鍵"},
    {"Now:", "目前："},
    {"none", "無"},
    {"Esc cancels, Backspace clears. A key that's taken moves here.",
     "Esc 取消，Backspace 清除。已被使用的按鍵會移到這裡。"},
    {"No shortcut matches \"%s\".", "沒有符合「%s」的快速鍵。"},
    {"Double-click a row, or Ctrl+Alt+click any button, to assign a key.",
     "按兩下某一列，或 Ctrl+Alt+按一下任何按鈕，即可指派按鍵。"},
    // --- Input.cpp (tools/i18n/coverage.py) ---
    {"With Alt", "搭配 Alt"},
    {"With Ctrl", "搭配 Ctrl"},
    // --- PanelUvEditor.cpp (tools/i18n/coverage.py) ---
    {"On model", "模型上"},
    {"Seams (%zu edges)", "接縫（%zu 條邊）"},
    {"Select", "選取"},
    // --- RadialMenu.cpp (tools/i18n/coverage.py) ---
    {"Right-drag opens this menu. Turn the view with %s.", "右鍵拖曳會開啟此選單。以 %s 旋轉視圖。"},
    {"Action's own", "動作本身的"},
    {"Change action", "變更動作"},
    {"Add item", "新增項目"},
    {"No matching actions", "沒有符合的動作"},
    {"Your menus save themselves", "你的選單會自動儲存"},
    // --- SpaceMouse.cpp (tools/i18n/coverage.py) ---
    {"No SpaceMouse found", "找不到 SpaceMouse"},
    {"Last button pressed: %d", "最後按下的按鈕：%d"},
    // --- Tablet.cpp (tools/i18n/coverage.py) ---
    {"Pressure: no pen seen yet", "壓力：尚未偵測到繪圖筆"},
    {"Pressure: on", "壓力：已啟用"},
    {"Pressure: not detected", "壓力：未偵測到"},
    {"Pressure: touch the tablet to check", "壓力：請觸碰繪圖板以檢查"},
    {"Troubleshooting", "疑難排解"},
    {"No pen seen yet", "尚未偵測到繪圖筆"},
    {"Tilt: not reported by this pen", "傾斜：此繪圖筆不回報"},
    {"Pressure curve (all brushes)", "壓力曲線（所有筆刷）"},
    // --- UserGuide.cpp (tools/i18n/coverage.py) ---
    {"SculptForge is free and open source, and stays that way. If it helps your work, you can support its development.",
     "SculptForge 是自由且開放原始碼的軟體，並將一直如此。如果它對你的工作有幫助，歡迎支持它的開發。"},
    {"Nothing in the guide matches \"%s\".", "指南中沒有符合「%s」的內容。"},
    // --- core/RadialMenu.cpp (tools/i18n/coverage.py) ---
    {"None", "無"},
    {"Middle mouse button", "滑鼠中鍵"},
    {"Mouse back button", "滑鼠上一頁鍵"},
    {"Mouse forward button", "滑鼠下一頁鍵"},
    {"Pen lower button", "繪圖筆下方按鈕"},
    {"Pen upper button", "繪圖筆上方按鈕"},
    {"Pen third button", "繪圖筆第三個按鈕"},
    {"Right mouse button", "滑鼠右鍵"},
    // --- PanelUvEditor.cpp (tools/i18n/coverage.py) ---
    {" (folds)", " （翻折）"},
    {"%zu seam edges marked", "已標記 %zu 條接縫邊"},
    {"%u islands  |  %zu selected  |  stretch %.3f%s  |  %.0f tx/u at %dK  |  %s",
     "%u 個島  |  已選取 %zu  |  拉伸 %.3f%s  |  %.0f tx/u（%dK）  |  %s"},
    {"Density", "密度"},
    {"Edge", "邊"},
    {"Face", "面"},
    {"Island", "島"},
    {"Islands", "島"},
    {"No Image", "無影像"},
    {"Plain", "單色"},
    {"Selected", "已選取"},
    {"Stretch", "拉伸"},
    {"Texel Density", "紋素密度"},
    {"Trim Sheet", "裝飾貼圖"},
    {"Unwrap Selected", "展開所選"},
    // --- Brush palette (tools/i18n/coverage.py) ---
    {"Filter brushes", "篩選筆刷"},
    {"Alphas", "Alpha"},
    {"Built-in", "內建"},
    {"Custom", "自訂"},
    {"Custom brushes", "自訂筆刷"},
    {"Larger brush by 1 px", "筆刷放大 1 px"},
    {"Smaller brush by 1 px", "筆刷縮小 1 px"},
    {"On", "開啟"},
    {"Use global", "使用全域"},
    {"Reset Brush to Default", "重設筆刷為預設"},
    {"Save Brush Settings", "儲存筆刷設定"},
    {"Sort: Recent", "排序：最近"},
    {"Stamp rotation", "印章旋轉"},
    {"Vector displacement", "向量位移"},
    {"World Size", "世界尺寸"},

    // --- Help > Connect an AI tool (PanelConnectAi.cpp, ConnectAi.cpp) ---
    {"Add to Cursor's global MCP file (or .cursor/mcp.json in a project).",
     "加入 Cursor 的全域 MCP 檔案（或專案中的 .cursor/mcp.json）。"},
    {"Add to Kiro's user MCP file.",
     "加入 Kiro 的使用者 MCP 檔案。"},
    {"Add to Rovo Dev's MCP file (acli rovodev mcp opens it).",
     "加入 Rovo Dev 的 MCP 檔案（acli rovodev mcp 會開啟它）。"},
    {"Add to Zed's settings file (command palette: zed: open settings file).",
     "加入 Zed 的設定檔（命令面板：zed: open settings file）。"},
    {"Add to opencode.json.",
     "加入 opencode.json。"},
    {"Add to the global MCP file, then use it from Copilot Chat in Agent mode.",
     "加入全域 MCP 檔案，然後在 Copilot Chat 的 Agent 模式中使用。"},
    {"Add to:",
     "加入："},
    {"Add under extensions in Goose's configuration file (or goose configure > Add Extension > Command-Line Extension).",
     "加入 Goose 設定檔的 extensions 下（或 goose configure > Add Extension > Command-Line Extension）。"},
    {"Claude Code uses your Pro, Max or Ultra plan when you sign in with your Claude account. If ANTHROPIC_API_KEY is set, it uses that key and API billing instead.",
     "以 Claude 帳號登入時，Claude Code 會使用你的 Pro、Max 或 Ultra 方案。若設定了 ANTHROPIC_API_KEY，則改用該金鑰並按 API 計費。"},
    {"Claude on the web and on phones can't start programs on your computer: use Claude Desktop or Claude Code.",
     "網頁版與手機版 Claude 無法啟動你電腦上的程式：請使用 Claude Desktop 或 Claude Code。"},
    {"Connect an AI tool",
     "連接 AI 工具"},
    {"Connect an AI tool...",
     "連接 AI 工具..."},
    {"Copied to the clipboard",
     "已複製到剪貼簿"},
    {"Download the Claude Desktop bundle",
     "下載 Claude Desktop 套件"},
    {"Fields:",
     "欄位："},
    {"For Gemini Code Assist Standard or Enterprise, or a Gemini API key. Run once in a terminal.",
     "適用於 Gemini Code Assist Standard 或 Enterprise，或 Gemini API 金鑰。在終端機執行一次。"},
    {"Google's CLI for personal Google accounts (free, Google AI Pro and Ultra). Add this to its MCP file, then sign in with Google.",
     "Google 為個人 Google 帳號（免費、Google AI Pro 與 Ultra）提供的 CLI。將此加入其 MCP 檔案，然後以 Google 登入。"},
    {"I have a subscription to",
     "我訂閱了"},
    {"In a terminal:",
     "在終端機中："},
    {"Let an AI assistant use SculptForge's tools: it starts sculptforge-cli on this computer and talks to it directly. Nothing listens on the network. The assistant works on its own scene and saves files where you ask; open them here to see the result.",
     "讓 AI 助理使用 SculptForge 的工具：它會在這台電腦上啟動 sculptforge-cli 並直接與之溝通。沒有任何東西在網路上監聽。助理在自己的場景中工作，並把檔案存到你指定的位置；在這裡開啟即可查看結果。"},
    {"Download the bundle for your system and install it in Claude Desktop under Settings > Extensions > Advanced settings. Or add SculptForge to its configuration file and restart it.",
     "下載適合你系統的套件，並在 Claude Desktop 的 Settings > Extensions > Advanced settings 中安裝。或將 SculptForge 加入其設定檔後重新啟動。"},
    {"Other tools",
     "其他工具"},
    {"Program tab > Install > Edit mcp.json, then paste this.",
     "Program 分頁 > Install > Edit mcp.json，然後貼上這段。"},
    {"Rovo in Atlassian's web apps only takes public HTTPS servers added by an admin: use Rovo Dev CLI.",
     "Atlassian 網頁應用程式中的 Rovo 只接受由管理員加入的公開 HTTPS 伺服器：請使用 Rovo Dev CLI。"},
    {"Run once in a terminal (Cline CLI), or paste into Cline's MCP settings (MCP Servers > Configure).",
     "在終端機執行一次（Cline CLI），或貼到 Cline 的 MCP 設定（MCP Servers > Configure）。"},
    {"Run once in a terminal, or add to the user MCP file.",
     "在終端機執行一次，或加入使用者 MCP 檔案。"},
    {"Run once in a terminal, or open the command MCP: Open User Configuration and add the entry.",
     "在終端機執行一次，或開啟命令 MCP: Open User Configuration 並加入此項目。"},
    {"Run once in a terminal, then choose Sign in with ChatGPT when Codex starts.",
     "在終端機執行一次，然後在 Codex 啟動時選擇 Sign in with ChatGPT。"},
    {"Run once in a terminal.",
     "在終端機執行一次。"},
    {"Run once in a terminal; it signs in with your Claude plan.",
     "在終端機執行一次；它會以你的 Claude 方案登入。"},
    {"Save as a file in your workspace's .continue/mcpServers folder; tools work in agent mode.",
     "存成工作區 .continue/mcpServers 資料夾中的檔案；工具在 agent 模式下運作。"},
    {"Settings > Agents > MCP servers > Add, then paste this.",
     "Settings > Agents > MCP servers > Add，然後貼上這段。"},
    {"Settings > MCP Servers > +, then fill in these fields.",
     "Settings > MCP Servers > +，然後填入這些欄位。"},
    {"Settings > Tools > AI Assistant > Model Context Protocol (MCP): Add, choose STDIO and paste this.",
     "Settings > Tools > AI Assistant > Model Context Protocol (MCP)：Add，選擇 STDIO 並貼上這段。"},
    {"Since June 2026 personal Google accounts (free, Google AI Pro, Google AI Ultra) use Antigravity CLI; Gemini CLI is for Gemini Code Assist and API keys.",
     "自 2026 年 6 月起，個人 Google 帳號（免費、Google AI Pro、Google AI Ultra）使用 Antigravity CLI；Gemini CLI 則用於 Gemini Code Assist 與 API 金鑰。"},
    {"The ChatGPT app only connects to remote (HTTPS) servers, and SculptForge doesn't run one: nothing of yours listens on the network. Codex CLI runs on your computer and signs in with your ChatGPT plan.",
     "ChatGPT 應用程式只連接遠端（HTTPS）伺服器，而 SculptForge 不執行這種伺服器：你的電腦上沒有任何東西在網路上監聽。Codex CLI 在你的電腦上執行，並以你的 ChatGPT 方案登入。"},
    {"The Grok app (web, phone, X) can't reach programs on your computer, only public HTTPS servers: use Grok Build. xAI's documentation doesn't say which plans Grok Build's sign-in includes; it also takes an xAI API key.",
     "Grok 應用程式（網頁、手機、X）無法連到你電腦上的程式，只能連公開的 HTTPS 伺服器：請使用 Grok Build。xAI 的文件沒有說明 Grok Build 的登入包含哪些方案；它也接受 xAI API 金鑰。"},
    {"Their documentation",
     "官方文件"},
    {"This install:",
     "此安裝："},
    {"Turn this off in Preferences > AI.",
     "可在偏好設定 > AI 中關閉。"},
    {"Use SculptForge from Claude, ChatGPT (Codex), Gemini, Grok, Cursor, VS Code and other AI tools: the exact command or settings for this install, with Copy buttons. The tool starts SculptForge on this computer; nothing listens on the network.",
     "從 Claude、ChatGPT（Codex）、Gemini、Grok、Cursor、VS Code 等 AI 工具使用 SculptForge：此安裝的確切命令或設定，附複製按鈕。該工具會在這台電腦上啟動 SculptForge；沒有任何東西在網路上監聽。"},
    {"User guide: AI agents",
     "使用者指南：AI 代理"},
    {"Your own agent in Python (uses the OpenAI API with an API key).",
     "你自己的 Python 代理（以 API 金鑰使用 OpenAI API）。"},
    {"Your own code with the OpenAI, Gemini or xAI API: sculptforge-cli --schema prints every tool as JSON (examples in the user guide, Scripting > AI agents).",
     "以 OpenAI、Gemini 或 xAI API 撰寫你自己的程式：sculptforge-cli --schema 會把每個工具輸出為 JSON（範例見使用者指南，Scripting > AI agents）。"},
    {"Your own code with the xAI API: sculptforge-cli --schema gives the tool definitions (examples in the user guide, Scripting > AI agents). API usage is billed by xAI.",
     "以 xAI API 撰寫你自己的程式：sculptforge-cli --schema 提供工具定義（範例見使用者指南，Scripting > AI agents）。API 用量由 xAI 計費。"},
    {"xAI's coding agent (the grok command). Run once in a terminal; it signs in with your xAI account in the browser.",
     "xAI 的程式設計代理（grok 命令）。在終端機執行一次；它會在瀏覽器中以你的 xAI 帳號登入。"},
};

} // namespace

std::span<const Entry> traditionalChineseTable() { return {kTraditionalChinese, std::size(kTraditionalChinese)}; }

} // namespace sf::i18n
