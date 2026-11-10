// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Spanish interface (#44), written by hand for SculptForge. The English text
// is the key (I18n.h); a label without an entry stays in English.
//
// Conventions, as Spanish-speaking experienced sculptors use them:
//   - Names of tools, brushes and products stay in English: Quad Remesh, Forge Spheres, Lit Sphere, Clay Block and every
//     other brush, Rgb, Project Detail, Lazy Stroke, Mask Pen, VDM. Generic words are translated: Pieza (Part),
//     Biblioteca (Library), Pintura de vértices (Vertex Paint), Grupos de caras (Face Groups).
//   - Palettes and actions read own Spanish: Pincel, Trazo, Herramienta, Dibujar, Tamaño de dibujo,
//     Intensidad, Atenuación, Añadir nivel, Enmascarado.
//   - Short and imperative, "tú" (Elige, Guarda), sentence case like the English.
//   - printf conversions (%d, %.1f, %s, %%) stay in the same order: tests check every entry.
// Keep the entries sorted by English within each group, one entry per English text.
#include "app/I18n.h"

#include <iterator>

namespace sf::i18n {

namespace {

constexpr Entry kSpanish[] = {
    // --- Palettes (the palette bar) and their sub-palettes ---
    {"Alpha", "Alpha"},
    {"Brush", "Pincel"},
    {"Color", "Color"},
    {"Draw", "Dibujar"},
    {"Edit", "Editar"},
    {"File", "Archivo"},
    {"Generate", "Generar"},
    {"Help", "Ayuda"},
    {"Material", "Material"},
    {"Preferences", "Preferencias"},
    {"Render", "Render"},
    {"Script", "Script"},
    {"Splats", "Splats"},
    {"Stroke", "Trazo"},
    {"Tool", "Herramienta"},
    {"Transform", "Transformar"},
    // The SculptForge layout's task menus and their sections (Palettes.cpp taskMenus), Preferences > Interface > Look.
    {"Mesh", "Malla"},
    {"View", "Vista"},
    {"Draw & Tablet", "Dibujo y tableta"},
    {"Symmetry & Gizmo", "Simetría y gizmo"},
    {"Vertex Paint & Color", "Pintura de vértices y color"},
    {"Parts & Geometry", "Partes y geometría"},
    {"Materials", "Materiales"},
    {"Gaussian Splats", "Splats gaussianos"},
    {"Look", "Aspecto"},
    {"Colours:", "Colores:"},
    {"Match my desktop theme", "Usar el tema de mi escritorio"},
    {"Uses your Omarchy theme's colours when Omarchy is installed", "Usa los colores de tu tema de Omarchy cuando Omarchy está instalado"},
    {"Green", "Verde"},
    {"Amber", "Ámbar"},
    {"Scanlines & glow", "Líneas de barrido y brillo"},
    {"Classic", "Clásico"},
    {"AI brush provider", "Proveedor del pincel IA"},
    {"Autosave", "Autoguardado"},
    {"Bake", "Bake"},
    {"Saving & Layouts", "Guardado y diseños"},
    {"Reshape", "Remodelar"},
    {"Display", "Visualización"},
    {"Viewport Look", "Aspecto de la vista"},
    {"Make Shell", "Crear carcasa"},
    {"Export", "Exportar"},
    {"Facial Shapes", "Formas faciales"},
    {"Geometry", "Geometría"},
    {"Hotkeys", "Atajos de teclado"},
    {"Input", "Entrada"},
    {"Interface", "Interfaz"},
    {"Kelvinlet & Capture", "Kelvinlet y captura"},
    {"Layers", "Capas"},
    {"Library", "Biblioteca"},
    {"Masking", "Enmascarado"},
    {"Paint Layers", "Capas de pintura"},
    {"Performance", "Rendimiento"},
    {"Face Groups", "Grupos de caras"},
    {"Remesh", "Remallado"},
    {"Part", "Pieza"},
    {"Parts", "Piezas"},
    {"Tablet", "Tableta"},
    {"Texture Image", "Imagen de textura"},
    // --- Garments: Tool > Garments and the Pattern window (PanelGarments.cpp) ---
    {"%.1f x %.1f cm, %.0f cm2", "%.1f x %.1f cm, %.0f cm2"},
    {"%.1f, %.1f cm", "%.1f, %.1f cm"},
    {"%.1f, %.1f cm   %s: %.1f cm (Alt+click adds a point, C curves it)", "%.1f, %.1f cm   %s: %.1f cm (Alt+clic añade un punto, C lo curva)"},
    {"%s: %.1f cm", "%s: %.1f cm"},
    {"%d panels, %d seams", "%d paneles, %d costuras"},
    {"A mirrored copy of the selected panel (the other sleeve) beside it, with no seams.", "Una copia reflejada del panel seleccionado (la otra manga) a su lado, sin costuras."},
    {"Add a 40 x 30 cm rectangle panel, ready to reshape.", "Añade un panel rectangular de 40 x 30 cm, listo para darle forma."},
    {"Add panels from an SVG: every closed shape, at the file's real size.", "Añade paneles desde un SVG: cada forma cerrada, al tamaño real del archivo."},
    {"Copy the selected panel beside it (Ctrl+D over the window).", "Copia el panel seleccionado a su lado (Ctrl+D sobre la ventana)."},
    {"Delete Panel", "Eliminar panel"},
    {"Distance between the cloth's particles: smaller makes finer folds and drapes slower.", "Distancia entre las partículas de la tela: menor da pliegues más finos y una caída más lenta."},
    {"Drag points to reshape, Alt+click an edge to add one, Delete removes it", "Arrastra los puntos para dar forma, Alt+clic en un borde añade uno, Supr lo elimina"},
    {"Dragged points land on the 5 mm grid and on other panels' points. Ctrl drags freely.", "Los puntos arrastrados se ajustan a la cuadrícula de 5 mm y a los puntos de otros paneles. Ctrl arrastra libremente."},
    {"Export DXF", "Exportar DXF"},
    {"Export SVG", "Exportar SVG"},
    {"Fit every panel in the view (F over it).", "Encuadra todos los paneles en la vista (F sobre ella)."},
    {"Flip the selected panel left to right in place.", "Voltea el panel seleccionado de izquierda a derecha en su sitio."},
    {"Garment fabric", "Tejido de la prenda"},
    {"Garments", "Prendas"},
    {"How the cloth stretches, bends and weighs.", "Cómo se estira, se dobla y cuánto pesa la tela."},
    {"Import SVG", "Importar SVG"},
    {"Mirror Copy", "Copia reflejada"},
    {"No pattern yet", "Aún no hay patrón"},
    {"No pattern yet: start from a T-shirt, add a rectangle or import an SVG", "Aún no hay patrón: empieza con una camiseta, añade un rectángulo o importa un SVG"},
    {"Open the Pattern window: the garment's panels flat at real size. Drag points and curve handles, sew edges together, import and export SVG.", "Abre la ventana Patrón: los paneles de la prenda planos a tamaño real. Arrastra puntos y tiradores de curva, cose bordes entre sí, importa y exporta SVG."},
    {"Panels", "Paneles"},
    {"Drape on the body", "Drapear sobre el cuerpo"},
    {"Place panels", "Colocar paneles"},
    {"Click the body for %s.", "Haz clic en el cuerpo para %s."},
    {"Place each panel slightly in front of the body at its widest point, from the view that faces it: a front panel on the front of the chest, the back from behind, a sleeve on the upper arm from its side.", "Coloca cada panel un poco delante del cuerpo en su punto más ancho, desde la vista que lo mira: un panel delantero en la parte delantera del pecho, la espalda desde detrás, una manga en la parte superior del brazo desde su lado."},
    {"%s on the body:", "%s sobre el cuerpo:"},
    {"%d of %d panels placed", "%d de %d paneles colocados"},
    {"Convert to Part", "Convertir en Pieza"},
    {"Click the body once for each panel: it sits a little in front of the click, facing you, its top up the screen. Place each panel slightly in front of the body at its widest point, from the view that faces it: a front panel on the front of the chest, the back from behind, a sleeve on the upper arm from its side. Cloth dropped from above settles behind overhangs like a chin or a beard.", "Haz clic en el cuerpo una vez por panel: queda un poco delante del clic, mirándote, con su parte superior hacia arriba en la pantalla. Coloca cada panel un poco delante del cuerpo en su punto más ancho, desde la vista que lo mira: un panel delantero en la parte delantera del pecho, la espalda desde detrás, una manga en la parte superior del brazo desde su lado. La tela que cae desde arriba se asienta detrás de salientes como un mentón o una barba."},
    {"Curls the panel back round the body: 0 flat, 360 a closed tube. A panel sewn to itself (a sleeve, a trouser leg) starts nearly closed, its own seam on the far side from your click.", "Curva el panel alrededor del cuerpo: 0 plano, 360 un tubo cerrado. Un panel cosido a sí mismo (una manga, una pernera) empieza casi cerrado, con su costura en el lado opuesto a tu clic."},
    {"Gap", "Separación"},
    {"Gap %.0f mm", "Separación %.0f mm"},
    {"Wrap", "Envolver"},
    {"Wrap %.0f°", "Envolver %.0f°"},
    {"How far in front of the click the panel starts. Keep it clear of the body.", "A qué distancia delante del clic empieza el panel. Mantenlo separado del cuerpo."},
    {"Keeps the draped garment as a Part to sculpt: seams welded, UVs from the flat pattern, the pattern's thickness. One undo step.", "Conserva la prenda drapeada como una Pieza para esculpir: costuras soldadas, UV del patrón plano, el grosor del patrón. Un paso de deshacer."},
    {"Sews the placed panels together and lets the garment settle on the visible Parts, live. Place every panel first.", "Cose entre sí los paneles colocados y deja que la prenda se asiente sobre las Piezas visibles, en directo. Coloca primero todos los paneles."},
    {"Turns the panel in the view's plane: a sleeve's top toward the shoulder.", "Gira el panel en el plano de la vista: la parte superior de una manga hacia el hombro."},
    {"Particles", "Partículas"},
    {"Particles %.0f mm", "Partículas %.0f mm"},
    {"Pattern", "Patrón"},
    {"Rectangle", "Rectángulo"},
    {"Redo it (Ctrl+Shift+Z over the window).", "Rehazlo (Ctrl+Mayús+Z sobre la ventana)."},
    {"Remove the selected panel and its seams.", "Elimina el panel seleccionado y sus costuras."},
    {"Save the outlines in millimetres for cutting machines.", "Guarda los contornos en milímetros para máquinas de corte."},
    {"Save the panels at real size for printing and sewing: sewing and cutting lines, notches, grain lines.", "Guarda los paneles a tamaño real para imprimir y coser: líneas de costura y de corte, piquetes, líneas de hilo."},
    {"Seams", "Costuras"},
    {"Sew", "Coser"},
    {"Sew start to start: the fabric turns over along the seam (rarely wanted).", "Cose inicio con inicio: la tela se da la vuelta a lo largo de la costura (rara vez se quiere)."},
    {"Sew two edges: click one, then the other. Their lengths show while you pick; Flip sews start to start.", "Cose dos bordes: haz clic en uno y luego en el otro. Sus longitudes se ven mientras eliges; Voltear cose inicio con inicio."},
    {"Sewing %.1f cm to %.1f cm", "Cosiendo %.1f cm con %.1f cm"},
    {"Sewing %.1f cm to...", "Cosiendo %.1f cm con..."},
    {"Simulation", "Simulación"},
    {"Snap", "Ajustar"},
    {"Start from a T-shirt pattern: front, back and two sleeves, sewn. Replaces the pattern.", "Empieza con un patrón de camiseta: delantero, espalda y dos mangas, cosidos. Reemplaza el patrón."},
    {"Templates", "Plantillas"},
    {"Start a garment from a template at your sizes: T-shirt, trousers, skirts, hoodie, dress, cloak, gloves. The size chart is saved in the project.", "Empieza una prenda desde una plantilla a tus medidas: camiseta, pantalón, faldas, sudadera con capucha, vestido, capa, guantes. La tabla de medidas se guarda en el proyecto."},
    {"Print", "Imprimir"},
    {"Print the pattern at home on A4 or Letter pages that tape together, at real size.", "Imprime el patrón en casa en hojas A4 o Carta que se unen con cinta, a tamaño real."},
    {"Save PDF...", "Guardar PDF..."},
    {"Every page in one PDF: trim lines, matching triangles, page names, the 50 mm square.", "Todas las páginas en un PDF: líneas de corte, triángulos de unión, nombres de página, el cuadrado de 50 mm."},
    {"Save SVG pages...", "Guardar páginas SVG..."},
    {"The same pages, one SVG file each (name-A1.svg, name-A2.svg, ...).", "Las mismas páginas, un archivo SVG por página (nombre-A1.svg, nombre-A2.svg, ...)."},
    {"Chest", "Pecho"},
    {"Waist", "Cintura"},
    {"Hip", "Cadera"},
    {"Neck", "Cuello"},
    {"Shoulder width", "Ancho de hombros"},
    {"Arm length", "Largo de brazo"},
    {"Upper arm", "Brazo"},
    {"Wrist", "Muñeca"},
    {"Back length", "Talle espalda"},
    {"Inseam", "Entrepierna"},
    {"Rise", "Tiro"},
    {"Waist to knee", "Cintura a rodilla"},
    {"Head", "Cabeza"},
    {"Hand girth", "Contorno de mano"},
    {"Hand length", "Largo de mano"},
    {"Round the fullest part of the chest", "Alrededor de la parte más ancha del pecho"},
    {"Round the natural waist", "Alrededor de la cintura natural"},
    {"Round the fullest part of the hips", "Alrededor de la parte más ancha de la cadera"},
    {"Round the base of the neck", "Alrededor de la base del cuello"},
    {"Shoulder point to shoulder point, across the back", "De punta de hombro a punta de hombro, por la espalda"},
    {"Shoulder point to wrist, arm hanging", "De la punta del hombro a la muñeca, con el brazo caído"},
    {"Round the fullest part of the upper arm", "Alrededor de la parte más ancha del brazo"},
    {"Round the wrist", "Alrededor de la muñeca"},
    {"The bone at the base of the neck down to the waist", "Del hueso de la base del cuello hasta la cintura"},
    {"Crotch to the floor, or to where the trousers should end", "De la entrepierna al suelo, o hasta donde termina el pantalón"},
    {"Sitting on a hard chair: waist down to the seat, at the side", "Sentado en una silla dura: de la cintura al asiento, por el costado"},
    {"Waist to knee, down the side", "De la cintura a la rodilla, por el costado"},
    {"Round the forehead and the back of the head", "Alrededor de la frente y la parte de atrás de la cabeza"},
    {"Round the knuckles, thumb out", "Alrededor de los nudillos, sin el pulgar"},
    {"Wrist crease to the tip of the middle finger", "Del pliegue de la muñeca a la punta del dedo medio"},
    {"New garment at your sizes", "Prenda nueva a tus medidas"},
    {"Trousers", "Pantalón"},
    {"Straight skirt", "Falda recta"},
    {"A-line skirt", "Falda en A"},
    {"Hoodie", "Sudadera con capucha"},
    {"Dress", "Vestido"},
    {"Cloak", "Capa"},
    {"Gloves", "Guantes"},
    {"Add beside the pattern", "Añadir junto al patrón"},
    {"Keep the pattern and add the new garment next to it (an outfit)", "Conserva el patrón y añade la prenda nueva al lado (un conjunto)"},
    {"Size chart (mm)", "Tabla de medidas (mm)"},
    {"Default sizes", "Medidas por defecto"},
    {"Print at home, at real size", "Imprimir en casa, a tamaño real"},
    {"A4", "A4"},
    {"Letter", "Carta"},
    {"Landscape", "Horizontal"},
    {"%d pages", "%d páginas"},
    {"Print at 100% (no fit to page), then measure the 50 mm square on page A1.", "Imprime al 100% (sin ajustar a la página) y mide el cuadrado de 50 mm de la página A1."},
    {"T-Shirt", "Camiseta"},
    {"The panel's name.", "El nombre del panel."},
    {"Thickness of the Part the garment becomes (0: a single surface).", "Grosor de la pieza en que se convierte la prenda (0: una sola superficie)."},
    {"Undo the last pattern edit (Ctrl+Z over the window).", "Deshace la última edición del patrón (Ctrl+Z sobre la ventana)."},
    {"Unsew", "Descoser"},
    {"Updates", "Actualizaciones"},
    {"Vector Displacement", "Desplazamiento vectorial"},
    {"Visibility", "Visibilidad"},
    {"Forge Spheres", "Forge Spheres"},
    {"Edit recipe", "Editar receta"},

    // --- Palette paths in the Ctrl-help box ("Tool > Geometry") ---
    {"Bottom shelf", "Estante inferior"},
    {"Left shelf", "Estante izquierdo"},
    {"Right shelf", "Estante derecho"},
    {"Top shelf", "Estante superior"},
    {"Title", "Barra de título"},
    {"Layout", "Diseño"},
    {"Dynamic topology", "Topología dinámica"},
    {"Camera", "Cámara"},
    {"Import / Export", "Importar / Exportar"},
    {"From an image", "A partir de una imagen"},
    {"From Image", "Desde imagen"},
    {"Voxel Remesh", "Remallado vóxel"},
    {"Quick Remesh", "Remallado rápido"},
    {"Game Ready", "Listo para juegos"},
    {"Navigation", "Navegación"},

    // --- Title bar, status line, search ---
    {"Untitled", "Sin título"},
    {"Project", "Proyecto"},
    {"Search", "Buscar"},
    {"Minimize", "Minimizar"},
    {"Menus", "Menús"},
    {"More menus", "Más menús"},
    {"The menus that don't fit in the window's width.", "Los menús que no caben en el ancho de la ventana."},
    {"Snapshots", "Instantáneas"},
    {"Save Snapshot", "Guardar instantánea"},
    {"Base", "Base"},
    {"   Splats ", "   Splats "},
    {"Verts %s   Tris %s   Part %s (%d/%d)   Layer %s%s%s", "Vért. %s   Tris %s   Pieza %s (%d/%d)   Capa %s%s%s"},
    {"Customize: Ctrl+Alt+drag to move or remove items", "Personalizar: Ctrl+Alt+arrastrar para mover o quitar elementos"},
    {"Customize off", "Personalizar desactivado"},
    {"Standard layout and shortcuts restored - Save Layout keeps the layout",
     "Diseño y atajos estándar restaurados: Guardar diseño conserva el diseño"},
    {"Saved layout restored", "Diseño guardado restaurado"},
    {"No saved layout yet - Save Layout first", "Aún no hay un diseño guardado: usa Guardar diseño primero"},
    {"Layout saved", "Diseño guardado"},
    {"Preferences saved (they also save themselves)", "Preferencias guardadas (también se guardan solas)"},
    {"Saved", "Guardado"},
    {"Background: Flat Colour", "Fondo: color plano"},
    {"Background: Gradient", "Fondo: degradado"},

    // --- Command palette ---
    {"Search commands, brushes and settings", "Buscar comandos, pinceles y ajustes"},
    {"Recently used", "Usados recientemente"},
    {"  No matching commands", "  Ningún comando coincide"},
    {" result", " resultado"},
    {" results", " resultados"},
    {"select", "elegir"},
    {"run", "ejecutar"},
    {"edit in console", "a la consola"},
    {"close", "cerrar"},

    // --- Ctrl-help box ---
    {"No UVs yet: pick a method and Unwrap (or mark seams first)", "Aún no hay UV: elige un método y Desenvolver (o marca costuras primero)"},
    {"No mesh", "Sin malla"},
    {"No description yet.", "Aún sin descripción."},
    {"Not available right now.", "No disponible ahora mismo."},
    {"Learn more: %s", "Más información: %s"},
    {"Back to the palette bar", "Volver a la barra de paletas"},
    {"Move to the tray", "Mover a la bandeja"},
    {"Take this palette out of the tray.", "Saca esta paleta de la bandeja."},
    {"Keep this palette in the right tray (Shift+click: left tray), or drag the icon onto a tray.",
     "Deja esta paleta en la bandeja derecha (Mayús+clic: la izquierda), o arrastra el icono a una bandeja."},
    {"Click to open or close. Opening one closes the others (Shift+click keeps them open; Preferences > Interface).",
     "Clic para abrir o cerrar. Al abrir una se cierran las demás (Mayús+clic las mantiene abiertas; Preferencias > "
     "Interfaz)."},
    {"Click to save the project. * means it has unsaved changes.",
     "Clic para guardar el proyecto. * indica que tiene cambios sin guardar."},
    {"Find any command, brush or material by name and run it.",
     "Busca cualquier comando, pincel o material por su nombre y ejecútalo."},
    {"Minimize the SculptForge window.", "Minimiza la ventana de SculptForge."},
    {"Left tray", "Bandeja izquierda"},
    {"Right tray", "Bandeja derecha"},
    {"Shortcut for", "Atajo para"},

    // --- Shelves ---
    {"Add a brush", "Añadir pincel"},
    {"Put another brush (built-in or one of yours) on this shelf.",
     "Pon otro pincel (de serie o uno tuyo) en este estante."},
    {"One of your custom brushes.", "Uno de tus pinceles personalizados."},
    {"Remove from shelf", "Quitar del estante"},
    {"Tile size", "Tamaño de las fichas"},
    {"Small", "Pequeño"},
    {"Medium", "Mediano"},
    {"Large", "Grande"},
    {"The active brush. Click for the brush picker; the bottom shelf holds favourites.",
     "El pincel activo. Clic para elegir pincel; el estante inferior guarda los favoritos."},
    {"How dabs follow the cursor: Dots, Drag Stamp, Slide Stamp, Spray or Lazy Stroke. Click to choose.",
     "Cómo siguen los toques al cursor: Dots, Drag Stamp, Slide Stamp, Spray o Lazy Stroke. Clic para elegir."},
    {"The VDM brush's vector displacement map. Click to pick another.",
     "El mapa de desplazamiento vectorial del pincel VDM. Clic para elegir otro."},
    {"The brush's stamp shape (white = full effect). Click for the alpha picker.",
     "La forma del sello del pincel (blanco = efecto completo). Clic para elegir alpha."},
    {"How the model is shaded in the view. Click for the material picker.",
     "Cómo se sombrea el modelo en la vista. Clic para elegir material."},
    {"Brush radius in screen pixels. [ and ] change it by 10%; Ctrl+click to type.",
     "Radio del pincel en píxeles de pantalla. [ y ] lo cambian un 10%; Ctrl+clic para escribirlo."},
    {"Brush radius in screen pixels. [ and ] change it by 10%.",
     "Radio del pincel en píxeles de pantalla. [ y ] lo cambian un 10%."},
    {"ActiveTris / TotalTris", "Tris activos / Tris totales"},
    {"Move", "Mover"},
    {"Zoom", "Zoom"},
    {"Rotate", "Rotar"},
    {"Scale", "Escalar"},

    // --- Draw and the top shelf ---
    {"Size", "Tamaño de dibujo"},
    {"Intensity", "Intensidad"},
    {"Smooth Intensity", "Intensidad de Smooth"},
    {"Colour intensity", "Intensidad RGB"},
    {"Focal Length", "Distancia focal"},
    {"Detail", "Detalle"},
    {"Sculpt", "Esculpir"},
    {"Paint", "Pintar"},
    {"Dynamic", "Dinámica"},
    {"Constant detail", "Detalle constante"},
    {"Axes", "Ejes"},
    {"No pen detected", "No se detecta lápiz"},
    {"Pen pressure", "Presión del lápiz"},
    {"Pressure > Size", "Presión > Tamaño"},
    {"Pressure > Strength", "Presión > Fuerza"},
    {"Front faces only", "Solo caras frontales"},
    {"Ctrl+Shift+drag a Cut shape", "Ctrl+Mayús+arrastrar una forma de Corte"},
    {"Depth %.2f", "Profundidad %.2f"},
    {"Copy the active Part (%s)", "Copiar la Parte activa (%s)"},
    {"Lasso Mask and Rectangle Mask mask only what faces you: the far side and folds turned away stay as they are.",
     "Lasso Mask y Rectangle Mask enmascaran solo lo que mira hacia ti: el lado de atrás y los pliegues que no te miran no cambian."},
    {"How deep Cut In goes, or how far Cut Add stands out, as a share of the Part's size.",
     "Cuánto entra Cut In, o cuánto sobresale Cut Add, como fracción del tamaño de la Parte."},
    {"Invert (Alt)", "Invertir (Alt)"},
    {"Invert", "Invertir"},
    {"Smooth keeps volume", "Smooth conserva el volumen"},
    {"Smooth face group borders only", "Smooth solo en bordes de grupos"},
    {"Symmetry X", "Simetría X"},
    {"Local", "Local"},
    {"X axis", "Eje X"},
    {"Y axis", "Eje Y"},
    {"Z axis", "Eje Z"},
    {"Radial Copies %d", "Copias radiales %d"},
    {"Timeline position", "Posición de la línea de tiempo"},
    {"Top of viewport", "Arriba del visor"},
    {"How strongly painting colours the surface (1-100); Ctrl+click to type.",
     "Con qué fuerza colorea la pintura la superficie (1-100); Ctrl+clic para escribirlo."},
    {"This Part has subdivision levels, so strokes don't remesh it. Freeze Levels keeps the level shown as one plain "
     "mesh (Ctrl+Z brings the levels back).",
     "Esta pieza tiene niveles de subdivisión, así que los trazos no la remallan. Congelar niveles conserva el nivel "
     "visible como una sola malla (Ctrl+Z recupera los niveles)."},

    // --- Edit, File, Help ---
    {"Undo", "Deshacer"},
    {"Redo", "Rehacer"},
    {"Paste brush", "Pegar pincel"},
    {"New Sphere", "Nueva esfera"},
    {"New Plane", "Nuevo plano"},
    {"New Forge Sphere", "Nueva Forge Sphere"},
    {"Open", "Abrir"},
    {"Open Recent", "Abrir reciente"},
    {"Save", "Guardar"},
    {"Save As", "Guardar como"},
    {"Import", "Importar"},
    {"Export OBJ", "Exportar OBJ"},
    {"Export STL", "Exportar STL"},
    {"Export GLB", "Exportar GLB"},
    {"Export LOD Chain", "Exportar cadena de LOD"},
    {"Export USD / USDZ", "Exportar USD / USDZ"},
    {"Export FBX for Unreal", "Exportar FBX para Unreal"},
    {"Export EXR", "Exportar EXR"},
    {"Export Maps", "Exportar mapas"},
    {"Export Texture", "Exportar textura"},
    {"Export Displacement", "Exportar desplazamiento"},
    {"Export timelapse", "Exportar timelapse"},
    {"Send to Unreal", "Enviar a Unreal"},
    {"Send to Blender", "Enviar a Blender"},
    {"Receive from Blender", "Recibir de Blender"},
    {"Save Splats", "Guardar splats"},
    {"Capture from phone", "Capturar con el móvil"},
    {"Quit", "Salir"},
    {"Controls", "Controles"},
    {"User Guide", "Guía del usuario"},
    {"What's New", "Novedades"},
    {"Check for updates", "Buscar actualizaciones"},
    {"Command palette", "Paleta de comandos"},
    {"Website & documentation", "Web y documentación"},
    {"Join the Discord", "Únete al Discord"},
    {"Join the community on Discord", "Únete a la comunidad en Discord"},
    {"Report a problem", "Informar de un problema"},
    {"Open log folder", "Abrir la carpeta de registros"},
    {"Record a performance trace", "Grabar una traza de rendimiento"},
    {"Fixes & suggestions", "Correcciones y sugerencias"},
    {"Benchmark (proof mode)", "Benchmark (modo prueba)"},
    {"Support SculptForge", "Apoya a SculptForge"},
    {"About SculptForge", "Acerca de SculptForge"},
    {"Welcome", "Bienvenida"},

    // --- Generate ---
    {"Generate 3D", "Generar 3D"},
    {"Choose image", "Elegir imagen"},
    {"Change image", "Cambiar imagen"},
    {"Relief", "Relieve"},
    {"Inflate", "Inflar"},
    {"Stamp", "Estampar"},
    {"AI Brush", "Pincel IA"},
    {"Image or text to a sculptable 3D model: Hugging Face (free), fal.ai, Meshy, your ComfyUI server, your own "
     "command, or an offline blockout.",
     "De una imagen o un texto a un modelo 3D esculpible: Hugging Face (gratis), fal.ai, Meshy, tu servidor ComfyUI, "
     "tu propio comando o un bloqueo sin conexión."},

    // --- Render, Display, Material ---
    {"Wireframe", "Malla de alambre"},
    {"Show Mesh", "Mostrar malla"},
    {"Show Vertex Paint", "Mostrar pintura de vértices"},
    {"Show Mask", "Mostrar máscara"},
    {"Show Face Groups", "Mostrar grupos de caras"},
    {"Show Splats", "Mostrar splats"},
    {"Show mesh", "Mostrar malla"},
    {"Show mask", "Mostrar máscara"},
    {"Show face groups", "Mostrar grupos de caras"},
    {"Show vertex paint", "Mostrar pintura de vértices"},
    {"Colour tints the material", "El color tiñe el material"},
    {"Show texture", "Mostrar textura"},
    {"Show UVs", "Mostrar UVs"},
    {"Mask", "Máscara"},
    {"Groups", "Grupos"},
    {"Material colour", "Color del material"},
    {"Colour of the Studio material (the clay colour).", "Color del material Studio (el color de la arcilla)."},
    {"Frame model", "Encuadrar modelo"},
    {"Frame Part", "Encuadrar pieza"},
    {"Reset view", "Restablecer vista"},
    {"Wheel zooms toward the pointer", "La rueda acerca hacia el puntero"},
    {"Fullscreen", "Pantalla completa"},
    {"Gradient", "Degradado"},
    {"Flat Colour", "Color plano"},
    {"Background colour", "Color de fondo"},
    {"Flat", "Plano"},
    {"Load Lit Sphere", "Cargar Lit Sphere"},
    {"Material picker", "Selector de materiales"},
    {"Material swatches", "Muestras de material"},

    // --- Script ---
    {"Console", "Consola"},
    {"Run script file", "Ejecutar archivo de script"},
    {"Copy session as script", "Copiar la sesión como script"},
    {"Clear console", "Limpiar la consola"},
    {"Close the console", "Cerrar la consola"},
    {"Every action is a script command: run them with --script \"subdivide; remesh resolution=200\" or the "
     "sculptforge-cli tool.",
     "Cada acción es un comando de script: ejecútalas con --script \"subdivide; remesh resolution=200\" o con la "
     "herramienta sculptforge-cli."},

    // --- Stroke ---
    {"Continuous", "Continuo"},
    {"Stroke picker", "Selector de trazos"},
    {"Lazy radius", "Radio Lazy"},

    // --- Tool: header, Parts ---
    {"tris", "tris"},
    {"Append", "Añadir"},
    {"Append as Part", "Añadir como pieza"},
    {"Add a new Part beside the model", "Añade una pieza nueva junto al modelo"},
    {"Insert on the model", "Insertar en el modelo"},
    {"Duplicate", "Duplicar"},
    {"Rename", "Renombrar"},
    {"Delete", "Eliminar"},
    {"Move up", "Subir"},
    {"Move down", "Bajar"},
    {"Move up the list", "Sube en la lista"},
    {"Move down the list", "Baja en la lista"},
    {"Merge down", "Combinar abajo"},
    {"Merge the active Part with the one below it", "Combina la pieza activa con la que tiene debajo"},
    {"Merge visible", "Combinar visibles"},
    {"Pose all Parts", "Posar todas las piezas"},
    {"Apply Pose", "Aplicar pose"},
    {"Cancel Pose", "Cancelar pose"},
    {"Give every Part the Pose Mesh's moves at full resolution, as one undo step", "Da a cada pieza los movimientos de la malla de pose a resolución completa, en un solo paso de deshacer"},
    {"Give every Part the Pose Mesh's moves at full resolution, as one undo step, and remove the Pose Mesh.", "Da a cada pieza los movimientos de la malla de pose a resolución completa, en un solo paso de deshacer, y quita la malla de pose."},
    {"Merge the visible Parts into one light Pose Mesh and pose it with the Gizmo and masks; Apply Pose gives every Part the pose at full resolution.", "Une las piezas visibles en una malla de pose ligera y pósala con el Gizmo y las máscaras; Aplicar pose da la pose a cada pieza a resolución completa."},
    {"Pose the visible Parts together on one light Pose Mesh with the Gizmo and masks, then Apply Pose", "Posa juntas las piezas visibles en una malla de pose ligera con el Gizmo y las máscaras, y luego Aplicar pose"},
    {"Remove the Pose Mesh and show the Parts again; nothing changes.", "Quita la malla de pose y vuelve a mostrar las piezas; no cambia nada."},
    {"Split parts", "Separar partes"},
    {"Split groups", "Separar grupos"},
    {"One Part per disconnected piece", "Una pieza por cada pieza separada"},
    {"One Part per face group", "Una pieza por cada grupo de caras"},
    {"Alt+click a Part to select it", "Alt+clic en una pieza para seleccionarla"},
    {"All Parts", "Todas las piezas"},
    {"Solo", "Solo"},
    {"Sphere", "Esfera"},
    {"Cube", "Cubo"},
    {"Plane", "Plano"},
    {"From file", "Desde archivo"},
    {"Active Part's copy", "Copia de la pieza activa"},
    {"Insert set from file...", "Conjunto para insertar desde archivo..."},
    {"Insert set from folder...", "Conjunto para insertar desde carpeta..."},
    {"Creature bones", "Huesos de criatura"},
    {"Skull", "Cráneo"},
    {"Jaw", "Mandíbula"},
    {"Fang", "Colmillo"},
    {"Blade tooth", "Diente cortante"},
    {"Swept horn", "Cuerno curvo"},
    {"Spiral horn", "Cuerno en espiral"},
    {"Claw", "Garra"},
    {"Vertebra", "Vértebra"},
    {"Tail vertebrae", "Vértebras de la cola"},
    {"Rib", "Costilla"},
    {"Limb bone", "Hueso de extremidad"},
    {"Wing bone", "Hueso del ala"},
    {"Wing finger", "Dedo del ala"},
    {"Original creature bones to build on: skull, jaw, teeth, horns, spine, ribs, limb and wing bones, claws and tail", "Huesos de criatura originales para empezar: cráneo, mandíbula, dientes, cuernos, columna, costillas, huesos de extremidades y alas, garras y cola"},
    {"A model file with several objects: insert them one at a time", "Un archivo de modelo con varios objetos: insértalos de uno en uno"},
    {"A folder of .obj, .ply or .glb files: insert them one at a time", "Una carpeta de archivos .obj, .ply o .glb: insértalos de uno en uno"},
    {"Insert set", "Conjunto para insertar"},
    {"[ / ] previous / next   Esc stops", "[ / ] anterior / siguiente   Esc termina"},
    {"Not enough GPU memory to show every Part: hide some or step down a subdivision level.",
     "No hay memoria de GPU suficiente para mostrar todas las piezas: oculta alguna o baja un nivel de "
     "subdivisión."},
    {"%s\n%zu vertices, %zu triangles%s%s%s\nClick to select, double-click to rename",
     "%s\n%zu vértices, %zu triángulos%s%s%s\nClic para seleccionar, doble clic para renombrar"},
    {"\nLinked: %d Parts share this mesh", "\nEnlazada: %d piezas comparten esta malla"},
    {"Linked Duplicate", "Duplicado enlazado"},
    {"Make Unique", "Hacer única"},
    {"A copy that shares the active Part's mesh, beside it: sculpt, paint or mask any of them and all change. Move each with the Gizmo; Make Unique gives one its own mesh again. Hundreds of copies cost about one mesh.",
     "Una copia que comparte la malla de la pieza activa, a su lado: esculpe, pinta o enmascara cualquiera y cambian todas. Mueve cada una con el gizmo; Hacer única le devuelve a una su propia malla. Cientos de copias cuestan más o menos una malla."},
    {"Give the active linked Part its own copy of the mesh: it stays where it is and no longer changes with the others.",
     "Da a la pieza enlazada activa su propia copia de la malla: se queda donde está y ya no cambia con las demás."},
    {"Extract", "Extraer"},
    {"Accept", "Aceptar"},
    {"Double", "Doble"},
    {"Thickness %.3f", "Grosor %.3f"},
    {"Both Sides", "Ambos lados"},
    {"Project Detail", "Project Detail"},
    {"Dist", "Dist"},

    // --- Geometry ---
    {"Subdivide", "Subdividir"},
    {"Subdivide (removes levels)", "Subdividir (quita los niveles)"},
    {"Symmetry", "Simetría"},
    {"Levels", "Niveles"},
    {"Cleanup", "Limpieza"},
    {"Resolution (no levels)", "Resolución (sin niveles)"},
    {"No subdivision levels - Add Level adds them", "Sin niveles de subdivisión: Añadir nivel los añade"},
    {"Remove levels %d to %d and the detail sculpted on them?", "¿Quitar los niveles %d a %d y el detalle esculpido en ellos?"},
    {"Remove levels 1 to %d? Level %d becomes the base mesh.", "¿Quitar los niveles 1 a %d? El nivel %d pasa a ser la malla base."},
    {"Add Level", "Añadir nivel"},
    {"Micro Detail", "Detalle micro"},
    {"Subdivide and Remove Levels", "Subdividir y quitar los niveles"},
    {"Freeze Levels and Proceed", "Congelar los niveles y continuar"},
    {"This drops %d subdivision level. %s keeps the level shown as one plain mesh.", "Esto elimina %d nivel de subdivisión. %s conserva el nivel mostrado como una malla simple."},
    {"This drops %d subdivision levels. %s keeps the level shown as one plain mesh.", "Esto elimina %d niveles de subdivisión. %s conserva el nivel mostrado como una malla simple."},
    {"Undo brings the levels back.", "Deshacer recupera los niveles."},
    {"Lower Level", "Nivel inferior"},
    {"Higher Level", "Nivel superior"},
    {"Remove Below", "Quitar inferiores"},
    {"Remove Above", "Quitar superiores"},
    {"Rebuild Lower Levels", "Reconstruir niveles inferiores"},
    {"Add Micro", "Añadir micro"},
    {"Delete Micro", "Eliminar micro"},
    {"Bake Micro", "Hacer bake de micro"},
    {"Micro Surface Noise", "Ruido de superficie micro"},
    {"Streamed: full detail loads where you look and sculpt", "Transmitido: el detalle completo se carga donde miras y esculpes"},
    {"Focus Detail keeps this micro level in a .sfdetail file beside the project and loads full detail only where you look "
     "and sculpt. Keep the file with the project.",
     "Focus Detail guarda este nivel micro en un archivo .sfdetail junto al proyecto y carga el detalle completo solo donde miras y esculpes. Guarda el archivo con el proyecto."},
    {"Micro", "Micro"},
    {"Level", "Nivel"},
    {"Decimate", "Diezmar"},
    {"Keep", "Conservar"},
    {"Keep Masked", "Conservar máscara"},
    {"Follow Face Groups", "Seguir grupos de caras"},
    {"Keep Original", "Conservar original"},
    {"Keep Texture", "Conservar textura"},
    {"Reduce the triangle count while keeping the shape. Sculpt layers are kept.",
     "Reduce el número de triángulos conservando la forma. Las capas de esculpido se conservan."},
    {"Fraction of the triangles Decimate keeps.", "Fracción de los triángulos que conserva Diezmar."},
    {"Mirror", "Reflejar"},
    {"Make Symmetric", "Hacer simétrico"},
    {"Close Gaps", "Cerrar huecos"},
    {"Fill holes", "Rellenar agujeros"},
    {"Remove Hidden", "Quitar ocultos"},

    // --- Quad Remesh and Remesh ---
    {"Quad Remesh", "Quad Remesh"},
    {"Quad Remesh Guides", "Guías de Quad Remesh"},
    {"Target Quads", "Quads objetivo"},
    {"Size Variation", "Variación de tamaño"},
    {"Guide Strength", "Fuerza de las guías"},
    {"Guides", "Guías"},
    {"Use Vertex Paint", "Usar pintura"},
    {"Keep Hard Edges", "Conservar bordes duros"},
    {"Quality Report", "Informe de calidad"},
    {"no curves", "sin curvas"},
    {"%zu curves (%zu loops)", "%zu curvas (%zu bucles)"},
    {"%zu polygons", "%zu polígonos"},
    {"%s: %zu quads in %.1f s", "%s: %zu quads en %.1f s"},
    {"%zu piece(s) used the legacy method", "%zu pieza(s) usaron el método anterior"},
    {"Corners off square: %.1f deg mean, %.0f deg worst 1%%",
     "Esquinas fuera de escuadra: %.1f° de media, %.0f° en el peor 1%%"},
    {"Poles: %zu  (3 edges %zu, 5 edges %zu, other %zu)", "Polos: %zu  (3 aristas %zu, 5 aristas %zu, otros %zu)"},
    {"Distance to the sculpt: %.3f%% mean, %.2f%% max", "Distancia a la escultura: %.3f%% de media, %.2f%% máx."},
    {"Resolution", "Resolución"},
    {"Remeshing", "Remallando"},
    {"Remesh after each stroke", "Remallar tras cada trazo"},
    {"Cancel", "Cancelar"},
    {"Stop the remesh; the Part stays as it is.", "Detén el remallado; la pieza se queda como está."},
    {"Remesh for sculpting", "Remallar para esculpir"},

    // --- Masking ---
    {"Clear", "Borrar"},
    {"All", "Todo"},
    {"Blur", "Difuminar"},
    {"Sharpen", "Enfocar"},
    {"Grow", "Expandir"},
    {"Shrink", "Contraer"},
    {"By cavity", "Por cavidad"},
    {"By face group", "Por grupo de caras"},
    {"By peaks", "Por crestas"},
    {"By colour", "Por color"},
    {"By facing", "Por orientación"},
    {"By border", "Por borde"},
    {"Shapes", "Formas"},
    {"Mask by", "Enmascarar por"},
    {"Adjust", "Ajustar"},
    {"Mask by options", "Opciones de Enmascarar por"},
    {"Face group %d", "Grupo de caras %d"},
    {"Passes", "Pasadas"},
    {"Mask Strength", "Fuerza de la máscara"},
    {"Threshold", "Umbral"},
    {"Clear Mask", "Borrar máscara"},
    {"Invert Mask", "Invertir máscara"},
    {"Mask All", "Enmascarar todo"},
    {"Mask brush", "Pincel de máscara"},
    {"Unmask everything (Mask Pen: Ctrl + drag background)", "Quita toda la máscara (Mask Pen: Ctrl + arrastrar el fondo)"},
    {"Ctrl + click background", "Ctrl + clic en el fondo"},
    {"Mask creases and folds (replaces the mask; uses the mask strength)",
     "Enmascara pliegues y arrugas (sustituye la máscara; usa la fuerza de la máscara)"},
    {"Mask one face group (replaces the mask)", "Enmascara un grupo de caras (sustituye la máscara)"},

    // --- Face Groups and Visibility ---
    {"By normals", "Por normales"},
    {"By pieces", "Por piezas"},
    {"From mask", "Desde la máscara"},
    {"Clear groups", "Borrar grupos"},
    {"Angle", "Ángulo"},
    {"Group from Mask", "Agrupar enmascarado"},
    {"Group by UVs", "Agrupar por UVs"},
    {"Masked triangles become a new group", "Los triángulos enmascarados forman un grupo nuevo"},
    {"New group wherever the surface turns more than the angle",
     "Grupo nuevo donde la superficie gira más que el ángulo"},
    {"One group per disconnected piece", "Un grupo por cada pieza separada"},
    {"Visible face groups", "Grupos de caras visibles"},
    {"Hide face group under cursor", "Ocultar el grupo de caras bajo el cursor"},
    {"Isolate face group under cursor", "Aislar el grupo de caras bajo el cursor"},
    {"Show all", "Mostrar todo"},
    {"Unhide all", "Mostrar todo"},
    {"Invert visible", "Invertir visibles"},
    {"Hide", "Ocultar"},
    {"Show", "Mostrar"},
    {"Ctrl+Shift+click a group to isolate it, +Alt to hide it, on the background to show all.\n"
     "Ctrl+Shift+drag hides outside the rectangle or lasso (Alt: inside).",
     "Ctrl+Mayús+clic en un grupo para aislarlo, +Alt para ocultarlo, en el fondo para mostrar todo.\n"
     "Ctrl+Mayús+arrastrar oculta lo que queda fuera del rectángulo o el lazo (Alt: dentro)."},

    // --- Layers, paint, texture ---
    {"New paint layer", "Nueva capa de pintura"},
    {"Delete layer", "Eliminar capa"},
    {"Opacity", "Opacidad"},
    {"Blend mode", "Modo de fusión"},
    {"Texture", "Textura"},
    {"New Texture", "Nueva textura"},
    {"Import Texture", "Importar textura"},
    {"Delete Texture", "Eliminar textura"},
    {"Vertex Paint > Texture", "Pintura de vértices > Textura"},
    {"Texture > Vertex Paint", "Textura > Pintura de vértices"},
    {"Texture size", "Tamaño de textura"},
    {"Auto UV", "UV automáticas"},
    {"Pick colour", "Tomar color"},
    {"Switch Color", "Intercambiar color"},
    {"Main colour", "Color principal"},
    {"Second colour", "Color secundario"},

    // --- Transform and the gizmo ---
    {"Gizmo", "Gizmo"},
    {"Front view", "Vista frontal"},
    {"Back view", "Vista trasera"},
    {"Left view", "Vista izquierda"},
    {"Right view", "Vista derecha"},
    {"Top view", "Vista superior"},
    {"Bottom view", "Vista inferior"},
    {"Opposite side", "Lado opuesto"},
    {"Orbit left", "Orbitar a la izquierda"},
    {"Orbit right", "Orbitar a la derecha"},
    {"Orbit up", "Orbitar hacia arriba"},
    {"Orbit down", "Orbitar hacia abajo"},
    {"Zoom in", "Acercar"},
    {"Zoom out", "Alejar"},
    {"Orthographic", "Ortográfica"},
    {"Larger brush", "Pincel más grande"},
    {"Smaller brush", "Pincel más pequeño"},
    {"Pivot", "Pivote"},

    // --- Preferences > Saving & Layouts ---
    {"Customize Shelves", "Personalizar estantes"},
    {"Save Layout", "Guardar diseño"},
    {"Save Preferences Now", "Guardar preferencias ahora"},
    {"Revert to Saved Layout", "Volver al diseño guardado"},
    {"Reset Layout and Shortcuts", "Restablecer diseño y atajos"},
    {"Import Interface", "Importar interfaz"},
    {"Export Interface", "Exportar interfaz"},
    {"Preferences and shortcuts save themselves. Layout changes last until you quit; Save Layout keeps them.",
     "Las preferencias y los atajos se guardan solos. Los cambios del diseño duran hasta que salgas; Guardar diseño "
     "los conserva."},

    // --- Preferences > Interface ---
    {"Language", "Idioma"},
    {"Language:", "Idioma:"},
    {"Change", "Cambiar"},
    {"SculptForge is in %s.", "SculptForge está en %s."},
    {"Units", "Unidades"},
    {"OBJ/PLY in", "OBJ/PLY en"},
    {"Part size", "Tamaño de la parte"},
    {"Export size", "Tamaño al exportar"},
    {"The unit sizes show in: the brush size, the Part size in Tool and the export size in File. SculptForge works in metres; this only changes how sizes read.", "La unidad en que se muestran los tamaños: el tamaño del pincel, el tamaño de la parte en Tool y el tamaño al exportar en File. SculptForge trabaja en metros; esto solo cambia cómo se leen los tamaños."},
    {"OBJ and PLY files have no units. Their numbers are read in this unit (mm for most 3D printing files, m for Blender and game tools), and exporting OBJ or PLY writes them back in the unit they were opened with, so a part keeps its size. STL is always millimetres.", "Los archivos OBJ y PLY no tienen unidades. Sus números se leen en esta unidad (mm para la mayoría de archivos de impresión 3D, m para Blender y herramientas de juegos), y al exportar OBJ o PLY se escriben en la unidad con la que se abrieron, así la parte conserva su tamaño. STL siempre está en milímetros."},
    {"The active Part's width x height x depth (X, Y, Z). The unit is set in Preferences > Interface > Units; STL exports write it in millimetres.", "Ancho x alto x profundidad de la parte activa (X, Y, Z). La unidad se elige en Preferences > Interface > Units; las exportaciones STL la escriben en milímetros."},
    {"The language of menus, palettes, shelves and messages. Brush and tool names (Quad Remesh, Forge Spheres, "
     "Clay Block...) stay as they are.",
     "El idioma de los menús, las paletas, los estantes y los mensajes. Los nombres de pinceles y herramientas "
     "(Quad Remesh, Forge Spheres, Clay Block...) se quedan como están."},
    {"Ctrl+hover help", "Ayuda con Ctrl+pasar el ratón"},
    {"Help box opacity", "Opacidad del cuadro de ayuda"},
    {"Tooltip delay", "Retardo de las descripciones"},
    {"One sub-palette open", "Una subpaleta abierta"},
    {"Reduce motion", "Reducir movimiento"},
    {"Auto collapse left tray", "Plegar sola la bandeja izquierda"},
    {"Auto collapse right tray", "Plegar sola la bandeja derecha"},
    {"Palettes strictly A-Z", "Paletas estrictamente de la A a la Z"},
    {"Left-handed layout", "Diseño para zurdos"},
    {"Shelf tiles", "Fichas del estante"},
    {"UI scale", "Escala de la interfaz"},
    {"Automatic", "Automática"},
    {"Rotation", "Rotación"},
    {"Free", "Libre"},
    {"Orbit lock",
     "Bloqueo de órbita"},
    {"Y (turntable)",
     "Y (plataforma giratoria)"},
    {"Rotation drags turn the view about the screen's axes and can roll it over the top (the default). "
     "Saved with the preferences.",
     "Arrastrar para girar mueve la vista sobre los ejes de la pantalla y puede darle la vuelta por "
     "arriba (lo predeterminado). Se guarda con las preferencias."},
    {"Rotation drags turn the view like a turntable: the model stays upright and turns about its vertical "
     "(Y) axis. Saved with the preferences.",
     "Arrastrar para girar mueve la vista como una plataforma giratoria: el modelo se queda derecho y "
     "gira sobre su eje vertical (Y). Se guarda con las preferencias."},
    {"Rotation drags turn the view about the world X axis only (red): from the front, an up or down drag "
     "tips the model towards you or away. Shift still snaps to a side view; the axis gizmo shows the "
     "lock. Saved with the preferences.",
     "Arrastrar para girar mueve la vista solo sobre el eje X del mundo (rojo): desde el frente, "
     "arrastrar arriba o abajo inclina el modelo hacia ti o hacia atrás. Mayús sigue ajustando a una "
     "vista lateral; el gizmo de ejes muestra el bloqueo. Se guarda con las preferencias."},
    {"Rotation drags turn the view about the world Z axis only (blue): from the front, a drag around the "
     "middle turns the model like a wheel. Shift still snaps to a side view; the axis gizmo shows the "
     "lock. Saved with the preferences.",
     "Arrastrar para girar mueve la vista solo sobre el eje Z del mundo (azul): desde el frente, "
     "arrastrar alrededor del centro gira el modelo como una rueda. Mayús sigue ajustando a una vista "
     "lateral; el gizmo de ejes muestra el bloqueo. Se guarda con las preferencias."},
    {"Turntable", "Plataforma giratoria"},
    {"Camera notch", "Muesca de la cámara"},
    {"Always reserve", "Reservar siempre"},
    {"Off", "Desactivado"},
    {"Hold Ctrl and point at any control to see what it does and its shortcut. Ctrl+Alt shows a command's scripting "
     "parameters.",
     "Mantén Ctrl y apunta a cualquier control para ver qué hace y su atajo. Ctrl+Alt muestra los parámetros de "
     "script del comando."},
    {"How solid the Ctrl-help box is.", "Lo opaca que es la ayuda de Ctrl."},
    {"How long to point at an icon-only button (or a clipped label) before its name appears.",
     "Cuánto hay que apuntar a un botón de solo icono (o a una etiqueta cortada) antes de que aparezca su nombre."},
    {"Opening a sub-palette closes the others in that palette. Shift+click opens more than one.",
     "Abrir una subpaleta cierra las demás de esa paleta. Mayús+clic abre más de una."},
    {"Opening a palette in the left tray collapses the others when they would not fit.",
     "Abrir una paleta en la bandeja izquierda pliega las demás cuando no caben."},
    {"Opening a palette in the right tray collapses the others when they would not fit.",
     "Abrir una paleta en la bandeja derecha pliega las demás cuando no caben."},
    {"Mirror the shelves and trays: tools on the left, navigation on the right.",
     "Refleja los estantes y las bandejas: herramientas a la izquierda, navegación a la derecha."},
    {"How big the brush tiles on the bottom shelf are. Small leaves the most room for the model.",
     "El tamaño de las fichas de pinceles del estante inferior. Pequeño deja más sitio al modelo."},

    // --- Preferences > Input ---
    {"Style", "Estilo"},
    {"Classic sculpting keys", "Teclas clásicas de escultura"},
    {"Blender", "Blender"},
    {"Maya", "Maya"},
    {"SculptForge", "SculptForge"},
    {"Emulate Numpad", "Emular teclado numérico"},
    {"Right click opens the Quick Menu", "El clic derecho abre el menú rápido"},
    {"Hold Space and drag to move the camera", "Mantén Espacio y arrastra para mover la cámara"},
    {"Start strokes off the model", "Empezar trazos fuera del modelo"},
    {"Strokes start off the model", "Los trazos empiezan fuera del modelo"},
    {"Toggle strokes off the model", "Activar trazos fuera del modelo"},
    {"Cursor follows surface", "El cursor sigue la superficie"},
    {"Hide ring while stroking", "Ocultar el anillo al trazar"},
    {"Outlines", "Contornos"},
    {"Don't show again", "No volver a mostrar"},
    {"Hints", "Pistas"},
    {"Show hints in the viewport", "Mostrar pistas en el visor"},
    {"Show all hints again", "Volver a mostrar todas las pistas"},
    {"Previewing an LOD", "Vista previa de un LOD"},
    {"Face landmark steps", "Pasos de los puntos de la cara"},
    {"Subtle", "Sutil"},
    {"Outline opacity", "Opacidad de los contornos"},
    {"Viewport cursor", "Cursor del visor"},
    {"Crosshair", "Cruz"},
    {"Arrow", "Flecha"},
    {"Import Blender keymap", "Importar atajos de Blender"},
    {"Quick Menu", "Menú rápido"},

    // --- Preferences > Hotkeys ---
    {"Filter", "Filtrar"},
    {"press a key", "pulsa una tecla"},
    {"Default shortcuts", "Atajos predeterminados"},

    // --- Preferences > Display, Library, Tablet, Performance, Updates, Autosave ---
    {"Show at startup", "Mostrar al iniciar"},
    {"Show Library at startup", "Mostrar Biblioteca al iniciar"},
    {"Placement", "Ubicación"},
    {"Top", "Arriba"},
    {"Bottom", "Abajo"},
    {"Rows", "Filas"},
    {"Library folder", "Carpeta de la biblioteca"},
    {"Use the default folder", "Usar la carpeta predeterminada"},
    {"Mouse & pen", "Ratón y lápiz"},
    {"Mouse mode (relative)", "Modo ratón (relativo)"},
    {"Mouse mode", "Modo ratón"},
    {"View speed", "Velocidad de vista"},
    {"View speed %.2f", "Velocidad de vista %.2f"},
    {"How far the pen turns, pans and zooms the view, against the mouse's 1.00. A tablet spreads its whole area over "
     "the screen, so a short pen movement crosses many pixels: if the view spins too fast with the pen, turn this down.",
     "Cuánto gira, desplaza y acerca la vista el lápiz, frente al 1.00 del ratón. Una tableta reparte toda su área "
     "sobre la pantalla, así que un movimiento corto del lápiz recorre muchos píxeles: si la vista gira demasiado rápido "
     "con el lápiz, bájalo."},
    {"Pressure curve", "Curva de presión"},
    {"Pressure test", "Prueba de presión"},
    {"Pressure meter", "Medidor de presión"},
    {"Tablet defaults", "Valores predeterminados de la tableta"},
    {"Tilt  X %+.0f\xc2\xb0   Y %+.0f\xc2\xb0", "Inclinación  X %+.0f\xc2\xb0   Y %+.0f\xc2\xb0"},
    {"Min", "Mín."},
    {"Max", "Máx."},
    {"Minimum", "Mínimo"},
    {"Maximum", "Máximo"},
    {"Full at", "Completo a"},
    {"Full pressure at", "Presión completa a"},
    {"Curve", "Curva"},
    {"Pen", "Lápiz"},
    {"Keyboard", "Teclado"},
    {"Bring the pen close to the tablet. Pressure works through the system's tablet driver.",
     "Acerca el lápiz a la tableta. La presión funciona a través del controlador de la tableta del sistema."},
    {"Bring the pen close to the tablet. Wacom: turn on \"Use Windows Ink\" in Wacom Tablet Properties > Mapping; "
     "Huion / XP-Pen: enable Windows Ink in the driver.",
     "Acerca el lápiz a la tableta. Wacom: activa \"Use Windows Ink\" en Propiedades de la tableta Wacom > Mapping; "
     "Huion / XP-Pen: activa Windows Ink en el controlador."},
    {"SpaceMouse (experimental)", "SpaceMouse (experimental)"},
    {"Use SpaceMouse", "Usar SpaceMouse"},
    {"SpaceMouse speed", "Velocidad del SpaceMouse"},
    {"SpaceMouse defaults", "Valores predeterminados del SpaceMouse"},
    {"Speed", "Velocidad"},
    {"Dead zone", "Zona muerta"},
    {"Dominant axis", "Eje dominante"},
    {"Lock rotation to turntable", "Bloquear la rotación como plataforma giratoria"},
    {"Flat shading", "Sombreado plano"},
    {"Flat shading (on): every polygon shows its own face. Off: smooth shading, the default. The same as Material > Smooth | Flat; Parts set to Smooth or Flat keep their own. A preference, saved automatically.", "Sombreado plano (activado): cada polígono muestra su propia cara. Desactivado: sombreado suave, el predeterminado. Lo mismo que Material > Suave | Plano; las Partes configuradas como Suave o Plano conservan el suyo. Una preferencia, guardada automáticamente."},
    {"Rotation drags turn the view like a turntable, the model upright (on), or freely about the screen's axes (off). The same as View > Orbit lock: Y (turntable) or Free. Saved with the preferences.", "Al arrastrar para rotar, la vista gira como una plataforma giratoria, con el modelo derecho (activado), o libremente sobre los ejes de la pantalla (desactivado). Lo mismo que Vista > Bloqueo de órbita: Y (plataforma giratoria) o Libre. Se guarda con las preferencias."},
    {"Plug it in. If 3DxWare is installed it may keep the device to itself: see the Navigation page of the guide.",
     "Conéctalo. Si 3DxWare está instalado puede quedarse con el dispositivo: consulta la página Navegación de la "
     "guía."},
    {"Push the cap: if the bars below stay still, another program (3DxWare, spacenavd) holds the SpaceMouse. See the "
     "Navigation page of the guide.",
     "Mueve el mando: si las barras de abajo no se mueven, otro programa (3DxWare, spacenavd) tiene el SpaceMouse. "
     "Consulta la página Navegación de la guía."},
    {"No joysticks or game controllers seen.", "No se ve ningún joystick ni mando."},
    {"Plug it in. SculptForge reads it as a joystick (/dev/input): you need read access to it, and spacenavd may hold "
     "it. See the Navigation page of the guide.",
     "Conéctalo. SculptForge lo lee como un joystick (/dev/input): necesitas permiso de lectura, y spacenavd puede "
     "estar usándolo. Consulta la página Navegación de la guía."},
    {"Antialiasing", "Antialiasing"},
    {"Anti-aliasing", "Antialiasing"},
    {"Fast Navigation", "Navegación rápida"},
    {"Multithreading", "Multihilo"},
    {"Predictive Strokes", "Trazos predictivos"},
    {"Draft Strokes", "Trazos de borrador"},
    {"Live Move (Experimental)", "Mover en vivo (experimental)"},
    {"Background Preview (Experimental)", "Vista previa en segundo plano (experimental)"},
    {"Fast Upload (Experimental)", "Subida rápida (experimental)"},
    {"%d threads", "%d hilos"},
    {"Memory", "Memoria"},
    {"Hardware profile", "Perfil de hardware"},
    {"Frame Governor", "Regulador de fotogramas"},
    {"Re-calibrate", "Recalibrar"},
    {"Keep vertical sync on (frame rate capped at the display's)",
     "Mantener la sincronización vertical (fotogramas limitados a los de la pantalla)"},
    {"Every %d min", "Cada %d min"},
    {"Autosave off", "Autoguardado desactivado"},
    {"Save a recovery copy this often (0 = off). Restored after a crash.",
     "Guarda una copia de recuperación con esta frecuencia (0 = desactivado). Se restaura tras un fallo."},

    // --- Library ---
    {"Image", "Imagen"},
    {"Recent", "Recientes"},
    {"Nothing here yet.", "Aún no hay nada."},
    {"Nothing here yet. Projects you save (Ctrl+S) into the User folder show up here.",
     "Aún no hay nada. Aquí aparecen los proyectos que guardas (Ctrl+S) en la carpeta User."},
    {"Nothing here yet. Save Snapshot keeps timestamped copies of your scene here.",
     "Aún no hay nada. Guardar instantánea deja aquí copias de tu escena con fecha y hora."},
    {"Nothing here yet. Files you open show up here.", "Aún no hay nada. Aquí aparecen los archivos que abres."},
    {"Nothing here yet. Put reference images in the User folder.",
     "Aún no hay nada. Pon imágenes de referencia en la carpeta User."},
    {"Nothing here yet. Put Gaussian splat captures (.ply, .splat) in the User folder.",
     "Aún no hay nada. Pon capturas de Gaussian splats (.ply, .splat) en la carpeta User."},
    {"Back", "Atrás"},
    {"Forward", "Adelante"},
    {"Open folder", "Abrir carpeta"},
    {"Close Library", "Cerrar Biblioteca"},
    {"Relief from image", "Relieve a partir de la imagen"},
    {"Inflate image", "Inflar imagen"},
    {"Project onto model", "Proyectar sobre el modelo"},
    {"Use as alpha", "Usar como alpha"},
    {"Use as Alpha", "Usar como alpha"},
    {"Use as VDM", "Usar como VDM"},
    {"Show in file manager", "Mostrar en el gestor de archivos"},
    {"Refresh thumbnail", "Actualizar miniatura"},
    {"Nothing here matches \"%s\".", "Aquí nada coincide con \"%s\"."},
    {"Library rows", "Filas de la Biblioteca"},
    {"Library at the top", "Biblioteca arriba"},
    {"Library at the bottom", "Biblioteca abajo"},
    {"Show the items whose names contain this text.", "Muestra solo los elementos cuyo nombre contiene este texto."},
    {"Show only the items whose names contain this text.", "Muestra solo los elementos cuyo nombre contiene este texto."},
    {"The previous tab or folder.", "La pestaña o carpeta anterior."},
    {"Forward again after going back.", "Adelante otra vez después de volver atrás."},
    {"Hide the Library (comma toggles it).", "Oculta la Biblioteca (la coma la muestra u oculta)."},
    {"How many rows of thumbnails the Library shows (1-4).", "Cuántas filas de miniaturas muestra la Biblioteca (1-4)."},
    {"Your projects (.sfproj) and recently opened projects.", "Tus proyectos (.sfproj) y los abiertos recientemente."},
    {"Models to start from or append as Parts: primitives and model files.",
     "Modelos con los que empezar o que añadir como piezas: primitivas y archivos de modelo."},
    {"Every brush: built-in, custom and .sfbrush files in your library.",
     "Todos los pinceles: de serie, personalizados y archivos .sfbrush de tu biblioteca."},
    {"Brush alphas: built-in stamps and images in your Alphas folder.",
     "Alphas de pincel: sellos de serie e imágenes de tu carpeta Alphas."},
    {"Viewport materials: Studio and the Lit Spheres.", "Materiales de la vista: Studio y las Lit Spheres."},
    {"Reference images: generate 3D, relief, inflate, project onto the model or use as an alpha.",
     "Imágenes de referencia: genera 3D, relieve, infla, proyecta sobre el modelo o úsalas como alpha."},
    {"Gaussian splat captures (.ply, .splat) to sculpt against.",
     "Capturas de Gaussian splats (.ply, .splat) sobre las que esculpir."},
    {"Snapshots and the autosave, newest first.", "Las instantáneas y el autoguardado, los más nuevos primero."},
    {"Recently opened files of every kind.", "Archivos de todo tipo abiertos recientemente."},
    {"Add a folder of your own to the Library (shown at the root of every tab).",
     "Añade una carpeta tuya a la Biblioteca (aparece en la raíz de cada pestaña)."},
    {"Browse projects, models, brushes, alphas, materials, images and splats. Double-click to load; Shift+double-click "
     "a model to append it as a Part; drag sideways to scrub.",
     "Explora proyectos, modelos, pinceles, alphas, materiales, imágenes y splats. Doble clic para cargar; "
     "Mayús+doble clic en un modelo para añadirlo como pieza; arrastra a los lados para recorrerlos."},

    // --- Pickers and brushes ---
    {"Brush picker", "Selector de pinceles"},
    {"Alpha picker", "Selector de alphas"},
    {"Community brushes", "Comunidad"},
    {"Save to Library", "Guardar en la biblioteca"},
    {"Strength", "Fuerza"},
    {"Falloff", "Atenuación"},
    {"Name", "Nombre"},

    // --- Common words and buttons ---
    {"Apply", "Aplicar"},
    {"Reset", "Restablecer"},
    {"Preview", "Vista previa"},
    {"Turntable, Orthographic, Floor grid, Flat shading and Unlit: the window is too short to show them as buttons here, so they wait in this list.", "Plataforma giratoria, Ortográfica, Cuadrícula del suelo, Sombreado plano y Sin iluminación: la ventana es demasiado baja para mostrarlos como botones aquí, así que esperan en esta lista."},
    {"Unlit", "Sin iluminación"},
    {"Unlit view (on): the base colour alone, with no lighting, Lit Sphere or PBR shading, so painted colour reads exactly as stored. Works with vertex paint, texture paint and PBR; the mask and wireframe still draw. Saved with the project.", "Vista sin iluminación (activada): solo el color base, sin iluminación, Lit Sphere ni sombreado PBR, así el color pintado se ve exactamente como está guardado. Funciona con pintura de vértices, pintura de textura y PBR; la máscara y la malla de alambre se siguen dibujando. Se guarda con el proyecto."},
    {"Preview detail", "Detalle de vista previa"},
    {"Finish", "Terminar"},
    {"Close", "Cerrar"},
    {"OK", "Aceptar"},
    {"Yes", "Sí"},
    {"No", "No"},
    {"Lock", "Bloquear"},
    {"Live", "En vivo"},
    {"Mean", "Media"},
    {"Source", "Origen"},
    {"Method", "Método"},
    {"Weight", "Peso"},
    {"Review", "Revisar"},
    {"Network", "Red"},
    {"Contents", "Contenido"},
    {"Video", "Vídeo"},
    {"Surface", "Superficie"},
    {"World", "Mundo"},
    {"Vertex", "Vértice"},
    {"Front", "Frente"},
    {"Rear", "Atrás"},
    {"Sharpness", "Nitidez"},
    {"Padding", "Margen"},
    {"Map size", "Tamaño del mapa"},
    {"Map Size", "Tamaño del mapa"},
    {"Output folder", "Carpeta de salida"},
    {"Output Folder", "Carpeta de salida"},
    {"Open in browser", "Abrir en el navegador"},
    {"Open Folder", "Abrir carpeta"},

    // --- Project: unsaved changes, recovery, updates ---
    {"Clear Recent", "Borrar recientes"},
    {"Autosave (min)", "Autoguardado (min)"},
    {"Save changes to %s before continuing?", "¿Guardar los cambios en %s antes de continuar?"},
    {"Your changes will be lost if you don't save them.", "Si no los guardas, perderás los cambios."},
    {"Don't Save", "No guardar"},
    {"SculptForge did not close normally last time.", "SculptForge no se cerró correctamente la última vez."},
    {"Restore the autosaved work?", "¿Restaurar el trabajo autoguardado?"},
    {"What happened is in sculptforge-previous.log (Help > Open log folder);",
     "Lo ocurrido está en sculptforge-previous.log (Ayuda > Abrir la carpeta de registros);"},
    {"please attach it if you report the problem.", "adjúntalo si informas del problema."},
    {"Restore", "Restaurar"},
    {"Discard", "Descartar"},
    {"SculptForge %s is available", "SculptForge %s está disponible"},
    {"(you have %s)", "(tienes la %s)"},
    {"What's new", "Novedades"},
    {"Download", "Descargar"},
    {"Later", "Más tarde"},
    {"Skip this version", "Omitir esta versión"},
    {"Become a member", "Hazte miembro"},
    {"Members get betas early.", "Los miembros reciben las betas antes."},
    {"Checking", "Comprobando"},
    {"This build gets its updates from your package manager.",
     "Esta compilación se actualiza desde tu gestor de paquetes."},

    // --- Sculpt Timeline ---
    {"Sculpt Timeline", "Línea de tiempo de esculpido"},
    {"Step %zu of %zu", "Paso %zu de %zu"},
    {"Step %d: %s", "Paso %d: %s"},
    {"Save with project", "Guardar con el proyecto"},
    {"Start of the history", "Inicio del historial"},
    {"Show the model here", "Mostrar aquí el modelo"},
    {"Sculpting from an earlier step starts a branch;\nthe later steps stay as a lane above.",
     "Esculpir desde un paso anterior inicia una rama;\nlos pasos posteriores quedan como un carril encima."},
    {"The history of the active Part (%zu steps up to the cursor) played as a turntable.",
     "El historial de la pieza activa (%zu pasos hasta el cursor) reproducido en plataforma giratoria."},
    {"Length", "Duración"},
    {"Rendering timelapse", "Renderizando timelapse"},

    // --- Color, Vertex Paint, paint layers ---
    {"Fill", "Rellenar"},
    {"Fill layer", "Rellenar capa"},
    {"Fill masked", "Rellenar enmascarado"},
    {"Line Gradient", "Degradado lineal"},
    {"End", "Final"},
    {"Swap", "Intercambiar"},
    {"Drag a line across the model.", "Arrastra una línea sobre el modelo."},
    {"Drag its ends to adjust. Enter keeps it, Esc takes it back.", "Arrastra sus extremos para ajustarlo. Intro lo conserva, Esc lo deshace."},
    {"Swap the paint colour and the End colour.", "Intercambia el color de pintura y el color final."},
    {"The colour at the end of the line. The start is the paint colour above.",
     "El color al final de la línea. El inicio es el color de pintura de arriba."},
    {"Fade the paint colour out instead: the paint underneath shows through towards the end of the line.",
     "En su lugar, desvanece el color de pintura: la pintura de debajo se ve cada vez más hacia el final de "
     "la línea."},
    {"Drag a line across the model: the paint colour at its start runs into the End colour (or fades to "
     "clear) at its end, in screen space, into vertex paint or the texture. Drag either end to adjust it; "
     "Enter keeps it, Esc takes it back. Respects the mask; one undo step.",
     "Arrastra una línea sobre el modelo: el color de pintura de su inicio pasa al color final (o se "
     "desvanece) en su extremo, en el espacio de la pantalla, en la pintura de vértices o en la textura. "
     "Arrastra cualquiera de los extremos para ajustarlo; Intro lo conserva, Esc lo deshace. Respeta la "
     "máscara; un solo paso de deshacer."},
    {"Fill the unmasked area (the whole mesh if nothing is masked)",
     "Rellena la zona sin máscara (toda la malla si no hay nada enmascarado)"},
    {"Cavity", "Cavidad"},
    {"Darken creases and cavities (respects the mask)", "Oscurece pliegues y cavidades (respeta la máscara)"},
    {"Background", "Fondo"},
    {"Shading", "Sombreado"},
    {"Scene", "Escena"},
    {"Colour", "Color"},
    {"Base colour", "Color base"},
    {"Paint brush (P)", "Pincel de pintura (P)"},
    {"Paint into", "Pintar en"},
    {"Paint layer", "Capa de pintura"},
    {"Painting on: %s", "Pintando en: %s"},
    {"Painting on: texture, %s", "Pintando en: textura, %s"},
    {"Layer channel", "Canal de la capa"},
    {"Channel view", "Vista de canal"},
    {"RGB while sculpting", "RGB al esculpir"},
    {"Sculpt brushes also paint the colour", "Los pinceles de esculpir también pintan el color"},
    {"Min Opacity", "Opacidad mínima"},
    {"+ Add", "+ Añadir"},
    {"Group %d", "Grupo %d"},
    {"Group", "Grupo"},
    {"(Ctrl paints, Ctrl+Alt erases)", "(Ctrl pinta, Ctrl+Alt borra)"},
    {"(Ctrl masks, Ctrl+Alt unmasks)", "(Ctrl enmascara, Ctrl+Alt desenmascara)"},

    // --- More actions (shelves, Quick Menu, palettes) ---
    {"Shelves", "Estantes"},
    {"Frame", "Encuadrar"},
    {"Size at cursor", "Tamaño de dibujo en el cursor"},
    {"Intensity at cursor", "Intensidad en el cursor"},
    {"Colour intensity at cursor", "Intensidad RGB en el cursor"},
    {"Falloff at cursor", "Atenuación en el cursor"},
    {"Last stroke", "Último trazo"},
    {"To Surface", "A la superficie"},
    {"Crop Box", "Recortar a la caja"},
    {"Make Mesh", "Crear malla"},
    {"Delete Last", "Eliminar la última"},
    {"Make sculptable", "Hacer esculpible"},
    {"Sculpt to Splats", "Esculpir a splats"},
    {"Preview Normal Map", "Vista previa del mapa de normales"},
    {"AA Off", "AA desactivado"},
    {"AA 2x", "AA 2x"},
    {"AA 4x", "AA 4x"},
    {"AA 8x", "AA 8x"},
    {"These settings save themselves.", "Estos ajustes se guardan solos."},
    {"Pan X", "Desplazar X"},
    {"Pan Y", "Desplazar Y"},
    {"Roll", "Balanceo"},
    {"Tilt", "Inclinación"},
    {"Spin", "Giro"},

    // --- Reshape > Stack (PanelDeform.cpp) ---
    {"Turn", "Girar"},
    {"Bend", "Doblar"},
    {"Twist", "Torcer"},
    {"Taper", "Afilar"},
    {"Shear", "Inclinar"},
    {"Round", "Redondear"},
    {"Box", "Caja"},
    {"Sag", "Combar"},
    {"Some of this Part is hidden. %s works on all of it.",
     "Parte de esta pieza está oculta. %s actúa sobre toda ella."},
    {"Unhide and continue", "Mostrar y continuar"},
    {"Unhide everything first, so you see what changes. Undo brings it back.",
     "Primero se muestra todo, para que veas lo que cambia. Deshacer lo vuelve a ocultar."},
    {"Masked only", "Solo enmascarados"},
    {"Merge only masked points, with each other; the rest of the Part stays as it is. Mask a seam or an area first. The points that would merge show on the model while you point at this or the distance.",
     "Fusiona solo los puntos enmascarados, entre sí; el resto de la pieza queda como está. Enmascara antes una costura o una zona. Los puntos que se fusionarían se muestran en el modelo mientras señalas esto o la distancia."},
    {"Shape", "Forma"},
    {"Bend & Twist", "Doblar y torcer"},
    {"Stack", "Pila"},
    {"Add step", "Añadir paso"},
    {"No steps yet: Add step", "Aún no hay pasos: Añadir paso"},
    {"Direction", "Dirección"},
    {"Keep volume", "Mantener volumen"},
    {"Repeat", "Repetir"},
    {"Apply Selected", "Aplicar selección"},
    {"Add a deformation to the stack: Transform (Move, Turn, Scale, Squash), Bend & Twist (Bend, Twist, Taper, Shear), Shape (Round, Box, Flatten, Sag, Depth) or Surface (Polish, Relax, Smooth, Balloon). The steps run in order on the whole Part.",
     "Añade una deformación a la pila: Transformar (Mover, Girar, Escalar, Aplastar), Doblar y torcer (Doblar, Torcer, Afilar, Inclinar), Forma (Redondear, Caja, Aplanar, Combar, Profundidad) o Superficie (Pulir, Relajar, Suavizar, Globo). Los pasos se aplican en orden a toda la pieza."},
    {"Add the selected step again right after it (twice the twist, a second bend...).",
     "Añade de nuevo el paso seleccionado justo después (el doble de torsión, un segundo doblez...)."},
    {"An axis the selected step works along (several: one after the other, or together for Move, Scale, Round and Box).",
     "Un eje sobre el que actúa el paso seleccionado (varios: uno tras otro, o juntos para Mover, Escalar, Redondear y Caja)."},
    {"Ease the step out toward the ends of the Part instead of an even change along it (a bend that curves most in the middle, a cut with a rounded edge).",
     "Suaviza el paso hacia los extremos de la pieza en lugar de un cambio uniforme (un doblez que se curva más en el centro, un corte con el borde redondeado)."},
    {"Keep only the selected step on the Part (one undo step) and take it off the stack; the rest stay to edit.",
     "Conserva solo el paso seleccionado en la pieza (un paso de deshacer) y lo quita de la pila; el resto sigue editable."},
    {"Keep the stack's result on the Part (one undo step) and start a new stack. With Preview on, what you see is what stays.",
     "Conserva el resultado de la pila en la pieza (un paso de deshacer) y empieza una pila nueva. Con Vista previa activada, se queda lo que ves."},
    {"Show the stack on the Part as you edit it. It is applied for real (one undo step), so it saves and exports like any change; Reset takes it back.",
     "Muestra la pila en la pieza mientras la editas. Se aplica de verdad (un paso de deshacer), así que se guarda y exporta como cualquier cambio; Restablecer la deshace."},
    {"Take the preview back off the Part and empty the stack.", "Quita la vista previa de la pieza y vacía la pila."},
    {"Take the selected step off the stack.", "Quita el paso seleccionado de la pila."},
    {"Which way the step bends or leans, in degrees around its axis: 0 is toward +Y (toward +X for the Y axis).",
     "Hacia dónde se dobla o inclina el paso, en grados alrededor de su eje: 0 es hacia +Y (hacia +X para el eje Y)."},
    {"Uniform",
     "Uniforme"},
    {"Resizing one side resizes the others in proportion, keeping the Part's shape.",
     "Al cambiar el tamaño de un lado, los demás cambian en proporción y la pieza conserva su forma."},
    {"The active Part's width (X), height (Y) and depth (Z) in scene units. Drag, or double-click and type an exact value: the Part resizes about its centre (one undo step). With Uniform, the other sides follow in proportion.",
     "Ancho (X), alto (Y) y profundidad (Z) de la pieza activa en unidades de la escena. Arrastra, o haz doble clic y escribe un valor exacto: la pieza cambia de tamaño alrededor de su centro (un paso de deshacer). Con Uniforme, los demás lados siguen en proporción."},
    // --- Extract, Project, Deformation ---
    {"Edge Smooth", "Suavizar borde"},
    {"Edge Smooth %.0f", "Suavizar borde %.0f"},
    {"Corners", "Esquinas"},
    {"Border", "Borde"},
    {"Parts too", "También las piezas"},
    {"This Part", "Esta pieza"},
    {"Move along normals", "Mover a lo largo de las normales"},
    {"Mirror left and right", "Reflejar izquierda y derecha"},
    {"Flip X", "Voltear X"},
    {"Flip Y", "Voltear Y"},
    {"Flip Z", "Voltear Z"},
    {"Swap Y/Z", "Intercambiar Y/Z"},
    {"Squash", "Aplastar"},
    {"Fit plane", "Ajustar al plano"},
    {"Edge Fade", "Atenuación del borde"},
    {"Base Smoothing", "Suavizado de la base"},
    {"All pieces", "Todas las piezas"},
    {"Face loops", "Bucles de caras"},
    {"Low-poly", "Low-poly"},
    {"Base mesh", "Malla base"},

    // --- Bake ---
    {"Baking", "Haciendo bake"},
    {"AO rays", "Rayos de AO"},
    {"AO reach", "Alcance de AO"},
    {"Auto distance", "Distancia automática"},
    {"Max Frontal Distance", "Distancia frontal máx."},
    {"Max Rear Distance", "Distancia trasera máx."},
    {"Normal map convention", "Convención del mapa de normales"},
    {"Export preset", "Preajuste de exportación"},
    {"EXR displacement", "Desplazamiento EXR"},
    {"UV view", "Vista UV"},

    // --- Facial shapes, Make sculptable, phone, splats ---
    {"Generate Facial Shapes", "Generar formas faciales"},
    {"Record face take", "Grabar toma facial"},
    {"Play Take", "Reproducir toma"},
    {"Load Take", "Cargar toma"},
    {"Bake Take", "Hacer bake de la toma"},
    {"Clear Baked Take", "Borrar la toma con bake"},
    {"Cancel Bake", "Cancelar bake"},
    {"Take", "Toma"},
    {"Live axes", "Ejes en vivo"},
    {"Landmarks", "Puntos de referencia"},
    {"Place Landmarks", "Colocar puntos de referencia"},
    {"Auto Landmarks", "Puntos de referencia automáticos"},
    {"Clear Landmarks", "Borrar puntos de referencia"},
    {"Place by hand", "Colocar a mano"},
    {"Good enough - stop", "Suficiente: parar"},
    {"Try a test head", "Probar con una cabeza de prueba"},
    {"Face test", "Prueba facial"},
    {"From scan", "Desde el escaneo"},
    {"Keep scan", "Conservar el escaneo"},
    {"Generate from a photo at once", "Generar a partir de una foto de una vez"},
    {"Capture from view", "Capturar desde la vista"},
    {"Pairing code", "Código de emparejamiento"},
    {"QR code", "Código QR"},
    {"Unreal is on another computer", "Unreal está en otro ordenador"},
    {"Receive automatically", "Recibir automáticamente"},
    {"Update after each stroke", "Actualizar tras cada trazo"},
    {"Record a video of the run (needs ffmpeg)", "Grabar un vídeo de la ejecución (necesita ffmpeg)"},
    {"Choose Brush", "Elegir Brush"},
    {"Choose COLMAP", "Elegir COLMAP"},
    {"Photos Folder", "Carpeta de fotos"},
    {"Find again", "Buscar de nuevo"},

    // --- VDM, Kelvinlet, brush editing ---
    {"Create VDM", "Crear VDM"},
    {"Load VDM", "Cargar VDM"},
    {"New VDM Plane", "Nuevo plano VDM"},
    {"VDM name", "Nombre del VDM"},
    {"Plane Res", "Res. del plano"},
    {"Normal tilt", "Inclinación de la normal"},
    {"Detail Clone", "Detail Clone"},
    {"Clone Spacing", "Espaciado del clon"},
    {"Clone follows stroke", "El clon sigue el trazo"},
    {"Drape", "Drapeado"},
    {"Drops the active Part as cloth under gravity until it rests on the other Parts. It shows live; at rest the result is kept as one undo step.", "Deja caer la Part activa como una tela por gravedad hasta que reposa sobre las otras Parts. Se ve en directo; al reposar, el resultado se guarda como un solo paso de deshacer."},
    {"How big the sculpt is in real life: 1 when it is modelled at real size (a SculptForge unit is a metre). A body 2 units tall that should be a 1.8 m person: 1.11. Cloth drapes by real size.", "Qué tamaño tiene la escultura en la vida real: 1 si está modelada a tamaño real (una unidad de SculptForge es un metro). Un cuerpo de 2 unidades que debe ser una persona de 1,8 m: 1,11. La tela cae según el tamaño real."},
    {"How the cloth hangs: Silk flows into many fine folds, Cotton medium ones, Denim holds broad folds, Leather barely folds.", "Cómo cae la tela: la seda fluye en muchos pliegues finos, el algodón en medianos, la mezclilla mantiene pliegues anchos y el cuero apenas se pliega."},
    {"Masked areas hold still, like pins (a waistband, a collar).", "Las zonas enmascaradas se quedan quietas, como alfileres (una cintura, un cuello)."},
    {"Pins from mask", "Alfileres desde la máscara"},
    {"Resume", "Reanudar"},
    {"Simulate", "Simular"},
    {"The cloth falls onto the other visible Parts and rests on them (a shirt on a body).", "La tela cae sobre las otras Parts visibles y reposa en ellas (una camisa sobre un cuerpo)."},
    {"Units per metre", "Unidades por metro"},
    {"Units per metre %.2f", "Unidades por metro %.2f"},
    {"Cloth", "Tela"},
    {"Cloth brush: pick it from the brush shelf. Masked areas hold still, like pins.", "Pincel Cloth: elígelo en la barra de pinceles. Las zonas enmascaradas se quedan quietas, como alfileres."},
    {"Cloth mode", "Modo de tela"},
    {"Collide with other Parts", "Chocar con otras Parts"},
    {"Cotton", "Algodón"},
    {"Denim", "Mezclilla"},
    {"Drag", "Arrastrar"},
    {"Drag: the surface under the brush follows the stroke as fabric, folding where it bunches up. Push: presses it in along the surface (Alt pulls it out) and the cloth around follows. Settle: only gravity, the surface under the brush sags and drapes over what is below it. Grab: the cloth where the stroke starts is held and follows the brush, the rest trails and folds. Expand and Pinch: spread the cloth out from the brush centre or gather it in (Alt swaps). Inflate: adds fabric under the brush so it balloons out and wrinkles (Alt takes fabric away).", "Arrastrar: la superficie bajo el pincel sigue el trazo como una tela y se pliega donde se amontona. Empujar: la hunde a lo largo de la superficie (Alt la saca) y la tela de alrededor la sigue. Asentar: solo gravedad, la superficie bajo el pincel se descuelga y cae sobre lo que hay debajo. Agarrar: la tela donde empieza el trazo queda sujeta y sigue al pincel; el resto la sigue y se pliega. Expandir y Pellizcar: extienden la tela desde el centro del pincel o la recogen hacia él (Alt los intercambia). Inflar: añade tela bajo el pincel para que se hinche y se arrugue (Alt quita tela)."},
    {"Fabric", "Tejido"},
    {"Gravity", "Gravedad"},
    {"Gravity %.0f", "Gravedad %.0f"},
    {"How much of the surface around the brush is simulated, in brush radii. Further out stays put. More reach lets folds travel further and costs more.", "Cuánta superficie alrededor del pincel se simula, en radios del pincel. Lo que queda más lejos no se mueve. Más alcance deja que los pliegues lleguen más lejos y cuesta más."},
    {"How the cloth folds: Silk makes many fine folds, Cotton medium ones, Denim broad ones and Leather a few wide, stiff ones.", "Cómo se pliega la tela: la seda hace muchos pliegues finos, el algodón medianos, la mezclilla anchos y el cuero unos pocos, anchos y rígidos."},
    {"Leather", "Cuero"},
    {"Pulls the simulated cloth down (world down) while you drag, so folds hang. 0 keeps Drag and Push in place; Settle always uses at least 50.", "Tira de la tela simulada hacia abajo (abajo del mundo) mientras arrastras, para que los pliegues cuelguen. Con 0, Arrastrar y Empujar no la bajan; Asentar usa siempre al menos 50."},
    {"Push", "Empujar"},
    {"Reach", "Alcance"},
    {"Reach %.1f", "Alcance %.1f"},
    {"Settle", "Asentar"},
    {"Grab", "Agarrar"},
    {"Expand", "Expandir"},
    {"Pinch", "Pellizcar"},
    {"Soft body", "Cuerpo blando"},
    {"Soft body %.0f", "Cuerpo blando %.0f"},
    {"Self-collision", "Autocolisión"},
    {"0: the cloth stays where the brush leaves it. Higher: it springs back toward its shape at the start of the stroke, like a stuffed or elastic surface.", "0: la tela se queda donde la deja el pincel. Más: vuelve hacia su forma del inicio del trazo, como una superficie rellena o elástica."},
    {"Folds push against each other instead of passing through. A little slower on big regions.", "Los pliegues chocan entre sí en vez de atravesarse. Algo más lento en regiones grandes."},
    {"Silk", "Seda"},
    {"Simulation steps per dab: more is softer and settles further, but slower.", "Pasos de simulación por toque: más es más suave y se asienta más, pero más lento."},
    {"Steps", "Pasos"},
    {"Steps %d", "Pasos %d"},
    {"The cloth rests on the other visible Parts instead of passing through them (a shirt on a body).", "La tela se apoya en las otras Parts visibles en vez de atravesarlas (una camisa sobre un cuerpo)."},
    {"Refine the selected brush", "Refinar el pincel seleccionado"},
    {"Preset amount", "Cantidad del preajuste"},
    {"preset amount", "cantidad del preajuste"},
    {"Reset to 1", "Restablecer a 1"},
    {"Scatter", "Dispersión"},
    {"Loop", "Bucle"},
    {"Letter index", "Índice de letra"},
    {"Load image", "Cargar imagen"},
    {"Invert loaded image", "Invertir la imagen cargada"},
    {"Dark is high", "Oscuro es alto"},
    // --- App.cpp (tools/i18n/coverage.py) ---
    {"Strokes go into the base shape, not a layer.", "Los trazos van a la forma base, no a una capa."},
    {"Select to sculpt into this layer. With subdivision levels, record at any level: a layer keeps its own detail on every level and follows Lower Level / Higher Level and Add Level.", "Elige para esculpir en esta capa. Con niveles de subdivisión, graba en cualquier nivel: una capa guarda su propio detalle en cada nivel y sigue a Nivel inferior / Nivel superior y Añadir nivel."},
    {"Animate this blend shape 0 -> 1 -> 0 (Stop sets it back to 1).",
     "Anima esta forma de mezcla 0 -> 1 -> 0 (Detener la devuelve a 1)."},
    {"Delete this layer on every subdivision level (undoable).",
     "Borra esta capa en todos los niveles de subdivisión (se puede deshacer)."},
    {"How strongly the layer shows: 0 = off, 1 = as sculpted. One weight for every subdivision level.",
     "Cuánto se ve la capa: 0 = nada, 1 = tal como se esculpió. Un solo peso para todos los niveles de subdivisión."},
    {"Add a sculpt layer with the name typed beside it; strokes go into it.",
     "Añade una capa de escultura con el nombre escrito al lado; los trazos van a ella."},
    {"Hugging Face token", "Token de Hugging Face"},
    {"Free at huggingface.co/settings/tokens (read access). Raises the free daily allowance in Generate 3D, and "
     "TRELLIS.2 needs it. Empty = the HF_TOKEN environment variable.",
     "Gratis en huggingface.co/settings/tokens (acceso de lectura). Sube el límite diario gratuito de Generar 3D, y "
     "TRELLIS.2 lo necesita. Vacío = la variable de entorno HF_TOKEN."},
    {"Load a built-in test capture (a stylised head as Gaussian splats, with a few floaters) to try Make sculptable "
     "without a scan of your own. splat_test_capture kind=object loads a mug instead.",
     "Carga una captura de prueba incluida (una cabeza estilizada en splats gaussianos, con algunos flotantes) para "
     "probar Hacer esculpible sin un escaneo propio. splat_test_capture kind=object carga una taza."},
    // --- Config.cpp (tools/i18n/coverage.py) ---
    {"Fill Holes", "Rellenar agujeros"},
    {"Cap every open boundary of the active Part (a hole, an open bottom, the rim of an extracted shell) with a new "
     "patch sized like the polygons around it, so remeshing and sculpting see a closed surface. The rest of the Part "
     "stays as it is; the patches get a face group of their own (#103).",
     "Tapa cada borde abierto de la Pieza activa (un agujero, una base abierta, el borde de una cáscara extraída) "
     "con un parche nuevo del tamaño de los polígonos de alrededor, para que el remallado y la escultura vean una "
     "superficie cerrada. El resto de la Pieza no cambia; los parches reciben su propio grupo de caras (#103)."},
    {"Add a subdivision level: four times the polygons, and the lower levels are kept, so you can step down to shape big forms and back up to the detail. Quads stay quads (Catmull-Clark). Smooth off divides without smoothing.", "Añade un nivel de subdivisión: cuatro veces los polígonos, y se conservan los niveles inferiores, para bajar a dar forma a lo grande y volver al detalle. Los quads siguen siendo quads (Catmull-Clark). Con Suavizar apagado divide sin suavizar."},
    {"Smooth the next new level (on by default). Off: the new level keeps the hard edges and flat faces of the one below.", "Suaviza el próximo nivel nuevo (activado por defecto). Apagado: el nuevo nivel conserva los bordes duros y las caras planas del de abajo."},
    {"Step one subdivision level down (fewer polygons, for big forms). The detail of the higher levels is kept.",
     "Baja un nivel de subdivisión (menos polígonos, para las formas grandes). Se conserva el detalle de los niveles "
     "superiores."},
    {"Step one subdivision level up (more polygons, for detail). Edits made below carry the detail along.",
     "Sube un nivel de subdivisión (más polígonos, para el detalle). Los cambios hechos abajo arrastran el detalle."},
    {"Store the surface above the highest level as micro displacements: hundreds of millions of sculptable triangles "
     "in a fraction of the memory (300M take about 0.4 GB). The Micro slider sets how fine.",
     "Guarda la superficie por encima del nivel más alto como microdesplazamientos: cientos de millones de "
     "triángulos esculpibles en una fracción de la memoria (300M ocupan unos 0,4 GB). El control Micro fija lo fino "
     "que es."},
    {"Turn the micro level into a regular mesh in a new Part (for export or tools that need real geometry).",
     "Convierte el nivel micro en una malla normal en una Pieza nueva (para exportar o para herramientas que "
     "necesitan geometría real)."},
    {"Write the micro level as a 32-bit EXR displacement map over the base UVs (next to the project).",
     "Escribe el nivel micro como mapa de desplazamiento EXR de 32 bits sobre las UV base (junto al proyecto)."},
    {"Sculpting: %s", "Esculpiendo: %s"},
    {"No Part under the cursor", "No hay ninguna Pieza bajo el cursor"},
    {"Sculpt the Part under the cursor", "Esculpir la Pieza bajo el cursor"},
    {"Make the Part under the pointer the one you sculpt, without moving the view (each Part keeps its mask). "
     "The same key as Blender's; Preferences > Hotkeys changes it.",
     "Hace que la Pieza bajo el puntero sea la que esculpes, sin mover la vista (cada Pieza conserva su máscara). La misma tecla que en Blender; se cambia en Preferencias > Atajos de teclado."},
    {"Duplicate Part", "Duplicar Pieza"},
    {"Copy the active Part, with its layers and attributes, and make the copy active (also in the Parts list's "
     "right-click menu).",
     "Copia la Pieza activa, con sus capas y atributos, y activa la copia (también en el menú contextual de la lista "
     "de Piezas)."},
    {"Make a shell from the masked part of the Part (a helmet, a strap, clothing), shown as a preview below it. "
     "Change Thickness, Edge Smooth, Both Sides, Corners or Border and it updates; Accept keeps it as a new Part.",
     "Crea una cáscara a partir de la parte enmascarada de la Pieza (un casco, una correa, ropa), con una vista "
     "previa debajo. Cambia Grosor, Suavizar borde, Ambos lados, Esquinas o Borde y se actualiza; Aceptar la guarda "
     "como Pieza nueva."},
    {"Keep the Extract preview as a new Part below this one. Ctrl+Z takes it away again.",
     "Guarda la vista previa de Extraer como Pieza nueva debajo de esta. Ctrl+Z la quita de nuevo."},
    {"Grow the extracted shell both inward and outward from the surface, instead of outward only.",
     "Haz crecer la cáscara extraída hacia dentro y hacia fuera de la superficie, en vez de solo hacia fuera."},
    {"Triangle corners: where the extracted edge turns sharply, the rim is made of triangles instead of quads.",
     "Esquinas en triángulos: donde el borde extraído gira bruscamente, el reborde se hace con triángulos en vez de "
     "quads."},
    {"Triangle border: the whole rim of the extracted shell is made of triangles instead of quads.",
     "Borde en triángulos: todo el reborde de la cáscara extraída se hace con triángulos en vez de quads."},
    {"Mirror the + side of the Part onto the - side across the chosen axis and weld the seam into one watertight "
     "mesh. Face Groups and vertex paint come along. To keep the other side, Symmetry > Mirror first.",
     "Refleja el lado + de la Pieza sobre el lado - en el eje elegido y suelda la costura en una sola malla estanca. "
     "Los grupos de caras y la pintura de vértices también pasan. Para conservar el otro lado, usa antes Simetría "
     "> Reflejar."},
    {"Flip the Part across the chosen axis, through the origin (the plane symmetry and Make Symmetric use).", "Voltea la Pieza en el eje elegido, a través del origen (el plano que usan la simetría y Hacer simétrico)."},
    {"An axis Make Symmetric mirrors across (several: one after the other).", "Un eje en el que refleja Hacer simétrico (varios: uno tras otro)."},
    {"An axis Mirror flips along (several: one after the other).",
     "Un eje en el que voltea Reflejar (varios: uno tras otro)."},
    {"New from Starter Mesh...", "Nuevo desde malla inicial..."},
    {"Start from a base mesh: human head, bust, full body, hand, foot, ear, eye, stylised and creature heads, torso, "
     "or a bevelled box, cylinder, capsule or ring (Library > Tool > Starter meshes).",
     "Empieza desde una malla base: cabeza humana, busto, cuerpo entero, mano, pie, oreja, ojo, cabezas estilizadas "
     "y de criatura, torso, o una caja biselada, cilindro, cápsula o anillo (Biblioteca > Herramienta > Mallas "
     "iniciales)."},
    {"Open a project (.sfproj), a model (.obj, .glb, .fbx, .stl, .ply, .usd ...) or splats.",
     "Abre un proyecto (.sfproj), un modelo (.obj, .glb, .fbx, .stl, .ply, .usd ...) o splats."},
    {"Save the project (.sfproj: every Part, layer, brush and setting). Asks for a name the first time.",
     "Guarda el proyecto (.sfproj: cada Pieza, capa, pincel y ajuste). La primera vez pide un nombre."},
    {"Save the project under a new name.", "Guarda el proyecto con otro nombre."},
    {"Export the model as glTF binary, with sculpt layers as blend shapes (morph targets).",
     "Exporta el modelo como glTF binario, con las capas de escultura como formas de mezcla (morph targets)."},
    {"Save the visible Parts as one binary STL, for 3D printing.",
     "Guarda las Piezas visibles como un solo STL binario, para impresión 3D."},
    {"Export a game-ready FBX and import it into the running Unreal Editor (Python remote execution).",
     "Exporta un FBX listo para juegos y lo importa en el Unreal Editor abierto (ejecución remota de Python)."},
    {"Send the visible Parts back to Blender (the SculptForge add-on): objects that came from Blender update in "
     "place, keeping their transform, materials and modifiers; new Parts become new objects.",
     "Envía las Piezas visibles de vuelta a Blender (el complemento de SculptForge): los objetos que vinieron de "
     "Blender se actualizan en su sitio, con su transformación, materiales y modificadores; las Piezas nuevas pasan "
     "a ser objetos nuevos."},
    {"Load what Blender last sent with the add-on's Send to SculptForge. Each object updates its Part (undoable) or "
     "becomes a new one; this happens by itself while Receive automatically is on.",
     "Carga lo último que Blender envió con Send to SculptForge del complemento. Cada objeto actualiza su Pieza (se "
     "puede deshacer) o pasa a ser una nueva; ocurre solo mientras Recibir automáticamente está activado."},
    {"Send to Maya", "Enviar a Maya"},
    {"Send the visible Parts to Maya (OBJ with quads and UVs) through the exchange folder you chose; Get from "
     "SculptForge on Maya's SculptForge shelf loads them, replacing the objects they came from by name.",
     "Envía las Piezas visibles a Maya (OBJ con quads y UV) por la carpeta de intercambio que elegiste; Get from "
     "SculptForge, en el estante de SculptForge de Maya, las carga y reemplaza por nombre los objetos de los que "
     "vinieron."},
    {"Get from Maya", "Recibir de Maya"},
    {"Load what Maya last sent with the shelf's Send to SculptForge. Each object replaces the Part with its name "
     "(undoable) or becomes a new one.",
     "Carga lo último que Maya envió con Send to SculptForge del estante. Cada objeto reemplaza la Pieza con su "
     "nombre (se puede deshacer) o pasa a ser una nueva."},
    {"Exchange folder...", "Carpeta de intercambio..."},
    {"Choose the folder SculptForge and Maya exchange meshes through (pick the same one in Maya's shelf script). "
     "SculptForge reads and writes only there: when you send or get, and when Maya sends while Get automatically is on.",
     "Elige la carpeta por la que SculptForge y Maya intercambian mallas (elige la misma en el script del estante de "
     "Maya). SculptForge solo lee y escribe ahí: cuando envías o recibes, y cuando Maya envía con Recibir "
     "automáticamente activado."},
    {"Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file.",
     "Añade el pincel del portapapeles: un código de pincel que alguien compartió, o el texto de un archivo .sfbrush."},
    {"Every brush keeps its own Size, Intensity, Falloff, Colour intensity, stroke and alpha while you work. This "
     "saves the current brush's, so it starts with them every time SculptForge opens.",
     "Cada pincel guarda su propio Tamaño, Intensidad, Atenuación, Intensidad RGB, trazo y alpha mientras trabajas. "
     "Esto guarda los del pincel actual, para que empiece con ellos cada vez que se abre SculptForge."},
    {"Put the current brush's Size, Intensity, Falloff, Colour intensity, stroke and alpha back to the values "
     "SculptForge ships with, and forget what Save Brush Settings stored for it.",
     "Devuelve el Tamaño, Intensidad, Atenuación, Intensidad RGB, trazo y alpha del pincel actual a los valores de "
     "fábrica de SculptForge, y olvida lo que guardó Guardar ajustes del pincel."},
    {"On: Size and Intensity stay as they are when you pick another brush (as before 0.10.8); Falloff, Colour "
     "intensity, the stroke and the alpha still follow each brush. Off (the default): every brush keeps its own.",
     "Activado: Tamaño e Intensidad no cambian al elegir otro pincel (como antes de 0.10.8); Atenuación, Intensidad "
     "RGB, el trazo y el alpha siguen a cada pincel. Desactivado (por defecto): cada pincel guarda los suyos."},
    {"Make the brush 10% smaller (hold to repeat; Shift+[ steps one pixel).",
     "Reduce el pincel un 10% (mantén para repetir; Mayús+[ baja un píxel)."},
    {"Make the brush 10% larger (hold to repeat; Shift+] steps one pixel).",
     "Agranda el pincel un 10% (mantén para repetir; Mayús+] sube un píxel)."},
    {"Mirror strokes across the X axis (left / right).", "Refleja los trazos en el eje X (izquierda / derecha)."},
    {"Show the mesh's polygons (quads and triangles) over the surface.",
     "Muestra los polígonos de la malla (quads y triángulos) sobre la superficie."},
    {"Fit the whole model in the view.", "Encuadra todo el modelo en la vista."},
    {"Center on origin", "Centrar en el origen"},
    {"Drop to floor", "Apoyar en el suelo"},
    {"Fit every visible Part in the view (bind a key to it: F frames the active Part on the Classic sculpting keys).", "Encuadra todas las piezas visibles en la vista (asígnale una tecla: con las teclas de escultura clásicas, F encuadra la pieza activa)."},
    {"Fit the whole model in the view. Classic sculpting keys, with several Parts: F frames the active Part, F twice quickly frames the whole scene.", "Encuadra todo el modelo en la vista. Con las teclas de escultura clásicas y varias piezas, F encuadra la pieza activa y F dos veces seguidas encuadra toda la escena."},
    {"Frame double-tap", "Doble pulsación de Encuadrar"},
    {"Frame double-tap %.0f ms", "Doble pulsación de Encuadrar %.0f ms"},
    {"Frame scene", "Encuadrar escena"},
    {"Move the Part so the centre of its bounding box sits at the world origin 0,0,0, as one undo step (with All Parts: every visible Part together, keeping how they sit to each other).", "Mueve la pieza para que el centro de su caja envolvente quede en el origen del mundo 0,0,0, en un solo paso de deshacer (con Todas las piezas: todas las piezas visibles juntas, conservando su posición relativa)."},
    {"Move the Part straight down (or up) until its lowest point rests on the floor, Y = 0, as one undo step (with All Parts: every visible Part together).", "Mueve la pieza hacia abajo (o arriba) hasta que su punto más bajo toque el suelo, Y = 0, en un solo paso de deshacer (con Todas las piezas: todas las piezas visibles juntas)."},
    {"With several Parts, F frames the active Part and F twice within this time frames the whole scene. 0: F always frames the whole scene.", "Con varias piezas, F encuadra la pieza activa y F dos veces dentro de este tiempo encuadra toda la escena. 0: F siempre encuadra toda la escena."},
    {"Take the paint colour from the model under the cursor.", "Toma el color de pintura del modelo bajo el cursor."},
    {"Every shortcut and mouse gesture, generated from the commands themselves.",
     "Cada atajo y gesto del ratón, generados a partir de los propios comandos."},
    {"The SculptForge user guide: every tool explained, with search. Hold Ctrl over a control and press F1 to open "
     "the chapter about it.",
     "La guía de usuario de SculptForge: cada herramienta explicada, con búsqueda. Mantén Ctrl sobre un control y "
     "pulsa F1 para abrir su capítulo."},
    {"Your first sculpt (10 min)", "Tu primera escultura (10 min)"},
    {"A step-by-step lesson for complete beginners: a cute owl head from the default sphere, painted and rendered, in "
     "about ten minutes.",
     "Una lección paso a paso para principiantes totales: una cabeza de búho a partir de la esfera inicial, pintada "
     "y renderizada, en unos diez minutos."},
    {"Quick tour", "Recorrido rápido"},
    {"Ten short slides for sculptors who know another app: where remeshing, dynamic topology, retopology, levels, "
     "masking, cut, brushes and baking are, each with a Show me button.",
     "Diez diapositivas breves para escultores que conocen otra app: dónde están el remallado, la topología "
     "dinámica, la retopología, los niveles, el enmascarado, el corte, los pinceles y el horneado, cada una con un "
     "botón Muéstrame."},
    {"What changed in this version and the ones before it (opens once by itself after an update).",
     "Qué cambió en esta versión y en las anteriores (se abre sola una vez tras actualizar)."},
    {"Ask the SculptForge website whether a newer version is out. SculptForge also checks by itself once a day "
     "(Preferences > Updates).",
     "Pregunta al sitio web de SculptForge si hay una versión nueva. SculptForge también lo comprueba solo una vez "
     "al día (Preferencias > Actualizaciones)."},
    {"Version, licence and the open-source components SculptForge is built with.",
     "Versión, licencia y los componentes de código abierto con los que está hecho SculptForge."},
    {"SculptForge is free and stays free. A membership adds betas and early builds, Member Requests (one request a "
     "month, built first and credited to you), a vote on what we build next and your name in the credits: opens sculptforge.app/support.html in "
     "your browser. Nothing in the app changes either way.",
     "SculptForge es gratis y seguirá siéndolo. Ser miembro suma betas y versiones anticipadas, Member Requests "
     "(una petición al mes, la hacemos primero y con tu nombre), voto sobre lo próximo que hacemos y tu nombre en los créditos: abre "
     "sculptforge.app/support.html en tu navegador. En la app no cambia nada en ningún caso."},
    {"The official SculptForge community on Discord: questions, tips and work in progress. Opens the invite in your "
     "browser. Bugs still go to Report a problem (GitHub issues).",
     "La comunidad oficial de SculptForge en Discord: preguntas, consejos y trabajos en curso. Abre la invitación en "
     "tu navegador. Los errores siguen yendo a Informar de un problema (issues de GitHub)."},
    {"The welcome note shown the first time SculptForge starts.",
     "La nota de bienvenida que se muestra la primera vez que se abre SculptForge."},
    {"Open the SculptForge website and user guide in your browser.",
     "Abre el sitio web y la guía de usuario de SculptForge en tu navegador."},
    {"Open the issue tracker in your browser.", "Abre el gestor de incidencias en tu navegador."},
    {"Open the folder with sculptforge.log: what SculptForge did and, after a crash, what crashed. "
     "sculptforge-previous.log beside it is the last session's. Attach both to a problem report.",
     "Abre la carpeta con sculptforge.log: lo que hizo SculptForge y, tras un cierre inesperado, qué falló. "
     "sculptforge-previous.log, al lado, es el de la sesión anterior. Adjunta ambos a un informe de problema."},
    {"While on, sculptforge-trace.log in the log folder records what makes SculptForge slow: every frame over 33 ms "
     "with where its time went, strokes, uploads to the GPU, remeshes and other background jobs, the mesh size and "
     "levels, and the GPU and driver. Turn it on, do what is slow, turn it off and attach the file to your report. A "
     "small file on this computer only; off by default and costs nothing when off.",
     "Mientras está activado, sculptforge-trace.log en la carpeta de registros anota lo que hace lento a "
     "SculptForge: cada fotograma de más de 33 ms y en qué se fue su tiempo, trazos, subidas a la GPU, remallados y "
     "otras tareas en segundo plano, el tamaño de la malla y sus niveles, y la GPU y el controlador. Actívalo, haz "
     "lo que va lento, desactívalo y adjunta el archivo a tu informe. Un archivo pequeño solo en este equipo; "
     "desactivado por defecto y sin coste cuando está apagado."},
    {"Submit a request for a fix or a suggestion: opens the bug report and feature request forms in your browser.",
     "Envía una petición de arreglo o una sugerencia: abre en tu navegador los formularios de informe de error y de "
     "petición de función."},
    {"Search every command, brush and setting by name and run it. Commands that need parameters open in the console.",
     "Busca por nombre cualquier comando, pincel o ajuste y ejecútalo. Los comandos que necesitan parámetros se "
     "abren en la consola."},
    {"The script console: every command you run appears as a script line, and you can type commands yourself.",
     "La consola de scripts: cada comando que ejecutas aparece como una línea de script, y puedes escribir comandos "
     "tú mismo."},
    {"Run a text file of script statements, one per line (# starts a comment).",
     "Ejecuta un archivo de texto con sentencias de script, una por línea (# empieza un comentario)."},
    {"Put every command run this session on the clipboard as a script, ready to replay (a macro recorder).",
     "Copia al portapapeles cada comando ejecutado en esta sesión como un script, listo para repetirlo (una "
     "grabadora de macros)."},
    {"Empty the console log (the session script is kept).",
     "Vacía el registro de la consola (el script de la sesión se conserva)."},
    {"Keep the current layout (shelves, trays, open palettes) for future sessions. Preferences and shortcuts save "
     "themselves.",
     "Conserva el diseño actual (estantes, bandejas, paletas abiertas) para próximas sesiones. Las preferencias y "
     "los atajos se guardan solos."},
    {"Write the preferences and shortcuts to disk now. Not needed: they save themselves a moment after every change.",
     "Escribe ahora las preferencias y los atajos en el disco. No hace falta: se guardan solos un momento después de "
     "cada cambio."},
    {"Go back to the built-in layout, shortcuts and radial menus. The shortcuts save themselves; Save Layout keeps "
     "the layout.",
     "Vuelve al diseño, los atajos y los menús radiales de fábrica. Los atajos se guardan solos; Guardar diseño "
     "conserva el diseño."},
    {"Go back to the layout the last Save Layout kept.", "Vuelve al diseño que conservó el último Guardar diseño."},
    {"Load a layout and shortcuts file someone shared (or you saved with Export Interface).",
     "Carga un archivo de diseño y atajos que alguien compartió (o que guardaste con Exportar interfaz)."},
    {"Save the layout, shortcuts and interface preferences to a file you can share.",
     "Guarda el diseño, los atajos y las preferencias de la interfaz en un archivo que puedes compartir."},
    {"Rearrange the shelves: Ctrl+Alt+drag any button onto a shelf to add or move it, or off the shelves to remove it.",
     "Reorganiza los estantes: Ctrl+Alt+arrastra cualquier botón a un estante para añadirlo o moverlo, o fuera de "
     "los estantes para quitarlo."},
    {"The short notes SculptForge shows over the 3D view (a mode that is on, the next step of a tool). Off hides them "
     "all; each also has its own Don't show again.",
     "Las notas breves que SculptForge muestra sobre la vista 3D (un modo activado, el siguiente paso de una "
     "herramienta). Apagado las oculta todas; cada una tiene también su propio No volver a mostrar."},
    {"Hover and focus highlights change colour only: no easing, and tiles don't lift or grow.",
     "Los resaltados al pasar y al enfocar solo cambian de color: sin transiciones, y los mosaicos no se elevan ni "
     "crecen."},
    {"Off: File and Edit come first in the palette bar and Help last, the rest A to Z. On: every palette A to Z.",
     "Desactivado: Archivo y Editar van primero en la barra de paletas y Ayuda al final, el resto de la A a la Z. "
     "Activado: todas las paletas de la A a la Z."},
    {"Size of text and controls, applied at once. Automatic follows the display the window is on (Windows: Settings > "
     "Display > Scale, per monitor).",
     "Tamaño del texto y los controles, aplicado al instante. Automático sigue a la pantalla en la que está la "
     "ventana (Windows: Configuración > Pantalla > Escala, por monitor)."},
    {"Free: dragging turns the model about the screen's axes and can roll it over the top; press Shift while rotating "
     "to snap to the nearest front, side, top or bottom view. Turntable: the model stays upright and turns about its "
     "vertical axis, as in Blender or Maya. X axis, Z axis: it turns about that world axis only (View > Orbit lock).",
     "Libre: arrastrar gira el modelo sobre los ejes de la pantalla y puede darle la vuelta por arriba; pulsa Mayús "
     "mientras giras para ajustar a la vista frontal, lateral, superior o inferior más cercana. Plataforma "
     "giratoria: el modelo se queda derecho y gira sobre su eje vertical, como en Blender o Maya."
     " Eje X, eje Z: gira solo sobre ese eje del mundo (Vista > Bloqueo de órbita)."},
    {"Keeps the interface below the camera notch of MacBooks (and other laptops) when SculptForge fills the screen. "
     "Automatic asks the system (macOS) or recognises a notched MacBook display (Linux).",
     "Mantiene la interfaz por debajo de la muesca de la cámara de los MacBook (y otros portátiles) cuando "
     "SculptForge ocupa toda la pantalla. Automático pregunta al sistema (macOS) o reconoce una pantalla de MacBook "
     "con muesca (Linux)."},
    {"Click, then press the new key. Esc cancels, Backspace clears. Orange: changed from the default.",
     "Haz clic y pulsa la tecla nueva. Esc cancela, Retroceso borra. Naranja: cambiado respecto al de fábrica."},
    {"Undo every shortcut change.", "Deshace todos los cambios de atajos."},
    {"Open Library on the Project tab when SculptForge starts.",
     "Abre la Biblioteca en la pestaña Proyecto al iniciar SculptForge."},
    {"How many rows of thumbnails the Library strip shows.",
     "Cuántas filas de miniaturas muestra la tira de la Biblioteca."},
    {"Library opens along the top edge of the canvas.", "La Biblioteca se abre en el borde superior del lienzo."},
    {"Library opens above the bottom shelf.", "La Biblioteca se abre encima del estante inferior."},
    {"Where your projects, tools, brushes, alphas, images, splats and Snapshots live. Library shows it as User.",
     "Dónde viven tus proyectos, herramientas, pinceles, alphas, imágenes, splats e Instantáneas. La Biblioteca lo "
     "muestra como Usuario."},
    {"Colours", "Colores"},
    {"Forge Terminal's colour scheme, from well-known open-source terminal themes.",
     "El esquema de colores de Forge Terminal, de temas de terminal de código abierto conocidos."},
    {"Uses your Omarchy theme's colours when Omarchy is installed (read from its theme folder; SculptForge never "
     "changes it). After switching themes, toggle this to pick up the new colours.",
     "Usa los colores de tu tema de Omarchy cuando Omarchy está instalado (los lee de su carpeta de temas; "
     "SculptForge nunca lo cambia). Tras cambiar de tema, alterna esto para tomar los colores nuevos."},
    {"Faint scanlines and a glow around the focused panel, on the interface only (never on the 3D view). Off by "
     "default.",
     "Líneas de barrido tenues y un brillo alrededor del panel enfocado, solo en la interfaz (nunca en la vista 3D). "
     "Desactivado por defecto."},
    {"The borders around the panels in Forge Terminal, Phosphor and Blueprint. Subtle (the default): faint, and "
     "nothing lights up under the pointer. Off: no borders, the gaps alone separate the panels. On: clear borders, "
     "the panel under the pointer a faint step lighter.",
     "Los bordes de los paneles en Forge Terminal, Phosphor y Blueprint. Sutil (por defecto): tenues, y nada se "
     "ilumina bajo el puntero. Desactivado: sin bordes, solo los huecos separan los paneles. Activado: bordes "
     "claros, el panel bajo el puntero un poco más claro."},
    {"How strong the panel borders are (0-100%), on top of Outlines: 100% draws them as Subtle or On does, lower "
     "fades them into the panels (#98). Ctrl+click to type.",
     "La fuerza de los bordes de los paneles (0-100%), sobre Contornos: 100% los dibuja como Sutil o Activado, menos "
     "los funde con los paneles (#98). Ctrl+clic para escribir."},
    {"SculptForge layout", "Diseño de SculptForge"},
    {"Menus by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help), a status bar and a one-row tool shelf. "
     "Every palette is a section of one menu, and F3 finds them all.",
     "Menús por tarea (Archivo, Editar, Esculpir, Pintar, Malla, Vista, Render, Ayuda), una barra de estado y un "
     "estante de herramientas de una fila. Cada paleta es una sección de un menú, y F3 las encuentra todas."},
    {"Classic layout", "Diseño clásico"},
    {"The alphabetical palette bar and the two-row top shelf.",
     "La barra de paletas alfabética y el estante superior de dos filas."},
    // --- Console.cpp (tools/i18n/coverage.py) ---
    {"Hide the console. The log and the session script are kept.",
     "Oculta la consola. Se conservan el registro y el script de la sesión."},
    {"Runs SculptForge script: one command per line or separated by ';', parameters as name=value (quote text with "
     "spaces). Tab completes, Up/Down recalls, help lists every command.",
     "Ejecuta script de SculptForge: un comando por línea o separados por ';', parámetros como nombre=valor (pon "
     "entre comillas el texto con espacios). Tab completa, Arriba/Abajo recupera, help lista todos los comandos."},
    // --- Gizmo.cpp (tools/i18n/coverage.py) ---
    {"Draw mode: brushes sculpt and paint (leaves the Gizmo).",
     "Modo Dibujar: los pinceles esculpen y pintan (sale del gizmo)."},
    {"Gizmo, Move: drag an arrow to move along an axis, a square to move in a plane, the centre to move in the view. "
     "Masked parts stay; Shift snaps to the grid; Alt+drag moves only the gizmo. W again: back to Draw.",
     "Gizmo, Mover: arrastra una flecha para mover en un eje, un cuadrado para mover en un plano, el centro para "
     "mover en la vista. Lo enmascarado se queda; Mayús ajusta a la rejilla; Alt+arrastrar mueve solo el gizmo. W "
     "otra vez: vuelve a Dibujar."},
    {"Gizmo, Scale: drag a ball to stretch along an axis or the centre to scale evenly (right or up: bigger). Shift "
     "snaps to 10% steps. E again: back to Draw.",
     "Gizmo, Escalar: arrastra una bola para estirar en un eje o el centro para escalar por igual (derecha o arriba: "
     "más grande). Mayús ajusta a pasos del 10%. E otra vez: vuelve a Dibujar."},
    {"Gizmo, All: move, rotate and scale handles at once. Arrows and squares move, the centre moves in the view, "
     "rings turn, the ball on each axis stretches along it and the four outer tabs scale evenly. Shift snaps. "
     "Shift+W again: back to Draw.",
     "Gizmo, Todo: asas de mover, rotar y escalar a la vez. Las flechas y los cuadrados mueven, el centro mueve en la "
     "vista, los anillos giran, la bola de cada eje estira a lo largo de él y las cuatro pestañas "
     "exteriores escalan por igual. Mayús ajusta. Mayús+W otra vez: vuelve a Dibujar."},
    {"Gizmo, Rotate: drag a ring to turn about that axis, the outer ring to turn in the view. Mask the body and put "
     "the pivot at a joint to pose a limb. Shift snaps to 15 degrees. R again: back to Draw.",
     "Gizmo, Rotar: arrastra un anillo para girar sobre ese eje, el anillo exterior para girar en la vista. "
     "Enmascara el cuerpo y pon el pivote en una articulación para posar una extremidad. Mayús ajusta a 15 grados. R "
     "otra vez: vuelve a Dibujar."},
    {"Put the gizmo back at the centre of what moves (the Part, or its unmasked part) with world axes.",
     "Devuelve el gizmo al centro de lo que se mueve (la Pieza, o su parte sin máscara) con los ejes del mundo."},
    {"Put the pivot on the surface: click this, then click the model (or Alt+click the model at any time). With "
     "Surface orientation the axes follow the surface there (Y along the normal).",
     "Pon el pivote en la superficie: haz clic aquí y luego en el modelo (o Alt+clic en el modelo en cualquier "
     "momento). Con orientación Superficie, los ejes siguen la superficie en ese punto (Y en la normal)."},
    {"The gizmo's axes are the world's X, Y and Z.", "Los ejes del gizmo son X, Y y Z del mundo."},
    {"The gizmo's axes follow the surface where the pivot was snapped (Y along the normal) and turn with your "
     "rotations, for moving a part along itself.",
     "Los ejes del gizmo siguen la superficie donde se ajustó el pivote (Y en la normal) y giran con tus rotaciones, "
     "para mover una parte a lo largo de sí misma."},
    {"Masked parts stay where they are and partly masked parts move partly (on by default). Off: the whole Part moves "
     "whatever the mask.",
     "Lo enmascarado se queda donde está y lo parcialmente enmascarado se mueve en parte (activado por defecto). "
     "Desactivado: toda la Pieza se mueve sin importar la máscara."},
    {"Folder", "Carpeta"},
    {"Hide its Parts", "Ocultar sus Piezas"},
    {"Show its Parts", "Mostrar sus Piezas"},
    {"Move with the gizmo", "Mover con el gizmo"},
    {"New folder", "Carpeta nueva"},
    {"New folder with the active Part (drag Parts onto a folder to add them)", "Carpeta nueva con la Pieza activa (arrastra Piezas a una carpeta para añadirlas)"},
    {"Out of the folder", "Fuera de la carpeta"},
    {"Remove folder (keep its Parts)", "Quitar la carpeta (conservar sus Piezas)"},
    {"Click to open, double-click to rename, drag Parts onto it", "Clic para abrir, doble clic para renombrar, arrastra Piezas aquí"},
    {"Click to fold, double-click to rename, drag Parts onto it", "Clic para plegar, doble clic para renombrar, arrastra Piezas aquí"},
    {"Transform the visible Parts in the active Part's folder together (Parts list folders). Each Part gets its own undo step.", "Transforma juntas las Piezas visibles de la carpeta de la Pieza activa (carpetas de la lista de Piezas). Cada Pieza tiene su propio paso de deshacer."},
    {"Transform every visible Part together. Each Part gets its own undo step.",
     "Transforma juntas todas las Piezas visibles. Cada Pieza tiene su propio paso de deshacer."},
    {"Where the gizmo sits: rotation and scaling turn about this point. It follows the centre of what moves until you "
     "place it: Alt+drag the gizmo, Alt+click the model, To Surface, or type here.",
     "Dónde está el gizmo: la rotación y el escalado giran alrededor de este punto. Sigue el centro de lo que se "
     "mueve hasta que lo colocas: Alt+arrastra el gizmo, Alt+clic en el modelo, A la superficie, o escríbelo aquí."},
    {"A soft joint for posing: the movement eases in over this distance from the masked part (a share of the Part's "
     "size), so a hard mask edge bends smoothly instead of creasing. 0: the mask as it is.",
     "Una articulación suave para posar: el movimiento entra poco a poco en esta distancia desde lo enmascarado (una "
     "fracción del tamaño de la Pieza), para que un borde de máscara duro se doble suave en vez de hacer pliegue. 0: "
     "la máscara tal cual."},
    {"Distance to move along X, Y and Z (the gizmo's axes) when you press Apply.",
     "Distancia a mover en X, Y y Z (los ejes del gizmo) al pulsar Aplicar."},
    {"Degrees to turn about X, then Y, then Z (the gizmo's axes, through the pivot) when you press Apply.",
     "Grados a girar sobre X, luego Y, luego Z (los ejes del gizmo, por el pivote) al pulsar Aplicar."},
    {"Size to scale to about the pivot when you press Apply (100% = unchanged).",
     "Tamaño al que escalar alrededor del pivote al pulsar Aplicar (100% = sin cambio)."},
    {"Move, rotate and scale by the numbers above, about the pivot, as one undo step (the transform command). Masked "
     "parts stay; with symmetry on, a masked transform is mirrored.",
     "Mueve, rota y escala según los números de arriba, alrededor del pivote, en un solo paso de deshacer (el "
     "comando transform). Lo enmascarado se queda; con simetría, una transformación con máscara se refleja."},
    {"Set Move and Rotate back to 0 and Scale to 100%.", "Devuelve Mover y Rotar a 0 y Escalar a 100%."},
    // --- Input.cpp (tools/i18n/coverage.py) ---
    {"An Intensity slider under the cursor: drag it, or type a number (1-100) and press Enter. Hold Shift (or pick "
     "Smooth) to set Smooth's own Intensity.",
     "Un control de Intensidad bajo el cursor: arrástralo, o escribe un número (1-100) y pulsa Intro. Mantén Mayús "
     "(o elige Smooth) para ajustar la Intensidad propia de Smooth."},
    {"An Colour intensity slider under the cursor: how strongly painting colours the surface (1-100).",
     "Un control de Intensidad RGB bajo el cursor: cuánto colorea la superficie la pintura (1-100)."},
    {"A Falloff slider under the cursor: below 0 a broad, hard brush tip, above 0 a soft point (-100..100). The inner "
     "ring of the brush cursor shows it.",
     "Un control de Atenuación bajo el cursor: bajo 0 una punta de pincel ancha y dura, sobre 0 una punta suave "
     "(-100..100). Lo muestra el anillo interior del cursor del pincel."},
    {"The Quick Menu under the cursor: brush, stroke, alpha, material, colour, Size, Falloff and the intensities. "
     "Hold Space and let go to close it, or tap Space to keep it open.",
     "El Menú rápido bajo el cursor: pincel, trazo, alpha, material, color, Tamaño, Atenuación y las intensidades. "
     "Mantén Espacio y suelta para cerrarlo, o toca Espacio para dejarlo abierto."},
    {"Steady Stroke", "Trazo estable"},
    {"Steady Stroke on or off, with every stroke type (Dots, Drag Stamp, Slide Stamp, Spray): the brush trails the "
     "cursor on a string and only moves once the string is taut, for steady lines without hand shake.",
     "Trazo estable activado o no, con cada tipo de trazo (Dots, Drag Stamp, Slide Stamp, Spray): el pincel sigue al "
     "cursor con una cuerda y solo se mueve cuando la cuerda se tensa, para líneas firmes sin temblor de la mano."},
    {"Catch up on release", "Alcanzar al soltar"},
    {"Steady Stroke: when you let go, the line runs on to where the cursor is, instead of stopping a string's length "
     "behind it.",
     "Trazo estable: al soltar, la línea sigue hasta donde está el cursor, en vez de quedarse a una cuerda de "
     "distancia."},
    {"Keep the brush size in model units (mm) instead of screen pixels: it stays the same on the surface as you zoom "
     "in and out, for pores, wrinkles and trims of an exact width.",
     "Mantén el tamaño del pincel en unidades del modelo (mm) en vez de píxeles de pantalla: no cambia sobre la "
     "superficie al acercar o alejar, para poros, arrugas y remates de un ancho exacto."},
    {"Make the brush one pixel smaller (half a pixel below 2 px), down to 0.5 px.",
     "Reduce el pincel un píxel (medio píxel por debajo de 2 px), hasta 0,5 px."},
    {"Make the brush one pixel larger (half a pixel below 2 px).",
     "Agranda el pincel un píxel (medio píxel por debajo de 2 px)."},
    {"Continuous on or off: while you hold the brush still on the model it keeps adding, 30 dabs a second (build up a "
     "bump without moving). Not for Move, Tendril or the drag strokes.",
     "Continuo activado o no: mientras mantienes el pincel quieto sobre el modelo sigue añadiendo, 30 toques por "
     "segundo (crea un bulto sin moverte). No para Move, Tendril ni los trazos de arrastre."},
    {"Swap the main and the secondary colour.", "Intercambia el color principal y el secundario."},
    {"Mask the whole visible Part.", "Enmascara toda la Pieza visible."},
    {"Unmask everything (also: Ctrl+drag on the background, away from the model).",
     "Quita toda la máscara (también: Ctrl+arrastrar en el fondo, lejos del modelo)."},
    {"Mask what is unmasked and unmask what is masked (also: Ctrl+click the background).",
     "Enmascara lo que no tiene máscara y quita la máscara de lo que la tiene (también: Ctrl+clic en el fondo)."},
    {"Turn the masked part into a new face group and clear the mask. With nothing masked, everything visible becomes "
     "one group.",
     "Convierte lo enmascarado en un grupo de caras nuevo y borra la máscara. Sin nada enmascarado, todo lo visible "
     "pasa a ser un grupo."},
    {"One face group per UV island: triangles that touch without a UV seam share a group. Only the visible part is "
     "regrouped. Needs UVs (Tool > Texture Image > Auto UV makes some).",
     "Un grupo de caras por isla UV: los triángulos que se tocan sin costura UV comparten grupo. Solo se reagrupa la "
     "parte visible. Necesita UV (Herramienta > Imagen de textura > UV automáticas las crea)."},
    {"The classic sculpting keys and gestures: I is Colour intensity, 9 saves a snapshot, the number keys don't pick "
     "brushes, and Ctrl+drag on the background only clears the mask.",
     "Las teclas y gestos clásicos de escultura: I es Intensidad RGB, 9 guarda una instantánea, las teclas numéricas "
     "no eligen pinceles, y Ctrl+arrastrar en el fondo solo borra la máscara."},
    {"SculptForge's own keys (the default): 1-9 and 0 pick the first ten brushes, I is Falloff (as is O), U "
     "Intensity, and Ctrl+drag on the background zooms when nothing is masked.",
     "Las teclas propias de SculptForge (por defecto): 1-9 y 0 eligen los diez primeros pinceles, I es Atenuación "
     "(igual que O), U Intensidad, y Ctrl+arrastrar en el fondo acerca la vista cuando no hay nada enmascarado."},
    {"Blender's navigation and sculpt-mode keys: middle-drag orbits (Shift pans, Ctrl zooms), Alt+left-drag does the "
     "same for pens and trackpads, Ctrl inverts a stroke, Shift smooths, F / Shift+F set radius and strength, V S P "
     "I G C K pick brushes and M the mask brush, Home frames everything. Rotation switches to Turntable.",
     "Las teclas de navegación y del modo escultura de Blender: arrastrar con el medio orbita (Mayús desplaza, Ctrl "
     "acerca), Alt+arrastrar con el izquierdo hace lo mismo para lápices y trackpads, Ctrl invierte un trazo, Mayús "
     "suaviza, F / Mayús+F fijan radio y fuerza, V S P I G C K eligen pinceles y M el pincel de máscara, Inicio "
     "encuadra todo. La rotación pasa a Plataforma giratoria."},
    {"Maya's view controls: Alt+left-drag orbits, Alt+middle-drag pans, Alt+right-drag zooms, anywhere on the canvas; "
     "hold Alt, then add Ctrl, and a left drag rolls the view. Alt+F frames the Part, Alt+H resets the view, the "
     "wheel zooms toward the pointer. Ctrl inverts a stroke, Ctrl+Alt masks (Ctrl+Alt+drag on the background clears "
     "the mask), Shift smooths, Ctrl+Shift hides and shows. The Classic sculpting keys stay for everything else.",
     "Los controles de vista de Maya: Alt+arrastrar con el izquierdo orbita, Alt+arrastrar con el medio desplaza, "
     "Alt+arrastrar con el derecho acerca, en cualquier parte del lienzo; mantén Alt, añade Ctrl, y arrastrar con el "
     "izquierdo rueda la vista. Alt+F encuadra la Pieza, Alt+H restablece la vista, la rueda acerca hacia el "
     "puntero. Ctrl invierte un trazo, Ctrl+Alt enmascara (Ctrl+Alt+arrastrar en el fondo borra la máscara), Mayús "
     "suaviza, Ctrl+Mayús oculta y muestra. Para todo lo demás quedan las Teclas clásicas de escultura."},
    {"Blender style: the number keys above the letters work as the numpad (1 front, 3 right, 7 top, 5 "
     "orthographic...), for keyboards without one.",
     "Estilo Blender: las teclas numéricas sobre las letras funcionan como el teclado numérico (1 frente, 3 derecha, "
     "7 arriba, 5 ortográfica...), para teclados sin él."},
    {"A right click on the canvas, without dragging, opens the Quick Menu under the cursor (Space does too). A right "
     "drag still turns, pans or zooms the view. A pen button set to Right click works the same.",
     "Un clic derecho en el lienzo, sin arrastrar, abre el Menú rápido bajo el cursor (Espacio también). Arrastrar "
     "con el derecho sigue girando, desplazando o acercando la vista. Un botón del lápiz configurado como Clic "
     "derecho funciona igual."},
    {"While Space is held, a drag on the canvas moves the camera: left-drag turns it, right- or middle-drag pans, and "
     "with Ctrl held too a drag zooms (#43). Off by default: with the Classic sculpting keys, Space opens the Quick "
     "Menu (with this on, a right click still does). Typing a space in a text field is never taken.",
     "Mientras mantienes Espacio, arrastrar en el lienzo mueve la cámara: con el izquierdo gira, con el derecho o el "
     "medio desplaza, y con Ctrl también pulsado acerca (#43). Desactivado por defecto: con las Teclas clásicas de "
     "escultura, Espacio abre el Menú rápido (con esto activado, el clic derecho lo sigue abriendo). Nunca se "
     "captura un espacio escrito en un campo de texto."},
    {"Emulate 3 Button Mouse", "Emular ratón de 3 botones"},
    {"Blender style: Alt+left-drag always turns the view (Shift pans, Ctrl zooms), as Blender's Emulate 3 Button "
     "Mouse preference does, for pens and trackpads. Off (as Blender's default keymap, 5.1 and later): Alt+left-drag "
     "on the model masks and Ctrl+Alt+left-drag unmasks; beside the model it still turns the view (#77).",
     "Estilo Blender: Alt+arrastrar con el izquierdo siempre gira la vista (Mayús desplaza, Ctrl acerca), como la "
     "preferencia Emulate 3 Button Mouse de Blender, para lápices y trackpads. Desactivado (como el mapa de teclas "
     "por defecto de Blender, 5.1 y posteriores): Alt+arrastrar con el izquierdo sobre el modelo enmascara y "
     "Ctrl+Alt+arrastrar con el izquierdo quita la máscara; junto al modelo sigue girando la vista (#77)."},
    {"A left-drag that starts on the background begins a brush stroke instead of turning the view; the brush touches "
     "the model once the cursor reaches it (#62). Ctrl (mask) and Shift (smooth) work the same way, and a Ctrl+click "
     "on the background still inverts the mask. The view still turns with a right-drag or a pen button set to Right "
     "click, pans with Alt+drag or a middle-drag, and moves with Space held if Hold Space is on; with Maya keys, "
     "Alt+left-drag orbits, with Blender keys beside the model. Shift+L (View > Lock camera) turns it on and off.",
     "Arrastrar con el izquierdo empezando en el fondo inicia un trazo en vez de girar la vista; el pincel toca el "
     "modelo cuando el cursor llega a él (#62). Ctrl (máscara) y Mayús (suavizar) funcionan igual, y Ctrl+clic en el "
     "fondo sigue invirtiendo la máscara. La vista sigue girando con el derecho o con un botón del lápiz configurado "
     "como Clic derecho, se desplaza con Alt+arrastrar o con el medio, y se mueve con Espacio si Mantener Espacio "
     "está activado; con las teclas de Maya, Alt+arrastrar con el izquierdo orbita, con las de Blender junto al "
     "modelo. Mayús+L (Vista > Bloquear cámara) lo activa y desactiva."},
    {"Lock camera", "Bloquear cámara"},
    {"Lock the camera for left-drags: a drag that starts on the background sculpts once it reaches the model instead "
     "of turning the view (Preferences > Input > Start strokes off the model, #62). The status bar shows CAMERA "
     "LOCKED while it is on. Right-drag or Alt+drag still move the view. Shift+L turns it on and off.",
     "Bloquea la cámara para arrastres con el izquierdo: un arrastre que empieza en el fondo esculpe al llegar al "
     "modelo en vez de girar la vista (Preferencias > Entrada > Empezar trazos fuera del modelo, #62). La barra de "
     "estado muestra CÁMARA BLOQUEADA mientras está activo. Arrastrar con el derecho o Alt+arrastrar siguen moviendo "
     "la vista. Mayús+L lo activa y desactiva."},
    {"The brush ring lies on the model under the pointer: a circle at the brush's real size there, turned to the form "
     "of the surface (averaged over the brush, so fine bumps don't make it wobble), with a short line along the "
     "surface's direction. With symmetry on, a dot shows where each mirrored or radial copy lands. Off the model, "
     "and with this off, the ring is a flat circle on the screen.",
     "El anillo del pincel se apoya en el modelo bajo el puntero: un círculo del tamaño real del pincel ahí, girado "
     "según la forma de la superficie (promediada sobre el pincel, para que los relieves finos no lo hagan temblar), "
     "con una línea corta en la dirección de la superficie. Con simetría, un punto muestra dónde cae cada copia "
     "reflejada o radial. Fuera del modelo, y con esto desactivado, el anillo es un círculo plano en la pantalla."},
    {"Cursor", "Cursor"},
    {"While a stroke runs, show only the dot where the brush touches (and the symmetry dots), not the ring: nothing "
     "between you and the surface you are shaping. Off by default: the ring shows the brush size throughout.",
     "Mientras dura un trazo, muestra solo el punto donde toca el pincel (y los puntos de simetría), no el anillo: "
     "nada entre tú y la superficie que modelas. Desactivado por defecto: el anillo muestra el tamaño del pincel "
     "todo el tiempo."},
    {"Use the keys from a keymap you exported in Blender (Preferences > Keymap > Export, a .py file). Switches to the "
     "Blender style, then takes the navigation and sculpt keys SculptForge has an equivalent for. The file is only "
     "read, never run. Saved automatically.",
     "Usa las teclas de un mapa de teclas que exportaste en Blender (Preferencias > Keymap > Export, un archivo "
     ".py). Cambia al estilo Blender y toma las teclas de navegación y escultura que tienen equivalente en "
     "SculptForge. El archivo solo se lee, nunca se ejecuta. Se guarda automáticamente."},
    {"Paint the mask with a plain left drag, Ctrl+drag to unmask (Blender's Mask brush). Press it again, or pick "
     "another brush, to go back.",
     "Pinta la máscara arrastrando con el izquierdo, Ctrl+arrastrar para quitarla (el pincel Mask de Blender). "
     "Púlsalo otra vez, o elige otro pincel, para volver."},
    {"Hide the face group under the cursor (Blender: hide the face set).",
     "Oculta el grupo de caras bajo el cursor (Blender: ocultar el face set)."},
    {"Show only the face group under the cursor; again, with it isolated, shows everything (Blender: toggle "
     "visibility).",
     "Muestra solo el grupo de caras bajo el cursor; otra vez, con él aislado, lo muestra todo (Blender: alternar "
     "visibilidad)."},
    {"Show every hidden part of the mesh.", "Muestra todas las partes ocultas de la malla."},
    {"Look at the model from the front (orthographic).", "Mira el modelo desde el frente (ortográfica)."},
    {"Look at the model from behind (orthographic).", "Mira el modelo desde atrás (ortográfica)."},
    {"Look at the model's right side (orthographic).", "Mira el lado derecho del modelo (ortográfica)."},
    {"Look at the model's left side (orthographic).", "Mira el lado izquierdo del modelo (ortográfica)."},
    {"Look down on the model (orthographic).", "Mira el modelo desde arriba (ortográfica)."},
    {"Look up at the model from below (orthographic).", "Mira el modelo desde abajo (ortográfica)."},
    {"Gizmo size: how big the X / Y / Z axes and the View Figure are in the corner of the canvas. Grows with the UI "
     "scale.",
     "Tamaño del gizmo: lo grandes que son los ejes X / Y / Z y la Figura de vista en la esquina del lienzo. Crece "
     "con la escala de la interfaz."},
    {"Switch between a perspective and an orthographic view (no foreshortening: sizes don't shrink with depth).",
     "Alterna entre vista en perspectiva y ortográfica (sin escorzo: los tamaños no se reducen con la profundidad)."},
    {"Turn the view 15 degrees to the left around the model.",
     "Gira la vista 15 grados a la izquierda alrededor del modelo."},
    {"Turn the view 15 degrees to the right around the model.",
     "Gira la vista 15 grados a la derecha alrededor del modelo."},
    {"Turn the view 15 degrees up over the model.", "Gira la vista 15 grados hacia arriba sobre el modelo."},
    {"Turn the view 15 degrees down under the model.", "Gira la vista 15 grados hacia abajo bajo el modelo."},
    {"Look at the model from the other side.", "Mira el modelo desde el otro lado."},
    {"Move the view closer.", "Acerca la vista."},
    {"Move the view back.", "Aleja la vista."},
    {"Fit the active Part in the view (Blender: View Selected; Maya keys: Alt+F).",
     "Encuadra la Pieza activa en la vista (Blender: View Selected; teclas de Maya: Alt+F)."},
    {"Back to the view a new model opens with: from the front, in perspective, the whole model in view (Maya keys: "
     "Alt+H).",
     "Vuelve a la vista con la que se abre un modelo nuevo: de frente, en perspectiva, con todo el modelo a la vista "
     "(teclas de Maya: Alt+H)."},
    {"Zoom drag", "Arrastre de zoom"},
    {"Sideways (Maya)", "Lateral (Maya)"},
    {"Up and down", "Arriba y abajo"},
    {"Alt+right drag zooms. Sideways (the default, as Maya's own dolly): drag right to zoom in, left to zoom out. Up "
     "and down: drag up to zoom in, as the SculptForge style.",
     "Alt+arrastrar con el derecho acerca. Lateral (por defecto, como el dolly de Maya): arrastra a la derecha para "
     "acercar, a la izquierda para alejar. Arriba y abajo: arrastra hacia arriba para acercar, como el estilo "
     "SculptForge."},
    {"Invert zoom", "Invertir zoom"},
    {"Turn zoom drags around (the mouse wheel stays as it is).",
     "Invierte los arrastres de zoom (la rueda del ratón no cambia)."},
    {"The mouse wheel zooms toward the point of the model under the pointer, which stays under it, instead of the "
     "middle of the view. On with Maya keys (choosing a style sets it), off with the others.",
     "La rueda del ratón acerca hacia el punto del modelo bajo el puntero, que se queda bajo él, en vez del centro "
     "de la vista. Activado con las teclas de Maya (al elegir un estilo se ajusta), desactivado con los demás."},
    {"Along the normal", "En la normal"},
    {"Shift", "Mayús"},
    {"Ctrl", "Ctrl"},
    {"Alt", "Alt"},
    {"Slide Stamp rotates with", "Slide Stamp gira con"},
    {"How dabs follow the cursor: Dots, Drag Stamp, Slide Stamp, Spray or Lazy Stroke. Click to choose. In Slide Stamp, hold Shift while dragging to turn the alpha toward the cursor (Preferences > Input).", "Cómo siguen los toques al cursor: Dots, Drag Stamp, Slide Stamp, Spray o Lazy Stroke. Clic para elegir. En Slide Stamp, mantén Mayús mientras arrastras para girar el alpha hacia el cursor (Preferencias > Entrada)."},
    {"With the Slide Stamp stroke, hold this key after the stroke has begun and the stamp stays where it is while its alpha turns toward the cursor; let go and it slides on with that turn. Held at the press it keeps its own meaning (Shift smooths, Ctrl masks, Alt inverts). The turn belongs to that stroke: the brush's Angle is not changed. Off: the key does nothing here.", "Con el trazo Slide Stamp, mantén esta tecla después de empezar el trazo y el sello se queda donde está mientras su alpha gira hacia el cursor; al soltarla sigue deslizándose con ese giro. Si está pulsada al empezar, conserva su función (Mayús suaviza, Ctrl enmascara, Alt invierte). El giro es de ese trazo: el Ángulo del pincel no cambia. Off: la tecla no hace nada aquí."},
    {"Always", "Siempre"},
    {"Never", "Nunca"},
    {"Move, Tendril, Elastic Tendril and KelvinGrab: with the invert key held as the stroke begins (Alt; Ctrl in the "
     "Blender and Maya styles), the surface moves along the average normal under the brush instead of across the "
     "screen, so it pulls straight out of the surface or pushes straight in. Always: every stroke of those brushes "
     "does; Never: the key does nothing for them.",
     "Move, Tendril, Elastic Tendril y KelvinGrab: con la tecla de invertir pulsada al empezar el trazo (Alt; Ctrl "
     "en los estilos Blender y Maya), la superficie se mueve según la normal media bajo el pincel en vez de a lo "
     "largo de la pantalla, así sale recta de la superficie o entra recta. Siempre: lo hace cada trazo de esos "
     "pinceles; Nunca: la tecla no hace nada en ellos."},
    {"Share size and intensity across brushes", "Compartir tamaño e intensidad entre pinceles"},
    {"Free: the model turns about the screen's axes and can roll over the top. Turntable: it stays upright, as in "
     "Blender or Maya. X axis, Z axis: it turns about that world axis only (View > Orbit lock).",
     "Libre: el modelo gira sobre los ejes de la pantalla y puede darse la vuelta por arriba. Plataforma giratoria: "
     "se queda derecho, como en Blender o Maya. Eje X, eje Z: gira solo sobre ese eje del mundo (Vista > Bloqueo de órbita)."},
    {"The pointer over the 3D view: a crosshair (the default, as in most sculpting apps; it stays while the view "
     "turns) or the arrow. Panels, menus and popups always show the arrow (or their own resize and text cursors). "
     "Saved automatically.",
     "El puntero sobre la vista 3D: una cruz (por defecto, como en la mayoría de apps de escultura; se mantiene "
     "mientras gira la vista) o la flecha. Los paneles, menús y ventanas emergentes siempre muestran la flecha (o "
     "sus propios cursores de redimensionar y de texto). Se guarda automáticamente."},
    {"Brush radius in screen pixels (0.5 to 800), and in mm on the surface.",
     "Radio del pincel en píxeles de pantalla (0,5 a 800), y en mm sobre la superficie."},
    {"Below 0: a broad, hard tip. Above 0: a soft point.", "Bajo 0: una punta ancha y dura. Sobre 0: una punta suave."},
    {"How strongly the brush changes the surface.", "Cuánto cambia el pincel la superficie."},
    {"How strongly painting colours the surface.", "Cuánto colorea la superficie la pintura."},
    // --- LibraryBrowser.cpp (tools/i18n/coverage.py) ---
    {"Library tabs", "Pestañas de Biblioteca"},
    {"What the Library shows: projects, models, brushes, alphas, materials, images, splats, snapshots, recent files "
     "and packs.",
     "Lo que muestra la Biblioteca: proyectos, modelos, pinceles, alphas, materiales, imágenes, splats, "
     "instantáneas, archivos recientes y paquetes."},
    {"Hide the Library (comma toggles it; Esc closes it).",
     "Oculta la Biblioteca (la coma la alterna; Esc la cierra)."},
    {"Open the Library on the Project tab when SculptForge starts.",
     "Abre la Biblioteca en la pestaña Proyecto al iniciar SculptForge."},
    {"Rows of thumbnails in the Library strip.", "Filas de miniaturas en la tira de la Biblioteca."},
    {"Show the Library along the top or the bottom of the canvas.", "Muestra la Biblioteca arriba o abajo del lienzo."},
    {"Show or hide the Library: thumbnails of your projects, models, brushes, alphas, materials, images, splats and "
     "Snapshots along the top of the canvas. Double-click to load.",
     "Muestra u oculta la Biblioteca: miniaturas de tus proyectos, modelos, pinceles, alphas, materiales, imágenes, "
     "splats e Instantáneas a lo largo del borde superior del lienzo. Doble clic para cargar."},
    {"Pop up every brush under the cursor. Type the first letter of a brush, then the orange letter on its tile (B S "
     "T = Standard).",
     "Muestra todos los pinceles bajo el cursor. Escribe la primera letra de un pincel y luego la letra naranja de "
     "su mosaico (B S T = Standard)."},
    {"Pop up the stroke types: Dots, Drag Stamp, Slide Stamp, Spray and Lazy Stroke.",
     "Muestra los tipos de trazo: Dots, Drag Stamp, Slide Stamp, Spray y Lazy Stroke."},
    {"Pop up the brush alphas: built-in stamps and images from your library.",
     "Muestra los alphas de pincel: sellos incluidos e imágenes de tu biblioteca."},
    {"Pop up the materials under the cursor. Hover one to preview it on the model; click to keep it.",
     "Muestra los materiales bajo el cursor. Pasa sobre uno para verlo en el modelo; haz clic para quedártelo."},
    {"A Size slider under the cursor: drag it, or type a number and press Enter.",
     "Un control de Tamaño bajo el cursor: arrástralo, o escribe un número y pulsa Intro."},
    {"Save a timestamped snapshot of the whole scene to your library (the last 10 are kept). Your project file is not "
     "changed. Find them in the Library's Snapshots tab.",
     "Guarda en tu biblioteca una instantánea con fecha y hora de toda la escena (se conservan las 10 últimas). Tu "
     "archivo de proyecto no cambia. Están en la pestaña Instantáneas de la Biblioteca."},
    {"Save Render", "Guardar render"},
    {"Render the view (without the interface) to an image in your library: Library > Image lists it, and the status "
     "bar names the file. Material sets its size (twice the view by default), shape (the view's, 1:1, 16:9, 9:16, "
     "4:3) and format (PNG or JPG).",
     "Renderiza la vista (sin la interfaz) a una imagen en tu biblioteca: Biblioteca > Imagen la lista y la barra de "
     "estado nombra el archivo. Material fija su tamaño (el doble de la vista por defecto), su forma (la de la "
     "vista, 1:1, 16:9, 9:16, 4:3) y su formato (PNG o JPG)."},
    // --- MemoryConfirm.cpp (tools/i18n/coverage.py) ---
    {"Not enough free memory", "No hay suficiente memoria libre"},
    {"If the computer runs out of memory it can stop responding and SculptForge may close without saving. Close other "
     "apps (or save first) before you go on.",
     "Si el equipo se queda sin memoria puede dejar de responder y SculptForge podría cerrarse sin guardar. Cierra "
     "otras apps (o guarda primero) antes de seguir."},
    {"Continue anyway", "Continuar de todos modos"},
    // --- Palettes.cpp (tools/i18n/coverage.py) ---
    {"Brushes subtract instead of add. Holding Alt flips it for one stroke.",
     "Los pinceles restan en vez de sumar. Mantener Alt lo invierte durante un trazo."},
    {"Connected only", "Solo conectado"},
    {"View falloff", "Ver atenuación"},
    {"The Smooth brush (and Shift) removes bumps without shrinking the form, instead of the plain smooth that also "
     "flattens it. Either way, let go of Shift while still pressing a Shift-smooth stroke to switch to the other "
     "smooth for the rest of the stroke.",
     "El pincel Smooth (y Mayús) quita bultos sin encoger la forma, en vez del suavizado simple que también la "
     "aplana. En ambos casos, suelta Mayús mientras sigues pulsando un trazo de suavizado para cambiar al otro "
     "suavizado el resto del trazo."},
    {"The Smooth brush (and Shift) straightens the borders between face groups and leaves everything else alone: a "
     "jagged clothing edge, a cuff or a panel line becomes a clean line along the surface. Corners where three "
     "groups meet stay where they are.",
     "El pincel Smooth (y Mayús) endereza los bordes entre grupos de caras y deja todo lo demás igual: un borde de "
     "ropa dentado, un puño o una línea de panel se vuelve una línea limpia sobre la superficie. Las esquinas donde "
     "se juntan tres grupos se quedan donde están."},
    {"Brush radius in screen pixels (0.5 to 800), and in mm on the surface under the cursor. [ and ] change it by "
     "10%, Shift+[ and Shift+] by 1 px; Ctrl+Shift+right-drag scrubs it finely.",
     "Radio del pincel en píxeles de pantalla (0,5 a 800), y en mm sobre la superficie bajo el cursor. [ y ] lo "
     "cambian un 10%, Mayús+[ y Mayús+] 1 px; Ctrl+Mayús+arrastrar con el derecho lo ajusta fino."},
    {"The brush falloff: below 0 a broad, hard tip, above 0 a soft point. The brush cursor's inner ring shows where "
     "the brush works at half strength.",
     "La atenuación del pincel: bajo 0 una punta ancha y dura, sobre 0 una punta suave. El anillo interior del "
     "cursor muestra dónde trabaja el pincel a media fuerza."},
    {"Off (the default): the triangle size follows the brush size on screen, as in Dynamic Topology. On: one size for "
     "the whole mesh, set by Detail. A preference (saved automatically).",
     "Desactivado (por defecto): el tamaño de los triángulos sigue al tamaño del pincel en pantalla, como en "
     "Topología dinámica. Activado: un solo tamaño para toda la malla, fijado por Detalle. Una preferencia (se "
     "guarda automáticamente)."},
    {"Smooth remesh", "Remallado al suavizar"},
    {"How much Smooth (Shift) changes the topology while Dynamic is on. 100 (the default): it also simplifies, "
     "merging triangles toward twice the size. Lower: it remeshes less. 0: Smooth only smooths and the triangles "
     "stay as they are. A preference (saved automatically).",
     "Cuánto cambia la topología Smooth (Mayús) mientras Dinámica está activada. 100 (por defecto): también "
     "simplifica, uniendo triángulos hacia el doble de tamaño. Menos: remalla menos. 0: Smooth solo suaviza y los "
     "triángulos no cambian. Una preferencia (se guarda automáticamente)."},
    {"The viewport's lens, as on a full-frame camera: short (15-35 mm) exaggerates perspective, long (85-300 mm) "
     "flattens it, which helps judge proportions. Saved with the project; Ctrl+click to type.",
     "El objetivo de la vista, como en una cámara de fotograma completo: corto (15-35 mm) exagera la perspectiva, "
     "largo (85-300 mm) la aplana, lo que ayuda a juzgar las proporciones. Se guarda con el proyecto; Ctrl+clic para "
     "escribir."},
    {"Show the X / Y / Z directions in the corner of the canvas.",
     "Muestra las direcciones X / Y / Z en la esquina del lienzo."},
    {"View Figure", "Figura de vista"},
    {"Show a small shaded head in the corner of the canvas that turns with the view. Click it to look from the "
     "nearest side (front, back, left, right, top or bottom).",
     "Muestra una pequeña cabeza sombreada en la esquina del lienzo que gira con la vista. Haz clic en ella para "
     "mirar desde el lado más cercano (frente, atrás, izquierda, derecha, arriba o abajo)."},
    {"Gizmo size", "Tamaño del gizmo"},
    {"AI features are off (Preferences > AI): Relief, Inflate and Stamp work offline.",
     "Las funciones de IA están desactivadas (Preferencias > IA): Relieve, Inflar y Estampar funcionan sin conexión."},
    {"A sketch, concept, logo, photo or height map (or drop one on the window).",
     "Un boceto, concepto, logo, foto o mapa de alturas (o suelta uno en la ventana)."},
    {"Light areas rise: logos, coins, emblems, height maps.",
     "Las zonas claras se elevan: logos, monedas, emblemas, mapas de alturas."},
    {"Inflates the outline into a rounded blockout.", "Infla el contorno en un bloqueo redondeado."},
    {"Projects the image onto the model from this view as surface detail.",
     "Proyecta la imagen sobre el modelo desde esta vista como detalle de superficie."},
    {"Integrations", "Integraciones"},
    {"Support", "Apoyo"},
    {"X-Ray", "Rayos X"},
    {"View resolution", "Resolución de vista"},
    {"Decimate leaves masked vertices where they are, so masked areas keep their triangles exactly; the rest is "
     "decimated around them.",
     "Diezmar deja los vértices enmascarados donde están, así las zonas enmascaradas conservan sus triángulos "
     "exactos; el resto se diezma a su alrededor."},
    {"Remeshing in the background (with a painted texture: Auto UV of the new surface, then the texture projected "
     "onto it). The result replaces the Part when it's done.",
     "Remallando en segundo plano (con una textura pintada: UV automáticas de la superficie nueva y luego la textura "
     "proyectada en ella). El resultado reemplaza la Pieza cuando termina."},
    {"Tool > Remesh", "Herramienta > Remallado"},
    {"Cut", "Cortar"},
    {"Selection Sets", "Conjuntos de selección"},
    {"Floor grid", "Cuadrícula del suelo"},
    {"A faint grid on the floor the model stands on (its lowest point, Y up), fading into the distance: a "
     "reference for perspective and lining up Parts; the red and blue lines are the X and Z axes. It moves when "
     "a stroke or a move ends. Off by default; saved automatically.",
     "Una cuadrícula tenue en el suelo sobre el que se apoya el modelo (su punto más bajo, Y hacia arriba), que "
     "se desvanece a lo lejos: una referencia para la perspectiva y para alinear Piezas; las líneas roja y azul "
     "son los ejes X y Z. Se mueve al terminar un trazo o un movimiento. Desactivada por defecto; se guarda "
     "automáticamente."},
    {"Backface culling", "Ocultar caras traseras"},
    {"Don't draw the back faces of the model: faster on dense meshes, and you see through the openings of open meshes "
     "(a scan, a garment) instead of their inside. Brushes then work only on faces you can see. Off by default; "
     "saved automatically, projects keep their own.",
     "No dibuja las caras traseras del modelo: más rápido en mallas densas, y ves a través de las aberturas de "
     "mallas abiertas (un escaneo, una prenda) en vez de su interior. Los pinceles solo actúan entonces sobre las "
     "caras visibles. Desactivado por defecto; se guarda automáticamente, los proyectos guardan el suyo."},
    {"Radial Copies", "Copias radiales"},
    {"How many copies radial symmetry makes of every stroke, turned evenly around the axis (2 to 64). With X, Y or Z "
     "on too, each mirror image gets its copies.",
     "Cuántas copias de cada trazo hace la simetría radial, repartidas por igual alrededor del eje (2 a 64). Con X, "
     "Y o Z también activados, cada imagen reflejada recibe sus copias."},
    // --- PanelAi.cpp (tools/i18n/coverage.py) ---
    {"Enable AI features", "Activar funciones de IA"},
    {"Brush Lab: describe a brush in words and AI builds it; tweak the recipe with sliders.",
     "Brush Lab: describe un pincel con palabras y la IA lo crea; ajusta la receta con controles deslizantes."},
    {"Turn an image or a description into a sculptable model (Hugging Face, fal.ai, Meshy, ComfyUI or offline).",
     "Convierte una imagen o una descripción en un modelo esculpible (Hugging Face, fal.ai, Meshy, ComfyUI o sin "
     "conexión)."},
    // --- PanelBake.cpp (tools/i18n/coverage.py) ---
    {"Bake the chosen maps from the source Part(s), or this Part's own top subdivision or micro level, onto its UVs, "
     "in the background. Maps are saved as PNGs (displacement also as a 32-bit EXR, position as a 16-bit PNG) in the "
     "output folder, one set per UDIM tile when the UVs use them, and the normal map shows on the model.",
     "Hornea los mapas elegidos desde la(s) Pieza(s) de origen, o desde la subdivisión más alta o el nivel micro de "
     "esta Pieza, sobre sus UV, en segundo plano. Los mapas se guardan como PNG (el desplazamiento también como EXR "
     "de 32 bits, la posición como PNG de 16 bits) en la carpeta de salida, un juego por mosaico UDIM cuando las UV "
     "los usan, y el mapa de normales se ve en el modelo."},
    {"Stop the bake that is running. Nothing is written.", "Detiene el horneado en curso. No se escribe nada."},
    {"Show the baked tangent-space normal map on the low-poly in the viewport (decoded with the same MikkTSpace "
     "tangents a game engine uses), to check it against the high-poly. Turn it off to see the bare low-poly.",
     "Muestra en la vista el mapa de normales en espacio tangente horneado sobre el low-poly (decodificado con las "
     "mismas tangentes MikkTSpace que usa un motor de juego), para compararlo con el high-poly. Desactívalo para ver "
     "el low-poly sin más."},
    {"Open the folder the last bake wrote its maps to.",
     "Abre la carpeta donde el último horneado escribió sus mapas."},
    {"Choose the folder baked maps are written to. Empty: beside your last export, else the project's folder, else "
     "Bakes in your library folder.",
     "Elige la carpeta donde se escriben los mapas horneados. Vacío: junto a tu última exportación, si no la carpeta "
     "del proyecto, si no Bakes en la carpeta de tu biblioteca."},
    {"Maps are baked onto the active Part's UVs: make the low-poly active (it needs UVs from import or from Auto UV "
     "in Tool > Texture Image), and keep the high-poly Part(s) visible.",
     "Los mapas se hornean sobre las UV de la Pieza activa: activa el low-poly (necesita UV de la importación o de "
     "UV automáticas en Herramienta > Imagen de textura) y deja visibles las Piezas high-poly."},
    {"The high-poly to bake from: one Part, or every visible Part except the active one (the low-poly), or this "
     "Part's own top subdivision level baked onto its lowest level, or its micro level baked onto its base. Hidden "
     "faces of the source are ignored. UVs outside 0..1 bake as UDIM tiles (name.1001.png, ...).",
     "El high-poly del que hornear: una Pieza, o todas las Piezas visibles excepto la activa (el low-poly), o la "
     "subdivisión más alta de esta Pieza horneada en su nivel más bajo, o su nivel micro horneado en su base. Las "
     "caras ocultas del origen se ignoran. Las UV fuera de 0..1 se hornean como mosaicos UDIM (nombre.1001.png, ...)."},
    {"Width and height of the maps: 1K, 2K (the default), 4K or 8K. Bake time and memory grow with the area (an 8K "
     "bake holds about 200 MB per map while it runs).",
     "Ancho y alto de los mapas: 1K, 2K (por defecto), 4K u 8K. El tiempo y la memoria del horneado crecen con el "
     "área (un horneado 8K ocupa unos 200 MB por mapa mientras dura)."},
    {"AA %.0fx", "AA %.0fx"},
    {"Supersampling: n x n rays per texel, averaged. 1 is fastest; 2 (the default) gives clean edges; 4 costs 16 rays "
     "per texel.",
     "Supermuestreo: n x n rayos por téxel, promediados. 1 es lo más rápido; 2 (por defecto) da bordes limpios; 4 "
     "cuesta 16 rayos por téxel."},
    {"Measure how far the high-poly lies in front of and behind the low-poly before baking, and cast rays just that "
     "far. Turn it off to set Front and Rear yourself (e.g. when rays hit a nearby part).",
     "Mide cuánto queda el high-poly por delante y por detrás del low-poly antes de hornear, y lanza los rayos justo "
     "esa distancia. Desactívalo para fijar Frente y Atrás tú mismo (p. ej. cuando los rayos tocan una parte "
     "cercana)."},
    {"How far in front of the low-poly rays start, as a percentage of the model's size (its bounding-box diagonal). "
     "Raise it when details that stick out come out flat or missing.",
     "Cuánto por delante del low-poly empiezan los rayos, como porcentaje del tamaño del modelo (la diagonal de su "
     "caja envolvente). Súbelo cuando los detalles que sobresalen salen planos o faltan."},
    {"How far behind the low-poly rays look, as a percentage of the model's size. Raise it when carved detail is "
     "missing; lower it when rays reach the far side of thin parts.",
     "Cuánto por detrás del low-poly miran los rayos, como porcentaje del tamaño del modelo. Súbelo cuando falta "
     "detalle tallado; bájalo cuando los rayos llegan al otro lado de partes finas."},
    {"Texels filled around each UV island with the island's edge colours, so seams don't show once the maps are "
     "mipmapped in an engine. 8 is plenty for 2K; use more for 4K and 8K.",
     "Téxeles rellenados alrededor de cada isla UV con los colores de su borde, para que las costuras no se vean "
     "cuando un motor hace mipmaps de los mapas. 8 basta para 2K; usa más para 4K y 8K."},
    {"OpenGL (green up) suits Blender, Unity, Godot and Substance. DirectX (green down) is Unreal's native "
     "convention; Send to Unreal flips an OpenGL map on import, so either works there.",
     "OpenGL (verde arriba) va bien con Blender, Unity, Godot y Substance. DirectX (verde abajo) es la convención "
     "nativa de Unreal; Enviar a Unreal voltea un mapa OpenGL al importarlo, así que ahí sirven los dos."},
    {"Rays per texel for ambient occlusion: more is smoother and slower.",
     "Rayos por téxel para la oclusión ambiental: más es más suave y más lento."},
    {"How far away geometry still occludes, as a percentage of the model's size. Short: only creases darken. Long: "
     "whole cavities and the insides of shapes darken too.",
     "Hasta qué distancia la geometría sigue ocluyendo, como porcentaje del tamaño del modelo. Corta: solo se "
     "oscurecen los pliegues. Larga: también se oscurecen cavidades enteras y el interior de las formas."},
    {"Where the maps are written, as <Part>_normal.png, _ao.png, _displacement.png (+ .exr) and so on. Empty: beside "
     "your last export, else the project's folder, else Bakes in your library folder.",
     "Dónde se escriben los mapas, como <Part>_normal.png, _ao.png, _displacement.png (+ .exr), etc. Vacío: junto a "
     "tu última exportación, si no la carpeta del proyecto, si no Bakes en la carpeta de tu biblioteca."},
    {"Also write the displacement as a 32-bit float OpenEXR in world units (for renderers that take real distances). "
     "The 16-bit PNG is always written.",
     "Escribe también el desplazamiento como OpenEXR float de 32 bits en unidades del mundo (para renderizadores que "
     "usan distancias reales). El PNG de 16 bits se escribe siempre."},
    {"Baking on every core in the background; you can keep working.",
     "Horneando con todos los núcleos en segundo plano; puedes seguir trabajando."},
    // --- PanelBlender.cpp (tools/i18n/coverage.py) ---
    {"Load what Blender sends as soon as it arrives (checked once a second). Off: use Receive from Blender.",
     "Carga lo que envía Blender en cuanto llega (se comprueba una vez por segundo). Desactivado: usa Recibir de "
     "Blender."},
    // --- PanelBrushes.cpp (tools/i18n/coverage.py) ---
    {"Follow stroke direction", "Seguir la dirección del trazo"},
    {"The alpha turns with the stroke: its top points the way you are drawing, for scales, stitches and hair that "
     "line up along the stroke. Small wobbles are smoothed out (the direction is taken every 20 pixels, as Blender's "
     "Rake does). Angle still adds its turn.",
     "El alpha gira con el trazo: su parte de arriba apunta hacia donde dibujas, para escamas, puntadas y pelo que "
     "se alinean con el trazo. Los pequeños vaivenes se suavizan (la dirección se toma cada 20 píxeles, como hace "
     "Rake en Blender). Ángulo sigue sumando su giro."},
    {"Turn the alpha by a fixed angle in degrees (counter-clockwise); Ctrl+click to type.",
     "Gira el alpha un ángulo fijo en grados (en sentido antihorario); Ctrl+clic para escribir."},
    {"Angle jitter %.0f", "Variación de ángulo %.0f"},
    {"The stamp turns with you: with Drag Stamp its top points the way you drag; in a dots stroke it turns along "
     "the stroke. Off, Drag Stamp only sets the size and the stamp keeps Angle's turn. Slide Stamp never turns "
     "while you slide it.",
     "El sello gira contigo: con Drag Stamp su parte de arriba apunta hacia donde arrastras; en un trazo de puntos "
     "gira a lo largo del trazo. Desactivado, Drag Stamp solo fija el tamaño y el sello conserva el giro de Ángulo. "
     "Slide Stamp nunca gira mientras lo deslizas."},
    {"Angle jitter", "Variación de ángulo"},
    {"Turn each dab by a random angle, up to this many degrees either way: breaks up repeats in skin, rock and noise "
     "alphas.",
     "Gira cada toque un ángulo aleatorio, hasta estos grados a cada lado: rompe las repeticiones en alphas de piel, "
     "roca y ruido."},
    {"Tile across %.0f", "Mosaico horizontal %.0f"},
    {"Tile across", "Mosaico horizontal"},
    {"Repeat the alpha this many times across the brush (1 = once).",
     "Repite el alpha estas veces a lo ancho del pincel (1 = una vez)."},
    {"Tile down %.0f", "Mosaico vertical %.0f"},
    {"Tile down", "Mosaico vertical"},
    {"Repeat the alpha this many times down the brush (1 = once).",
     "Repite el alpha estas veces a lo alto del pincel (1 = una vez)."},
    // --- PanelCut.cpp (tools/i18n/coverage.py) ---
    {"Depth", "Profundidad"},
    // --- PanelExtract.cpp (tools/i18n/coverage.py) ---
    {"Smooths the extracted edge, which otherwise follows the mask's triangles in steps (0 = as masked).",
     "Suaviza el borde extraído, que si no sigue en escalones los triángulos de la máscara (0 = tal como se "
     "enmascaró)."},
    {"Thickness of the shell, as a fraction of the Part's size (0.015 is 1.5%). Negative grows it inward; 0 makes a "
     "single-sided surface.",
     "Grosor de la cáscara, como fracción del tamaño de la Pieza (0,015 es 1,5%). Negativo la hace crecer hacia "
     "dentro; 0 crea una superficie de una sola cara."},
    {"Reshape > Smooth smooths without shrinking the form (Taubin smoothing) instead of the plain smooth, which "
     "also shrinks it.",
     "Remodelar > Suavizar suaviza sin encoger la forma (suavizado de Taubin) en vez del suavizado simple, que "
     "también la encoge."},
    // --- PanelFace.cpp (tools/i18n/coverage.py) ---
    {"Give the active Part (a humanoid head, looking down +Z in the front view) the 52 Apple ARKit blend shapes as "
     "sculpt layers: eyeBlinkLeft, jawOpen, mouthSmileRight and the rest, fitted to this head's proportions. Then "
     "fix any of them by sculpting into its layer (Tool > Layers), test them with the presets below, export them as "
     "morph targets (GLB, USD, FBX, Blender) or drive them live from your phone. The landmarks you placed (at least "
     "3) guide the fit; otherwise it finds the face by itself. Eyes, teeth and tongue Parts (by name) and Parts on "
     "the face get the shapes they need. Running it again replaces the 52 layers (undo brings back the old ones and "
     "the fixes sculpted into them).",
     "Da a la Pieza activa (una cabeza humanoide, mirando hacia +Z en la vista frontal) las 52 formas de mezcla de "
     "Apple ARKit como capas de escultura: eyeBlinkLeft, jawOpen, mouthSmileRight y el resto, ajustadas a las "
     "proporciones de esta cabeza. Luego corrige cualquiera esculpiendo en su capa (Herramienta > Capas), pruébalas "
     "con los ajustes de abajo, expórtalas como morph targets (GLB, USD, FBX, Blender) o contrólalas en vivo desde "
     "tu teléfono. Los puntos de referencia que pusiste (al menos 3) guían el ajuste; si no, encuentra la cara por "
     "sí solo. Las Piezas de ojos, dientes y lengua (por nombre) y las Piezas sobre la cara reciben las formas que "
     "necesitan. Ejecutarlo otra vez reemplaza las 52 capas (deshacer recupera las anteriores y las correcciones "
     "esculpidas en ellas)."},
    {"Guide the fit for heads it gets wrong (stylised proportions, a turned head, a bust with big shoulders): click "
     "the eye corners, the nose tip, the mouth corners and the chin on the sculpt, in the order the canvas asks. "
     "Right and left are the character's own (its right eye is on your left in the front view). Click a dot again "
     "later to move it. 3 are enough to start; all 8 fit best. Auto places them all for you to check.",
     "Guía el ajuste en cabezas que salen mal (proporciones estilizadas, una cabeza girada, un busto con hombros "
     "grandes): haz clic en las comisuras de los ojos, la punta de la nariz, las comisuras de la boca y la barbilla "
     "sobre la escultura, en el orden que pide el lienzo. Derecha e izquierda son las del personaje (su ojo derecho "
     "está a tu izquierda en la vista frontal). Haz clic otra vez en un punto para moverlo. Con 3 basta para "
     "empezar; los 8 ajustan mejor. Auto los coloca todos para que los revises."},
    {"Find the landmarks automatically (the same fit Generate makes without landmarks) and show them, so you can "
     "check where the eye and mouth corners landed and move any that are off before generating.",
     "Busca los puntos de referencia automáticamente (el mismo ajuste que hace Generar sin puntos) y los muestra, "
     "para que compruebes dónde cayeron las comisuras de ojos y boca y muevas las que estén mal antes de generar."},
    {"Remove the landmarks: Generate then finds the face by itself.",
     "Quita los puntos de referencia: Generar encontrará entonces la cara por sí solo."},
    {"Play a recorded face take on the facial shapes: the JSON frames the phone link records ({\"type\":\"face\", "
     "\"t\":ms, \"weights\":{...}} as an array, {\"frames\":[...]} or JSON Lines).",
     "Reproduce una toma facial grabada en las formas faciales: los fotogramas JSON que graba el enlace del teléfono "
     "({\"type\":\"face\", \"t\":ms, \"weights\":{...}} como array, {\"frames\":[...]} o JSON Lines)."},
    {"Play or pause the loaded take (it loops while Loop is on).",
     "Reproduce o pausa la toma cargada (se repite mientras Bucle está activado)."},
    {"Bake the loaded take into keyframes at 30 fps: Export GLB writes them as a glTF animation of the morph target "
     "weights, Export USD as time samples (Blender, Maya, Unreal, Omniverse play them). Saved with the project.",
     "Hornea la toma cargada en fotogramas clave a 30 fps: Exportar GLB los escribe como animación glTF de los pesos "
     "de los morph targets, Exportar USD como muestras de tiempo (Blender, Maya, Unreal y Omniverse los reproducen). "
     "Se guarda con el proyecto."},
    {"Remove the baked take, so exports are a still pose again.",
     "Quita la toma horneada, para que las exportaciones vuelvan a ser una pose fija."},
    {"Auto: Left and Right shapes are made exact mirror images when the sculpt is symmetric about X = 0. On: always "
     "(asymmetric spots take the average). Off: each side as fitted.",
     "Auto: las formas Left y Right se hacen reflejos exactos cuando la escultura es simétrica respecto a X = 0. "
     "Activado: siempre (las zonas asimétricas toman la media). Desactivado: cada lado según se ajustó."},
    {"Eyes (Parts named Eye...) turn for the eyeLook shapes, Teeth / Tongue follow the jaw below the bite (and "
     "tongueOut), and other Parts on the face (brows, lashes, a beard) follow the skin.",
     "Los ojos (Piezas llamadas Eye...) giran para las formas eyeLook, Teeth / Tongue siguen a la mandíbula bajo la "
     "mordida (y tongueOut), y las demás Piezas sobre la cara (cejas, pestañas, una barba) siguen a la piel."},
    {"How strongly the presets above are applied (click a preset again to see it).",
     "Con qué fuerza se aplican los ajustes de arriba (haz clic otra vez en un ajuste para verlo)."},
    {"This ARKit shape's weight (it drives the same-named layer on every Part). To fix the shape, select its layer in "
     "Tool > Layers, set the weight to 1 and sculpt.",
     "El peso de esta forma de ARKit (controla la capa del mismo nombre en cada Pieza). Para corregir la forma, "
     "elige su capa en Herramienta > Capas, pon el peso a 1 y esculpe."},
    {"Play the take again from the start when it ends.",
     "Vuelve a reproducir la toma desde el principio cuando termina."},
    // --- PanelFill.cpp (tools/i18n/coverage.py) ---
    {"Fill Layers", "Capas de relleno"},
    {"New Fill Layer", "Nueva capa de relleno"},
    {"Add a Fill layer: a material over the whole Part through a mask you build from effects (Edge Wear, Dirt, "
     "Gradient, Thickness, Light Direction, Noise, a painted mask, Levels, Blur, Invert). Change it any time; it "
     "follows the sculpt.",
     "Añade una capa de relleno: un material sobre toda la Pieza a través de una máscara que construyes con efectos "
     "(Edge Wear, Dirt, Gradient, Thickness, Light Direction, Noise, una máscara pintada, Levels, Blur, Invert). "
     "Cámbiala cuando quieras; sigue a la escultura."},
    {"Material Recipe", "Receta de material"},
    {"Lay a ready-made stack of Fill layers on the Part: Worn Metal, Painted Edges, Dirt and Dust, Rusted Iron. They "
     "read the Part's Mesh Maps, so each model wears where it would. Undoable.",
     "Pone en la Pieza una pila lista de capas de relleno: Worn Metal, Painted Edges, Dirt and Dust, Rusted Iron. "
     "Leen los Mesh Maps de la Pieza, así cada modelo se desgasta donde lo haría. Se puede deshacer."},
    {"Baking Mesh Maps...", "Horneando Mesh Maps..."},
    {"No Mesh Maps yet", "Aún no hay Mesh Maps"},
    {"Mesh Maps", "Mesh Maps"},
    {"Ambient occlusion, curvature, thickness, position, world normal and ID of the Part on its texture's UVs, which "
     "Fill layers read. Baked in the background when a Fill layer needs them; with Live on, the tiles near what you "
     "sculpt are baked again after each stroke.",
     "Oclusión ambiental, curvatura, grosor, posición, normal del mundo e ID de la Pieza sobre las UV de su textura, "
     "que leen las capas de relleno. Se hornean en segundo plano cuando una capa de relleno los necesita; con En "
     "vivo activado, los mosaicos cerca de lo que esculpes se vuelven a hornear tras cada trazo."},
    {"Fill colour", "Color de relleno"},
    {"The base colour the Fill layer lays down.", "El color base que pone la capa de relleno."},
    {"Paint on the model with this layer active (Alt erases)", "Pinta en el modelo con esta capa activa (Alt borra)"},
    {"Add mask effect", "Añadir efecto de máscara"},
    {"Add an effect to the bottom of the mask stack. Generators read the Mesh Maps: Edge Wear (convex edges), Dirt "
     "(crevices), Gradient, Thickness (thin parts), Light Direction (surfaces facing up). Noise is in 3D, so it has "
     "no UV seams. Paint lets you paint the mask with the Paint brush.",
     "Añade un efecto al fondo de la pila de máscara. Los generadores leen los Mesh Maps: Edge Wear (bordes "
     "convexos), Dirt (grietas), Gradient, Thickness (partes finas), Light Direction (superficies hacia arriba). "
     "Noise es 3D, así que no tiene costuras UV. Paint te deja pintar la máscara con el pincel Paint."},
    // --- PanelForgeSpheres.cpp (tools/i18n/coverage.py) ---
    {"Add a Forge Sphere Part: one root sphere to block out a body, creature or plant from. Click a sphere to grow "
     "limbs from it, drag them into place and proportion, check the skin with Preview (A), then Make Mesh.",
     "Añade una Pieza de Forge Spheres: una esfera raíz desde la que bloquear un cuerpo, una criatura o una planta. "
     "Haz clic en una esfera para hacer crecer extremidades, arrástralas a su sitio y proporción, revisa la piel "
     "con Vista previa (A) y luego Crear malla."},
    {"Build without switching modes: click a sphere to grow a new one from it, drag a sphere to move it, drag "
     "near its edge to resize it (the cursor shows which). Click a link between two spheres to add a joint "
     "there, and drag to place it. Alt+click a sphere to delete it.",
     "Construye sin cambiar de modo: haz clic en una esfera para que crezca una nueva de ella, arrastra una esfera "
     "para moverla, arrastra cerca de su borde para cambiar su tamaño (el cursor indica cuál). Haz clic en un enlace "
     "entre dos esferas para añadir ahí una articulación, y arrastra para colocarla. Alt+clic en una esfera la borra."},
    {"Drag a sphere to move it; the links stretch to follow. Symmetry moves its mirror twin too.",
     "Arrastra una esfera para moverla; los enlaces se estiran para seguirla. La simetría mueve también su gemela "
     "reflejada."},
    {"Drag a sphere up or right to make it bigger, down or left to make it smaller.",
     "Arrastra una esfera hacia arriba o la derecha para agrandarla, hacia abajo o la izquierda para achicarla."},
    {"Drag places child", "Arrastrar coloca hija"},
    {"Draw: dragging on a sphere grows a new sphere and moves it away from it (one drag places a limb joint) "
     "instead of moving the sphere. Off, hold Ctrl while you drag for the same.",
     "Dibujar: arrastrar sobre una esfera hace crecer una esfera nueva y la aleja de ella (un arrastre coloca una "
     "articulación de extremidad) en vez de mover la esfera. Desactivado, mantén Ctrl mientras arrastras para lo mismo."},
    {"click: add  drag: move", "clic: añadir  arrastrar: mover"},
    {"drag: add", "arrastrar: añadir"},
    {"drag: resize", "arrastrar: tamaño"},
    {"Delete Selected", "Borrar selección"},
    {"Delete the selected sphere (the one you last clicked or added, outlined in orange) and everything growing from "
     "it. The root stays.",
     "Borra la esfera seleccionada (la última en la que hiciste clic o que añadiste, con contorno naranja) y todo lo "
     "que crece de ella. La raíz se queda."},
    {"Insert Between", "Insertar entre"},
    {"Add a joint halfway along the link from the selected sphere to the one it grows from.",
     "Añade una articulación a medio camino del enlace entre la esfera seleccionada y aquella de la que crece."},
    {"Set as Root", "Hacer raíz"},
    {"Make the selected sphere the root: the armature keeps its shape and grows from there (the links on the way turn "
     "around).",
     "Hace raíz la esfera seleccionada: el esqueleto conserva su forma y crece desde ahí (los enlaces del camino se "
     "dan la vuelta)."},
    {"Show the skin Make Mesh would build instead of the spheres. Turn it off to edit the spheres again.",
     "Muestra la piel que construiría Crear malla en vez de las esferas. Desactívalo para volver a editar las "
     "esferas."},
    {"Build the skin as a new, sculptable Part right below (watertight quads, one face group per link), like Make "
     "Adaptive Skin. The Forge Sphere Part is hidden and kept: show it to change the proportions and make a new mesh.",
     "Construye la piel como Pieza nueva esculpible justo debajo (quads estancos, un grupo de caras por enlace), "
     "como Make Adaptive Skin. La Pieza de Forge Spheres se oculta y se conserva: muéstrala para cambiar las "
     "proporciones y crear una malla nueva."},
    {"How dense Make Mesh's skin is: cells across the model's longest side. 128 suits a blockout; raise it for thin "
     "fingers or tails.",
     "Lo densa que es la piel de Crear malla: celdas a lo largo del lado más largo del modelo. 128 va bien para un "
     "bloqueo; súbelo para dedos finos o colas."},
    {"Forge Sphere insert", "Insertar Forge Sphere"},
    {"Forge Sphere add", "Añadir Forge Sphere"},
    // --- PanelGeometry.cpp (tools/i18n/coverage.py) ---
    {"The subdivision level to sculpt at: 1 is the base cage, the highest has the finest detail. D / Shift+D step up "
     "/ down. Detail is kept on every level.",
     "El nivel de subdivisión en el que esculpir: 1 es la caja base, el más alto tiene el detalle más fino. D / "
     "Mayús+D suben / bajan. El detalle se conserva en cada nivel."},
    {"Micro levels above the highest subdivision level: each 4x the triangles, stored as one 16-bit displacement per "
     "point (no mesh), so 300 million triangles take about 0.4 GB and sculpt in real time. Move reshapes the level "
     "below and the detail rides along.",
     "Niveles micro por encima del nivel de subdivisión más alto: cada uno 4x los triángulos, guardados como un "
     "desplazamiento de 16 bits por punto (sin malla), así 300 millones de triángulos ocupan unos 0,4 GB y se "
     "esculpen en tiempo real. Move cambia la forma del nivel inferior y el detalle va con él."},
    // --- PanelIntegrations.cpp (tools/i18n/coverage.py) ---
    {"Show SculptForge on Discord", "Mostrar SculptForge en Discord"},
    {"Your Discord profile shows SculptForge, its icon and how long you've been sculpting. Sent only to the Discord "
     "app on this computer, which shows it to your friends. Off by default.",
     "Tu perfil de Discord muestra SculptForge, su icono y cuánto llevas esculpiendo. Se envía solo a la app de "
     "Discord de este equipo, que lo muestra a tus amigos. Desactivado por defecto."},
    {"Show the monthly challenge", "Mostrar el reto mensual"},
    {"Adds \"Monthly challenge: <theme>\", with the theme you type below.",
     "Añade \"Monthly challenge: <theme>\", con el tema que escribas abajo."},
    {"Show the file name", "Mostrar el nombre del archivo"},
    {"Adds the name of the file you're working on (never its folder). Off by default.",
     "Añade el nombre del archivo en el que trabajas (nunca su carpeta). Desactivado por defecto."},
    // --- PanelKelvinlet.cpp (tools/i18n/coverage.py) ---
    {"How the Kelvinlet brushes' material keeps its volume (Poisson's ratio): 0 is rubbery (it stretches without "
     "bulging), 100 is incompressible clay (pressing in bulges the surface up around the thumb, pulling out draws "
     "the sides in).",
     "Cómo conserva su volumen el material de los pinceles Kelvinlet (coeficiente de Poisson): 0 es gomoso (se "
     "estira sin abultarse), 100 es arcilla incompresible (presionar abulta la superficie alrededor del pulgar, "
     "tirar hacia fuera mete los lados)."},
    {"A tighter, more pointed peak at the brush centre with the same soft reach (a smaller inner scale of the "
     "multi-scale Kelvinlet): for pulling points and tips.",
     "Un pico más apretado y puntiagudo en el centro del pincel con el mismo alcance suave (una escala interior "
     "menor del Kelvinlet multiescala): para tirar de puntas y extremos."},
    {"Detail Clone: the distance between stamps along the stroke, in stamp widths (1 = edge to edge).",
     "Detail Clone: la distancia entre sellos a lo largo del trazo, en anchos de sello (1 = borde con borde)."},
    {"Detail Clone: each stamp's top turns with the stroke (a row of scales or rivets along a curve); off, every "
     "stamp keeps the screen's up.",
     "Detail Clone: la parte de arriba de cada sello gira con el trazo (una fila de escamas o remaches a lo largo de "
     "una curva); desactivado, cada sello mantiene el arriba de la pantalla."},
    {"Alpha and VDM from the viewport: drag a square over any sculpted detail (Shift for a circle) and capture it as "
     "a grayscale height alpha and as a vector displacement brush, ready to stamp with the VDM brush or along "
     "strokes with Detail Clone, and to save in your library.",
     "Alpha y VDM desde la vista: arrastra un cuadrado sobre cualquier detalle esculpido (Mayús para un círculo) y "
     "captúralo como alpha de altura en escala de grises y como pincel de desplazamiento vectorial, listo para "
     "estampar con el pincel VDM o a lo largo de trazos con Detail Clone, y para guardarlo en tu biblioteca."},
    {"What counts as the detail: the capture is the surface minus a smoothed copy of itself. 0 measures from a flat "
     "plane (the whole shape); higher takes away more of the broad form, so a detail on a curved arm or cheek "
     "captures flat. The VDM keeps each point's sideways offset too (overhangs).",
     "Qué cuenta como detalle: la captura es la superficie menos una copia suavizada de sí misma. 0 mide desde un "
     "plano (toda la forma); más alto quita más de la forma general, así un detalle en un brazo o una mejilla curvos "
     "se captura plano. El VDM guarda también el desplazamiento lateral de cada punto (voladizos)."},
    {"Fades the capture out toward its border, so stamps blend into any surface.",
     "Desvanece la captura hacia su borde, para que los sellos se fundan con cualquier superficie."},
    {"Pixels a side of the alpha and the map (512 or less suits most details).",
     "Píxeles por lado del alpha y del mapa (512 o menos va bien para la mayoría de detalles)."},
    {"Height alpha: measure from a plane fitted to the surface (a tilted surface captures level) instead of the "
     "screen's plane.",
     "Alpha de altura: mide desde un plano ajustado a la superficie (una superficie inclinada se captura nivelada) "
     "en vez del plano de la pantalla."},
    {"Height alpha: white = deepest (for carving brushes).",
     "Alpha de altura: blanco = lo más hondo (para pinceles de tallar)."},
    {"What the alpha and the VDM are called in your library (the same name replaces).",
     "Cómo se llaman el alpha y el VDM en tu biblioteca (el mismo nombre reemplaza)."},
    {"Make the height capture the brush alpha (it shapes every brush's stamp).",
     "Usa la captura de altura como alpha del pincel (da forma al sello de cada pincel)."},
    {"Make the capture the VDM brush's map and pick the VDM brush (Drag Stamp stamps it).",
     "Usa la captura como mapa del pincel VDM y elige el pincel VDM (Drag Stamp la estampa)."},
    {"Paint copies of the captured detail along your strokes, each turned to follow the stroke.",
     "Pinta copias del detalle capturado a lo largo de tus trazos, cada una girada para seguir el trazo."},
    {"Keep both: the alpha as a PNG in your Library Alphas folder, the VDM in your brush library (Alpha picker, Brush "
     "> Vector Displacement and the Library's Brush tab), with thumbnails.",
     "Guarda ambos: el alpha como PNG en la carpeta Alphas de tu Biblioteca, el VDM en tu biblioteca de pinceles "
     "(selector de Alpha, Pincel > Desplazamiento vectorial y la pestaña Pincel de la Biblioteca), con miniaturas."},
    // --- PanelMasking.cpp (tools/i18n/coverage.py) ---
    {"Mask ridges, peaks and sharp edges (replaces the mask; uses the mask strength)",
     "Enmascara crestas, picos y bordes afilados (reemplaza la máscara; usa la fuerza de máscara)"},
    {"Mask where the vertex paint is close to the main colour (the left shelf's swatch); Tolerance sets how close.",
     "Enmascara donde la pintura de vértices se parece al color principal (la muestra del estante izquierdo); "
     "Tolerancia fija cuánto."},
    {"Mask what faces the camera (or faces up or down; Facing below), within the angle: e.g. the front of a face, or "
     "the top of a rock.",
     "Enmascara lo que mira a la cámara (o hacia arriba o abajo; Orientación, abajo), dentro del ángulo: p. ej. el "
     "frente de una cara, o la parte de arriba de una roca."},
    {"Mask the open edges of the mesh and the edge rings inside them, so sculpting never moves the border. Replaces "
     "the mask and uses the mask strength. A closed mesh has no open edges.",
     "Enmascara los bordes abiertos de la malla y los anillos de aristas de dentro, para que esculpir nunca mueva el "
     "borde. Reemplaza la máscara y usa la fuerza de máscara. Una malla cerrada no tiene bordes abiertos."},
    {"Cavity / peaks threshold %.2f", "Umbral de cavidad / crestas %.2f"},
    {"How deep a crease (By cavity) or how sharp a ridge (By peaks) must be: 0 takes anything, higher only the "
     "strongest.",
     "Lo honda que debe ser un pliegue (Por cavidad) o lo afilada una cresta (Por crestas): 0 toma todo, más alto "
     "solo lo más marcado."},
    {"Colour tolerance %.2f", "Tolerancia de color %.2f"},
    {"Border rings %d", "Anillos de borde %d"},
    {"Face Group borders", "Bordes de grupos de caras"},
    {"By border also masks where face groups meet.",
     "Por borde también enmascara donde se juntan los grupos de caras."},
    {"Soft", "Suave"},
    {"By border fades the mask from the border towards the innermost ring.",
     "Por borde desvanece la máscara desde el borde hacia el anillo más interior."},
    // --- PanelMaya.cpp (tools/i18n/coverage.py) ---
    {"Get automatically", "Recibir automáticamente"},
    {"Load what Maya sends as soon as it arrives: SculptForge checks the exchange folder's manifest every "
     "250 ms (its modification time and size only). On once you choose the folder; off: use Get from Maya.",
     "Carga lo que envía Maya en cuanto llega: SculptForge comprueba el manifiesto de la carpeta de intercambio cada "
     "250 ms (solo su fecha de modificación y su tamaño). Activado al elegir la carpeta; desactivado: usa "
     "Recibir de Maya."},
    {"Turn the Maya bridge off", "Desactivar el puente con Maya"},
    {"Forget the exchange folder; nothing in it is deleted.",
     "Olvida la carpeta de intercambio; no se borra nada de ella."},
    // --- PanelPacks.cpp (tools/i18n/coverage.py) ---
    {"Install Pack...", "Instalar paquete..."},
    {"Install a SculptForge pack (.sfpack): brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, pie menus "
     "and macros. A review shows exactly what's inside, its license and its author before anything is installed. "
     "Packs hold data only: nothing in one ever runs.",
     "Instala un paquete de SculptForge (.sfpack): pinceles, alphas, Lit Spheres, recetas de material, VDM, mapas de "
     "teclas, menús radiales y macros. Una revisión muestra exactamente qué hay dentro, su licencia y su autor antes "
     "de instalar nada. Los paquetes solo contienen datos: nada de ellos se ejecuta nunca."},
    {"New Pack...", "Nuevo paquete..."},
    {"Make a pack from your own brushes, alphas, Lit Spheres, Material Recipe, VDMs, pie menus, shortcuts and macros, "
     "to share as one .sfpack file.",
     "Crea un paquete con tus propios pinceles, alphas, Lit Spheres, receta de material, VDM, menús radiales, atajos "
     "y macros, para compartirlo como un solo archivo .sfpack."},
    {"Packs", "Paquetes"},
    {"Library > Packs: the installed packs. Switch one off or remove it, apply its keymap, add its pie menus.",
     "Biblioteca > Paquetes: los paquetes instalados. Desactiva o quita uno, aplica su mapa de teclas, añade sus "
     "menús radiales."},
    {"Record Macro", "Grabar macro"},
    {"Start recording: the commands you run (menus, buttons, the console) are kept as a macro until you press it "
     "again. Files inside the project folder are kept as relative paths, any other file is asked for when the macro "
     "runs.",
     "Empieza a grabar: los comandos que ejecutas (menús, botones, la consola) se guardan como macro hasta que lo "
     "pulses otra vez. Los archivos dentro de la carpeta del proyecto se guardan como rutas relativas; cualquier "
     "otro archivo se pide al ejecutar la macro."},
    {"Macro Editor", "Editor de macros"},
    {"Write or change a macro: SculptForge commands, one per line, checked as you type. Only SculptForge's own "
     "commands, nothing that reaches the network, and files only inside the project folder or picked when it runs.",
     "Escribe o cambia una macro: comandos de SculptForge, uno por línea, comprobados mientras escribes. Solo los "
     "propios comandos de SculptForge, nada que llegue a la red, y archivos solo dentro de la carpeta del proyecto o "
     "elegidos al ejecutarla."},
    {"Macros", "Macros"},
    {"Install Pack", "Instalar paquete"},
    {"This pack can't be installed", "Este paquete no se puede instalar"},
    {"Nothing was installed. SculptForge only installs packs that pass every check: plain data files of the kinds a "
     "pack may hold, listed in its pack.json, within safe sizes, with nothing that could leave the pack's folder.",
     "No se instaló nada. SculptForge solo instala paquetes que pasan todas las comprobaciones: archivos de datos "
     "simples de los tipos que puede contener un paquete, listados en su pack.json, de tamaños seguros, y nada que "
     "pueda salir de la carpeta del paquete."},
    {"Member pack", "Paquete de miembros"},
    {"Author:", "Autor:"},
    {"License:", "Licencia:"},
    {"Replaces the installed version", "Reemplaza la versión instalada"},
    {"Contains:", "Contiene:"},
    {"No license text in the pack (see License above).",
     "No hay texto de licencia en el paquete (ver Licencia arriba)."},
    {"Packs hold data only: nothing in this one runs on install or when a project opens. Its macros list their "
     "commands and ask before they first run.",
     "Los paquetes solo contienen datos: nada de este se ejecuta al instalarlo ni al abrir un proyecto. Sus macros "
     "muestran sus comandos y preguntan antes de ejecutarse por primera vez."},
    {"Keymaps and pie menus are only applied when you choose to (Library > Packs).",
     "Los mapas de teclas y los menús radiales solo se aplican cuando tú lo eliges (Biblioteca > Paquetes)."},
    {"Install", "Instalar"},
    {"Copy the pack into SculptForge's packs folder and add its items to the lists.",
     "Copia el paquete en la carpeta de paquetes de SculptForge y añade sus elementos a las listas."},
    {"Stop Recording", "Detener grabación"},
    {"Safe mode (--no-packs): no pack is loaded this session",
     "Modo seguro (--no-packs): no se carga ningún paquete en esta sesión"},
    {"No packs installed. Drop a .sfpack on the window or use Install Pack...",
     "No hay paquetes instalados. Suelta un .sfpack en la ventana o usa Instalar paquete..."},
    {"Pack on", "Paquete activo"},
    {"Off: its items leave the lists until you switch it on again (nothing is deleted).",
     "Desactivado: sus elementos salen de las listas hasta que lo vuelvas a activar (no se borra nada)."},
    {"Details", "Detalles"},
    {"Remove", "Quitar"},
    {"Delete the pack and everything it brought. Your own files are never touched.",
     "Borra el paquete y todo lo que trajo. Tus propios archivos nunca se tocan."},
    {"Apply keymap", "Aplicar mapa de teclas"},
    {"Add pie menu", "Añadir menú radial"},
    {"Run", "Ejecutar"},
    {"Your macros", "Tus macros"},
    {"Pack keymaps", "Teclas de paquetes"},
    {"Take the keys of this keymap from an installed pack (only actions SculptForge knows; a key another action had "
     "moves here). Saved automatically.",
     "Toma las teclas de este mapa de un paquete instalado (solo acciones que SculptForge conoce; una tecla que "
     "tenía otra acción pasa aquí). Se guarda automáticamente."},
    {"New Pack", "Nuevo paquete"},
    {"The pack's name, shown next to every item it brings.",
     "El nombre del paquete, que se muestra junto a cada elemento que trae."},
    {"Id", "Id"},
    {"Its folder name: lower-case letters, digits, '.', '_' or '-'. A newer version keeps the same id.",
     "El nombre de su carpeta: minúsculas, dígitos, '.', '_' o '-'. Una versión más nueva conserva el mismo id."},
    {"Version", "Versión"},
    {"Author", "Autor"},
    {"Who made it (shown when it's installed).", "Quién lo hizo (se muestra al instalarlo)."},
    {"License", "Licencia"},
    {"For example CC-BY-4.0, CC0-1.0 or your own terms.", "Por ejemplo CC-BY-4.0, CC0-1.0 o tus propios términos."},
    {"Member pack (insider channel)", "Paquete de miembros (canal insider)"},
    {"Marks the pack as a members' pack (Member Requests). Display only: anyone can install it.",
     "Marca el paquete como paquete de miembros (Member Requests). Solo se muestra: cualquiera puede instalarlo."},
    {"Nothing of your own yet: save brushes, put alphas and Lit Spheres in your Library folders, record a macro...",
     "Aún no hay nada tuyo: guarda pinceles, pon alphas y Lit Spheres en las carpetas de tu Biblioteca, graba una "
     "macro..."},
    {"Save Pack...", "Guardar paquete..."},
    {"Save Pack", "Guardar paquete"},
    {"Write the .sfpack, checked exactly as an install would check it.",
     "Escribe el .sfpack, comprobado exactamente como lo comprobaría una instalación."},
    {"Run Macro?", "¿Ejecutar macro?"},
    {"Your macro", "Tu macro"},
    {"Files it uses stay inside the project folder.",
     "Los archivos que usa se quedan dentro de la carpeta del proyecto."},
    {"Run it now. You won't be asked again for this macro unless it changes.",
     "Ejecútala ahora. No se te volverá a preguntar por esta macro a menos que cambie."},
    {"One command per line, as in the console (# starts a comment). Files: a path inside the project folder, or ? to "
     "pick one when it runs.",
     "Un comando por línea, como en la consola (# empieza un comentario). Archivos: una ruta dentro de la carpeta "
     "del proyecto, o ? para elegir uno al ejecutarla."},
    {"Recording macro", "Grabando macro"},
    // --- PanelPaintLayers.cpp (tools/i18n/coverage.py) ---
    {"Lock the layer: the Paint brush, fills, clears and merges leave it alone.",
     "Bloquea la capa: el pincel Paint, los rellenos, los borrados y las fusiones no la tocan."},
    {"Show or hide the layer. Hidden layers don't show or export.",
     "Muestra u oculta la capa. Las capas ocultas no se ven ni se exportan."},
    {"How the active layer mixes with the layers below, with Photoshop's maths: Multiply darkens, Screen lightens, "
     "Overlay adds contrast (50% grey changes nothing), Add brightens, ...",
     "Cómo se mezcla la capa activa con las de abajo, con las matemáticas de Photoshop: Multiplicar oscurece, Trama "
     "aclara, Superponer añade contraste (el gris al 50% no cambia nada), Añadir aclara, ..."},
    {"How strongly the active layer shows (0-100%). Painting keeps its own coverage.",
     "Cuánto se ve la capa activa (0-100%). La pintura conserva su propia cobertura."},
    {"Add a transparent paint layer above the active one and paint on it.",
     "Añade una capa de pintura transparente encima de la activa y pinta en ella."},
    {"Copy the active layer (Base becomes an opaque layer) to a new layer above it.",
     "Copia la capa activa (Base pasa a ser una capa opaca) en una capa nueva encima."},
    {"Delete the active layer (undoable). Base can't be deleted.",
     "Borra la capa activa (se puede deshacer). Base no se puede borrar."},
    {"Move the active layer up the stack (it then covers the layer it passed).",
     "Sube la capa activa en la pila (entonces cubre la capa que pasó)."},
    {"Move the active layer down the stack (Base stays at the bottom).",
     "Baja la capa activa en la pila (Base se queda abajo del todo)."},
    {"Merge the active layer into the one below it (Base for the lowest layer), keeping how it looks.",
     "Fusiona la capa activa con la de abajo (Base para la capa más baja), conservando su aspecto."},
    {"Flatten", "Acoplar"},
    {"Bake every visible layer into Base and remove the layers (hidden layers are discarded).",
     "Hornea todas las capas visibles en Base y quita las capas (las capas ocultas se descartan)."},
    {"Clear the active layer: a layer becomes transparent, Base white. Undoable.",
     "Borra la capa activa: una capa queda transparente, Base blanca. Se puede deshacer."},
    {"Fill the active layer with the current colour (respects the mask, like Color > Fill).",
     "Rellena la capa activa con el color actual (respeta la máscara, como Color > Rellenar)."},
    // --- PanelParts.cpp (tools/i18n/coverage.py) ---
    {"Draw the other Parts solid again", "Dibujar de nuevo sólidas las demás Piezas"},
    {"See through the other Parts to the active one", "Ver la Pieza activa a través de las demás"},
    // --- PanelPbr.cpp (tools/i18n/coverage.py) ---
    {"Write the texture's channels as image files for an engine or DCC, with the export preset chosen beside the "
     "button (Unreal, Unity URP / HDRP, glTF, Godot, Blender, USD).",
     "Escribe los canales de la textura como archivos de imagen para un motor o un DCC, con el ajuste de exportación "
     "elegido junto al botón (Unreal, Unity URP / HDRP, glTF, Godot, Blender, USD)."},
    {"Show one channel on its own on the model: roughness, metallic, height, AO and opacity in grey, the normal as "
     "its colours (with the height's normal in it), emission in colour. Painting updates it live.",
     "Muestra un canal solo en el modelo: rugosidad, metálico, altura, AO y opacidad en gris, la normal con sus "
     "colores (con la normal de la altura incluida), la emisión en color. Pintar lo actualiza en vivo."},
    {"Which files Export Maps writes and how channels are packed. Your own presets are JSON files (export_maps "
     "preset=path/to/preset.json; see the guide's Export presets page).",
     "Qué archivos escribe Exportar mapas y cómo se empaquetan los canales. Tus propios ajustes son archivos JSON "
     "(export_maps preset=ruta/al/ajuste.json; ver la página Export presets de la guía)."},
    {"Set the brush's colour, roughness and metallic at once, and make it paint those channels.",
     "Fija a la vez el color, la rugosidad y el metálico del pincel, y hace que pinte esos canales."},
    {"Which way painted normals lean (x, y); 0, 0 paints a flat normal.",
     "Hacia dónde se inclinan las normales pintadas (x, y); 0, 0 pinta una normal plana."},
    {"Whether this layer affects the channel. Right-click for the channel's blend mode (Normal Combine lays normal "
     "detail onto the normals below) and opacity.",
     "Si esta capa afecta al canal. Clic derecho para el modo de fusión del canal (Normal Combine pone el detalle de "
     "normales sobre las normales de abajo) y la opacidad."},
    // --- PanelPerformance.cpp (tools/i18n/coverage.py) ---
    {"While you rotate, pan or zoom a big scene (over the hardware profile's motion proxy threshold, which the frame "
     "governor adjusts; 10 million triangles without a profile), draw a lighter version without antialiasing, so the "
     "view keeps up; it is full detail again as soon as the view stops.",
     "Mientras giras, desplazas o acercas una escena grande (por encima del umbral de proxy de movimiento del perfil "
     "de hardware, que ajusta el regulador de fotogramas; 10 millones de triángulos sin perfil), dibuja una versión "
     "más ligera sin antialiasing, para que la vista vaya fluida; vuelve al detalle completo en cuanto la vista se "
     "detiene."},
    {"Run brushes, normal and BVH updates and project saving on every core. Turn it off only to compare speeds or "
     "rule it out when troubleshooting: the results are the same either way.",
     "Ejecuta pinceles, actualizaciones de normales y BVH y el guardado del proyecto en todos los núcleos. "
     "Desactívalo solo para comparar velocidades o descartarlo al buscar un problema: el resultado es el mismo en "
     "ambos casos."},
    {"Sculpt strokes work out the brush on their own thread while the view keeps drawing at full speed, and show the "
     "dabs still being worked out as a preview on the surface, with the cursor and the stroke's head drawn where the "
     "pen is about to be. On a heavy mesh the surface moves under the pen at once and lifting the pen is instant. "
     "The finished sculpt is exactly the same either way.",
     "Los trazos de escultura calculan el pincel en su propio hilo mientras la vista sigue dibujando a toda "
     "velocidad, y muestran como vista previa en la superficie los toques que aún se calculan, con el cursor y la "
     "cabeza del trazo dibujados donde va a estar el lápiz. En una malla pesada la superficie se mueve bajo el lápiz "
     "al instante y levantar el lápiz es inmediato. La escultura final es exactamente la misma en ambos casos."},
    {"Experimental. The Move brush's drag is drawn by the graphics card from where the surface started, so it follows "
     "the pen at the view's frame rate whatever the mesh size; the move itself is applied once, exactly, when you "
     "lift the pen. Not with masks, hidden parts, sculpt layers, micro levels, Connected Only or View falloff (Move "
     "works as before there).",
     "Experimental. El arrastre del pincel Move lo dibuja la tarjeta gráfica desde donde empezó la superficie, así "
     "sigue al lápiz a la velocidad de la vista sea cual sea el tamaño de la malla; el movimiento en sí se aplica "
     "una vez, exacto, al levantar el lápiz. No con máscaras, partes ocultas, capas de escultura, niveles micro, "
     "Solo conectado ni Ver atenuación (ahí Move funciona como antes)."},
    {"Experimental. On meshes of 2 million vertices or more, big brushes are worked out on their own thread; with "
     "this on, the dabs still being worked out are shown as a preview on the surface (as Predictive Strokes does), "
     "so the stroke stays under the pen. The finished sculpt is exactly the same either way.",
     "Experimental. En mallas de 2 millones de vértices o más, los pinceles grandes se calculan en su propio hilo; "
     "con esto activado, los toques que aún se calculan se muestran como vista previa en la superficie (como hace "
     "Trazos predictivos), así el trazo se queda bajo el lápiz. La escultura final es exactamente la misma en ambos "
     "casos."},
    {"Experimental. While you sculpt, the changed part of the mesh goes to the graphics card through memory both can "
     "see, and the card copies it into place itself, instead of the driver copying it during the frame. The sculpt "
     "and the view are exactly the same either way; needs a driver with buffer storage (OpenGL 4.4).",
     "Experimental. Mientras esculpes, la parte cambiada de la malla va a la tarjeta gráfica por memoria que ven "
     "ambas, y la tarjeta la copia a su sitio ella misma, en vez de copiarla el controlador durante el fotograma. La "
     "escultura y la vista son exactamente iguales en ambos casos; necesita un controlador con buffer storage "
     "(OpenGL 4.4)."},
    {"With Predictive Strokes: while you sculpt, the brush only moves the surface and shades the moved part from its "
     "facets; the smooth shading, the cavities and the rest are worked out once when you lift the pen. Lighter on "
     "laptops and on battery: the hardware profile turns it on there by itself (its Draft brushes setting); this "
     "turns it on everywhere. Brushes that follow the surface's direction (Inflate, the plane brushes) use its "
     "direction from the stroke's start, so the result can differ slightly from a full stroke.",
     "Con Trazos predictivos: mientras esculpes, el pincel solo mueve la superficie y sombrea la parte movida a "
     "partir de sus facetas; el sombreado suave, las cavidades y lo demás se calculan una vez al levantar el lápiz. "
     "Más ligero en portátiles y con batería: el perfil de hardware lo activa solo ahí (su ajuste de pinceles en "
     "borrador); esto lo activa en todas partes. Los pinceles que siguen la dirección de la superficie (Inflate, los "
     "pinceles de plano) usan su dirección del inicio del trazo, así que el resultado puede diferir un poco de un "
     "trazo completo."},
    {"Fast Big Brushes (Experimental)", "Pinceles grandes rápidos (experimental)"},
    {"Experimental. Big brushes on dense meshes: a dab that reaches more than 150,000 vertices finds its plane from a "
     "sample of them and turns the shading along with the move it makes, and the dabs waiting for a frame are placed "
     "together in one pass. Every vertex still moves by the brush itself; the exact shading follows behind the brush "
     "and when you lift the pen. Small brushes, Smooth, Layer, Inflate, Nudge, Clay Block, alphas and masked areas "
     "are placed dab by dab as before. Try it on a heavy mesh and tell us how it feels.",
     "Experimental. Pinceles grandes en mallas densas: un toque que alcanza más de 150.000 vértices calcula su plano "
     "con una muestra de ellos y gira el sombreado junto con el movimiento que hace, y los toques que esperan un "
     "fotograma se colocan juntos en una pasada. Cada vértice sigue moviéndolo el propio pincel; el sombreado exacto "
     "llega detrás del pincel y al levantar el lápiz. Los pinceles pequeños, Smooth, Layer, Inflate, Nudge, Clay "
     "Block, los alphas y las zonas enmascaradas se colocan toque a toque como antes. Pruébalo en una malla pesada y "
     "cuéntanos qué tal."},
    {"Samples per pixel in the 3D view: more smooths edges, fewer is faster on huge meshes.",
     "Muestras por píxel en la vista 3D: más suaviza los bordes, menos es más rápido en mallas enormes."},
    {"Draw the 3D view at a share of the window's resolution and scale it up: 75% draws about half the pixels, 50% a "
     "quarter. For slow or integrated GPUs and big screens; Save Render stays sharp (#80).",
     "Dibuja la vista 3D a una fracción de la resolución de la ventana y la escala: 75% dibuja más o menos la mitad "
     "de los píxeles, 50% una cuarta parte. Para GPU lentas o integradas y pantallas grandes; Guardar render sigue "
     "nítido (#80)."},
    {"What the last brush stroke cost, per dab (one step of the brush, all symmetry sides): finding and moving the "
     "vertices, updating their normals and the BVH (the picking structure), and sending the change to the GPU (per "
     "frame). Under 16 ms keeps up with a 60 Hz display.",
     "Lo que costó el último trazo, por toque (un paso del pincel, todos los lados de simetría): buscar y mover los "
     "vértices, actualizar sus normales y el BVH (la estructura de selección), y enviar el cambio a la GPU (por "
     "fotograma). Menos de 16 ms sigue el ritmo de una pantalla de 60 Hz."},
    {"What the scene holds: the meshes with their topology, the BVHs, the subdivision levels kept for stepping "
     "between them, the undo history (Edit keeps up to 1 GB, dropping the oldest steps), painted textures with their "
     "layers and material channels (only painted 64x64 tiles of a channel use memory), and the GPU buffers of the "
     "visible Parts.",
     "Lo que contiene la escena: las mallas con su topología, los BVH, los niveles de subdivisión guardados para "
     "pasar entre ellos, el historial de deshacer (Editar guarda hasta 1 GB, descartando los pasos más antiguos), "
     "las texturas pintadas con sus capas y canales de material (solo los mosaicos pintados de 64x64 de un canal "
     "usan memoria), y los buffers de GPU de las Piezas visibles."},
    // --- PanelPerformanceProfile.cpp (tools/i18n/coverage.py) ---
    {"Measure this computer again (about 2 seconds): the GPU's triangle throughput and upload speed, brush speed and "
     "memory bandwidth, then pick the performance profile. Your overrides are kept.",
     "Vuelve a medir este equipo (unos 2 segundos): el rendimiento de triángulos y la velocidad de subida de la GPU, "
     "la velocidad de pincel y el ancho de banda de memoria, y luego elige el perfil de rendimiento. Tus cambios "
     "manuales se conservan."},
    {"Hold the view at the target frame rate (60 fps, or 120 on a 120 Hz display) by drawing big scenes' lighter "
     "motion proxy sooner or later while the view moves. Only what you see changes, never the model.",
     "Mantiene la vista a la frecuencia objetivo (60 fps, o 120 en una pantalla de 120 Hz) usando antes o después el "
     "proxy de movimiento más ligero de las escenas grandes mientras la vista se mueve. Solo cambia lo que ves, "
     "nunca el modelo."},
    {"SculptForge measures this computer on first launch (and when the graphics card, its driver or the version "
     "changes) and picks settings to match: how many triangles the view draws per frame, when big scenes switch to "
     "their lighter motion proxy, GPU memory and upload budgets, texture and subdivision limits, threads. Nothing is "
     "sent anywhere.",
     "SculptForge mide este equipo en el primer arranque (y cuando cambian la tarjeta gráfica, su controlador o la "
     "versión) y elige ajustes acordes: cuántos triángulos dibuja la vista por fotograma, cuándo las escenas grandes "
     "pasan a su proxy de movimiento más ligero, los presupuestos de memoria y subida de la GPU, los límites de "
     "textura y subdivisión, los hilos. No se envía nada a ningún sitio."},
    // --- PanelPhone.cpp (tools/i18n/coverage.py) ---
    {"Use your phone as SculptForge's camera: scan the QR code to send scans, photos and splat files, see the sculpt "
     "on your desk in AR, and drive a face with your own. No app to install; phone and computer on the same Wi-Fi.",
     "Usa tu teléfono como cámara de SculptForge: escanea el código QR para enviar escaneos, fotos y archivos de "
     "splats, ver la escultura en tu escritorio en RA y controlar una cara con la tuya. Sin app que instalar; "
     "teléfono y equipo en la misma Wi-Fi."},
    {"Record the live face feed into a take (Captures/<time>-face/take.jsonl, one frame of ARKit weights per line) "
     "for playback and export. Again: stop.",
     "Graba la cara en vivo en una toma (Captures/<time>-face/take.jsonl, un fotograma de pesos ARKit por línea) "
     "para reproducirla y exportarla. Otra vez: detener."},
    {"Point your phone's camera at it and tap the link: the capture page opens, paired with this window. A new code "
     "is made every time the window opens.",
     "Apunta la cámara del teléfono hacia él y toca el enlace: se abre la página de captura, emparejada con esta "
     "ventana. Se crea un código nuevo cada vez que se abre la ventana."},
    {"For a phone that can't scan: open the address in its browser and type these 6 digits. After 5 wrong tries the "
     "code changes.",
     "Para un teléfono que no puede escanear: abre la dirección en su navegador y escribe estos 6 dígitos. Tras 5 "
     "intentos fallidos el código cambia."},
    {"This computer has several network addresses. If the phone can't open the page, pick the one on the Wi-Fi the "
     "phone uses (often 192.168.x.x).",
     "Este equipo tiene varias direcciones de red. Si el teléfono no puede abrir la página, elige la de la Wi-Fi que "
     "usa el teléfono (a menudo 192.168.x.x)."},
    {"A photo sent from the phone starts Image to 3D straight away with the service chosen there (free Hugging Face "
     "by default). Off: it opens in Image to 3D and waits for Generate.",
     "Una foto enviada desde el teléfono inicia Imagen a 3D enseguida con el servicio elegido allí (Hugging Face "
     "gratuito por defecto). Desactivado: se abre en Imagen a 3D y espera a Generar."},
    {"While the phone shows the model, sculpting re-exports it (at most every few seconds); the phone offers the new "
     "version.",
     "Mientras el teléfono muestra el modelo, esculpir lo vuelve a exportar (como mucho cada pocos segundos); el "
     "teléfono ofrece la versión nueva."},
    {"Swap the left and right expressions (and the head's turn), if the character winks with the wrong eye.",
     "Intercambia las expresiones izquierda y derecha (y el giro de la cabeza), si el personaje guiña el ojo "
     "equivocado."},
    // --- PanelProject.cpp (tools/i18n/coverage.py) ---
    {"Once a day at startup, download a small file from the SculptForge website that names the newest version, and "
     "show a banner when there is a newer one. Nothing about you or your computer is sent.",
     "Una vez al día al iniciar, descarga del sitio web de SculptForge un archivo pequeño que nombra la versión más "
     "nueva, y muestra un aviso cuando hay una más nueva. No se envía nada sobre ti ni sobre tu equipo."},
    {"Update channel", "Canal de actualización"},
    {"Stable", "Estable"},
    {"Preview (betas)", "Vista previa (betas)"},
    {"Stable offers only finished releases. Preview also offers the betas the update feed names. Betas are a member "
     "perk: members get them from the private members' repository and the #members channel on Discord. When the "
     "stable release passes a beta, Preview offers that.",
     "Estable ofrece solo versiones terminadas. Vista previa ofrece también las betas que nombra el feed de "
     "actualizaciones. Las betas son una ventaja para miembros: las reciben del repositorio privado de miembros y "
     "del canal #members de Discord. Cuando la versión estable supera a una beta, Vista previa ofrece esa."},
    // --- PanelRetopo.cpp (tools/i18n/coverage.py) ---
    {"Automatic quad retopology: rebuilds the Part as clean, all-quad topology whose edge loops follow the forms, close to Target Quads. Runs in the background (Cancel stops it); undoable. Then divide it and use Part > Project > Project Detail to bring the detail back.", "Retopología automática en quads: reconstruye la Pieza como topología limpia, toda de quads, cuyos bucles de aristas siguen las formas, cerca de Quads objetivo. Se ejecuta en segundo plano (Cancelar la detiene); se puede deshacer. Luego divídela y usa Pieza > Proyectar > Project Detail para recuperar el detalle."},
    {"The Quad Remesh Guides brush: drag on the model to draw a curve for Quad Remesh's edge loops to follow, around "
     "an eye, the mouth or a joint. A curve that ends where it started becomes a loop. Symmetry mirrors it; "
     "Alt+click a curve to delete it. Pick any brush to sculpt again.",
     "El pincel Quad Remesh Guides: arrastra sobre el modelo para dibujar una curva que sigan los bucles de aristas "
     "de Quad Remesh, alrededor de un ojo, la boca o una articulación. Una curva que acaba donde empezó se vuelve un "
     "bucle. La simetría la refleja; Alt+clic en una curva la borra. Elige cualquier pincel para volver a esculpir."},
    {"Delete the last Quad Remesh guide curve drawn (with its mirror copies).",
     "Borra la última curva guía de Quad Remesh dibujada (con sus copias reflejadas)."},
    {"Delete every Quad Remesh guide curve of this Part.", "Borra todas las curvas guía de Quad Remesh de esta Pieza."},
    {"Project the shape and vertex paint of every other visible Part onto this one: each vertex moves along its "
     "normal to the nearest surface within Dist. The mask holds vertices back. Typical use: Quad Remesh a copy, "
     "divide it a few times, show the original, Project Detail.",
     "Proyecta la forma y la pintura de vértices de todas las demás Piezas visibles sobre esta: cada vértice se "
     "mueve por su normal hasta la superficie más cercana dentro de Dist. La máscara retiene vértices. Uso típico: "
     "Quad Remesh a una copia, divídela unas veces, muestra el original, Project Detail."},
    {"After a stroke that stretched the surface (edges pulled to more than 1.6 times their length), the active Part "
     "is remeshed at the Resolution above in the background. Sculpting never waits: a stroke made meanwhile remeshes "
     "again. Each remesh is one undo step. Not on a Part with subdivision levels, sculpt layers or a painted texture.",
     "Tras un trazo que estiró la superficie (aristas estiradas a más de 1,6 veces su longitud), la Pieza activa se "
     "remalla en segundo plano a la Resolución de arriba. Esculpir nunca espera: un trazo hecho mientras tanto "
     "remalla de nuevo. Cada remallado es un paso de deshacer. No en una Pieza con niveles de subdivisión, capas de "
     "escultura o una textura pintada."},
    {"Remeshing in the background. The result replaces the Part when it's done.",
     "Remallando en segundo plano. El resultado reemplaza la Pieza cuando termina."},
    {"Stop Quad Remesh; the Part stays as it is.", "Detiene Quad Remesh; la Pieza se queda como está."},
    {"Quad Remesh 2 (default): a curvature-aligned layout solved as one integer grid over the whole model, so poles "
     "appear only where the form needs them and edge loops run unbroken; open guide curves become exact edge loops. "
     "Legacy: the method of SculptForge 0.9 (more poles, a split look).",
     "Quad Remesh 2 (por defecto): una distribución alineada con la curvatura resuelta como una sola rejilla entera "
     "sobre todo el modelo, así los polos aparecen solo donde la forma los necesita y los bucles de aristas corren "
     "sin cortes; las curvas guía abiertas se vuelven bucles de aristas exactos. Heredado: el método de SculptForge "
     "0.9 (más polos, un aspecto partido)."},
    {"About how many quads Quad Remesh makes (Ctrl+click to type a number). 50% / 100% / 200% set it from the Part's current polygon count.", "Más o menos cuántos quads hace Quad Remesh (Ctrl+clic para escribir un número). 50% / 100% / 200% lo fijan a partir del número de polígonos actual de la Pieza."},
    {"Target half the Part's current polygon count.", "Objetivo: la mitad de los polígonos actuales de la Pieza."},
    {"Target the Part's current polygon count (to clean up topology without changing its density).",
     "Objetivo: los polígonos actuales de la Pieza (para limpiar la topología sin cambiar su densidad)."},
    {"Target twice the Part's current polygon count.", "Objetivo: el doble de los polígonos actuales de la Pieza."},
    {"0: quads all about the same size. Higher: smaller quads where the surface curves, bigger ones on flat areas "
     "(the count stays near the target).",
     "0: quads todos más o menos del mismo tamaño. Más alto: quads más pequeños donde la superficie se curva, más "
     "grandes en las zonas planas (el número se queda cerca del objetivo)."},
    {"Edge loops follow the face group borders exactly and the result keeps the face groups. Paint groups where you "
     "want loops (around the eyes, the mouth) to guide the flow.",
     "Los bucles de aristas siguen exactamente los bordes de los grupos de caras y el resultado conserva los grupos "
     "de caras. Pinta grupos donde quieras bucles (alrededor de los ojos, la boca) para guiar el flujo."},
    {"Hard edges get an edge loop, so they stay sharp (Quad Remesh 2: faces meeting at more than about 60 degrees "
     "along a line at least a few quads long; legacy: more than 50 degrees).",
     "Los bordes duros reciben un bucle de aristas, así siguen afilados (Quad Remesh 2: caras que se juntan a más de "
     "unos 60 grados a lo largo de una línea de al menos unos pocos quads; heredado: más de 50 grados)."},
    {"Paint where you want density: red areas get up to 4 times the quads, blue areas down to a quarter, white is "
     "unchanged.",
     "Pinta donde quieras densidad: las zonas rojas reciben hasta 4 veces los quads, las azules hasta una cuarta "
     "parte, el blanco no cambia."},
    {"Keep the Part as it was, hidden, right below the new one (\"<name> Original\"), ready to Project Detail its "
     "detail back. Off: replace it (undo brings it back).",
     "Conserva la Pieza como estaba, oculta, justo debajo de la nueva (\"<name> Original\"), lista para recuperar su "
     "detalle con Project Detail. Desactivado: la reemplaza (deshacer la recupera)."},
    {"How clean the new topology is: poles are vertices with other than four edges (fewer is cleaner), corners off "
     "square measure how far quad corners are from 90 degrees, and the distance is how far the new surface strays "
     "from the sculpt (as a percentage of its size; Project Detail brings fine detail back).",
     "Lo limpia que es la topología nueva: los polos son vértices con un número de aristas distinto de cuatro (menos "
     "es más limpio), esquinas no rectas mide cuánto se alejan de 90 grados las esquinas de los quads, y la "
     "distancia es cuánto se aparta la superficie nueva de la escultura (en porcentaje de su tamaño; Project Detail "
     "recupera el detalle fino)."},
    {"How far around each guide curve the edge flow follows it: 0 = only the edge loop on the curve, 100 = about six "
     "quads to each side. The loop on the curve is always made.",
     "Hasta dónde alrededor de cada curva guía la sigue el flujo de aristas: 0 = solo el bucle sobre la curva, 100 = "
     "unos seis quads a cada lado. El bucle sobre la curva se hace siempre."},
    {"How far each vertex looks for the source surface, along its normal, as a fraction of the Part's size. Raise it "
     "if parts don't pick up the detail; lower it where surfaces are close together.",
     "Hasta dónde busca cada vértice la superficie de origen, por su normal, como fracción del tamaño de la Pieza. "
     "Súbelo si hay partes que no toman el detalle; bájalo donde las superficies están muy juntas."},
    {"Smooth the projected result this many times (softens noise from the source).",
     "Suaviza el resultado proyectado estas veces (atenúa el ruido del origen)."},
    {"Where the source is found on both sides of a vertex, settle halfway between instead of on the nearest.",
     "Donde el origen está a ambos lados de un vértice, se queda a medio camino en vez de en el más cercano."},
    {"Project the shape (into the active sculpt layer).", "Proyecta la forma (en la capa de escultura activa)."},
    {"Vertex Paint", "Pintura de vértices"},
    {"Project the vertex paint (into the active paint layer) when a source has any.",
     "Proyecta la pintura de vértices (en la capa de pintura activa) cuando un origen la tiene."},
    // --- PanelSculptable.cpp (tools/i18n/coverage.py) ---
    {"Turn the capture into a clean, sculptable Part with its colours, in one go: a watertight surface (floaters "
     "dropped, holes filled), quad topology at the chosen polygon budget with edge loops round the eyes and mouth "
     "when it is a face, and vertex paint or a texture. The splats stay (hidden). Runs in the background; Cancel "
     "stops it.",
     "Convierte la captura en una Pieza limpia y esculpible con sus colores, de una vez: una superficie estanca (sin "
     "flotantes, agujeros rellenos), topología de quads con el presupuesto de polígonos elegido, con bucles de "
     "aristas alrededor de ojos y boca cuando es una cara, y pintura de vértices o una textura. Los splats se quedan "
     "(ocultos). Se ejecuta en segundo plano; Cancelar lo detiene."},
    {"Quad Remesh the scan along the face guides you checked, then colour it: the sculptable Part appears next to the "
     "scan (which is hidden).",
     "Aplica Quad Remesh al escaneo siguiendo las guías de cara que revisaste y luego lo colorea: la Pieza "
     "esculpible aparece junto al escaneo (que se oculta)."},
    {"Place the face landmarks yourself, one click each: the eye corners, the nose tip, the mouth corners and the "
     "chin (the panel says which is next). For faces the automatic search misses.",
     "Coloca tú los puntos de referencia de la cara, un clic cada uno: las comisuras de los ojos, la punta de la "
     "nariz, las comisuras de la boca y la barbilla (el panel dice cuál va después). Para caras que la búsqueda "
     "automática no encuentra."},
    {"Only the splats inside the box count: drag the box's face handles in the viewport to cut away the ground, the "
     "background or a stand.",
     "Solo cuentan los splats dentro de la caja: arrastra en la vista los tiradores de las caras de la caja para "
     "recortar el suelo, el fondo o un soporte."},
    {"Working in the background; the new Part appears when it's done.",
     "Trabajando en segundo plano; la Pieza nueva aparece cuando termina."},
    {"Stop Make sculptable; nothing changes.", "Detiene Hacer esculpible; no cambia nada."},
    {"Leave the review without remeshing (the scan Part stays as it is).",
     "Sale de la revisión sin remallar (la Pieza del escaneo se queda como está)."},
    {"Look for the face again from the current view (turn the view to face it first).",
     "Busca la cara de nuevo desde la vista actual (gira antes la vista para tenerla de frente)."},
    {"How finely the surface follows the capture: voxels across the subject. Higher keeps thinner parts and more "
     "detail but takes longer (about 5 s at 384 for a million splats).",
     "Lo fino que la superficie sigue a la captura: vóxeles a lo ancho del sujeto. Más alto conserva partes más "
     "finas y más detalle pero tarda más (unos 5 s a 384 para un millón de splats)."},
    {"When the capture is a head, find the eyes, nose, mouth and chin as seen from the current view and give Quad "
     "Remesh edge loops round the eyes and mouth and along the nasolabial folds, mirrored across the face (animation "
     "topology). Turn the view to face it first.",
     "Cuando la captura es una cabeza, busca los ojos, la nariz, la boca y la barbilla tal como se ven desde la "
     "vista actual y da a Quad Remesh bucles de aristas alrededor de ojos y boca y a lo largo de los surcos "
     "nasolabiales, reflejados en la cara (topología para animación). Gira antes la vista para tenerla de frente."},
    {"Stop after the scan to check the face landmarks and the loops: drag a dot to move it, or place them by hand, "
     "then Finish.",
     "Se detiene tras el escaneo para revisar los puntos de referencia de la cara y los bucles: arrastra un punto "
     "para moverlo, o colócalos a mano, y luego Terminar."},
    {"The capture's colours go into the vertices (vertex paint): more polygons, more colour detail.",
     "Los colores de la captura van a los vértices (pintura de vértices): más polígonos, más detalle de color."},
    {"Auto UV the result and bake the capture's colours into a 2K texture (sharp colour on a light mesh, for games "
     "and exports).",
     "UV automáticas para el resultado y hornea los colores de la captura en una textura 2K (color nítido en una "
     "malla ligera, para juegos y exportaciones)."},
    {"Keep the dense voxel surface as a hidden \"<name> Scan\" Part, to Project Detail more detail from later.",
     "Conserva la superficie densa de vóxeles como una Pieza oculta \"<name> Scan\", para recuperar más detalle con "
     "Project Detail después."},
    {"Splats fainter than this are ignored (haze and floaters are usually faint).",
     "Los splats más tenues que esto se ignoran (la neblina y los flotantes suelen ser tenues)."},
    {"Seal gaps up to this many voxels between splats.", "Sella huecos de hasta estos vóxeles entre splats."},
    {"Fill big holes such as the open bottom of a subject standing on a table (space boxed in on five sides counts as "
     "inside). Turn off for open cavities like a cup.",
     "Rellena agujeros grandes como la base abierta de un sujeto apoyado en una mesa (el espacio cerrado por cinco "
     "lados cuenta como interior). Desactívalo para cavidades abiertas como una taza."},
    {"Keep every solid piece instead of only the largest (floaters come back too).",
     "Conserva cada pieza sólida en vez de solo la más grande (los flotantes también vuelven)."},
    // --- PanelSelectionSets.cpp (tools/i18n/coverage.py) ---
    {"Set name", "Nombre"},
    {"Name of the set Store makes; storing again with the same name replaces it.",
     "Nombre del conjunto que crea Guardar; guardar de nuevo con el mismo nombre lo reemplaza."},
    {"Store", "Guardar"},
    {"Store the checked channels of this Part as a named set (one undo step). Sets restore at any subdivision level "
     "and are saved in the project.",
     "Guarda los canales marcados de esta Pieza como conjunto con nombre (un paso de deshacer). Los conjuntos se "
     "restauran en cualquier nivel de subdivisión y se guardan en el proyecto."},
    {"From Visible", "De lo visible"},
    {"Quick set: store the faces shown right now (Visible, Visible 2, ...).",
     "Conjunto rápido: guarda las caras que se ven ahora mismo (Visible, Visible 2, ...)."},
    {"Restore mode", "Modo de restaurar"},
    {"How Restore meets the current mask and visibility: Replace them, or Union, Intersect or Subtract the set's. "
     "Combine uses the same mode (not Replace) to make a new set from two.",
     "Cómo se combina Restaurar con la máscara y la visibilidad actuales: Reemplazarlas, o Unir, Intersecar o Restar "
     "las del conjunto. Combinar usa el mismo modo (no Reemplazar) para crear un conjunto nuevo a partir de dos."},
    {"No sets yet: Store saves one.", "Aún no hay conjuntos: Guardar crea uno."},
    {"Put the selected set's channels back (not its shape: Morph to Set does that), carried to the current "
     "subdivision level. One undo step.",
     "Recupera los canales del conjunto seleccionado (no su forma: eso lo hace Morph al conjunto), llevados al nivel "
     "de subdivisión actual. Un paso de deshacer."},
    {"Delete the selected set (undo brings it back).", "Borra el conjunto seleccionado (deshacer lo recupera)."},
    {"Combine", "Combinar"},
    {"Make a new set from the selected one and this one by their mask and visibility, with the mode above (Union, "
     "Intersect or Subtract).",
     "Crea un conjunto nuevo a partir del seleccionado y este según su máscara y visibilidad, con el modo de arriba "
     "(Unir, Intersecar o Restar)."},
    {"Clear Morph Target", "Quitar objetivo de Morph"},
    {"Morph Target", "Objetivo de Morph"},
    {"Make this set's shape the Morph brush's target and pick the Morph brush: strokes pull the surface back toward "
     "the stored shape (Alt pushes away), at any subdivision level.",
     "Usa la forma de este conjunto como objetivo del pincel Morph y elige el pincel Morph: los trazos devuelven la "
     "superficie hacia la forma guardada (Alt la aleja), en cualquier nivel de subdivisión."},
    {"Morph to Set", "Morph al conjunto"},
    {"Move the whole surface toward the set's shape by the amount below, within the mask. One undo step.",
     "Mueve toda la superficie hacia la forma del conjunto en la cantidad de abajo, dentro de la máscara. Un paso de "
     "deshacer."},
    {"Amount %.2f", "Cantidad %.2f"},
    {"Store with Shape checked to morph toward a set.", "Guarda con Forma marcada para transformar hacia un conjunto."},
    // --- PanelSplatTrain.cpp (tools/i18n/coverage.py) ---
    {"Train a Gaussian splat from a video of an object (walk slowly all the way around it). The sharpest frame of "
     "each moment is used. The splat shows in the viewport after a few hundred steps and sharpens live.",
     "Entrena un splat gaussiano a partir de un vídeo de un objeto (rodéalo despacio por completo). Se usa el "
     "fotograma más nítido de cada momento. El splat aparece en la vista tras unos cientos de pasos y se afina en "
     "vivo."},
    {"Train a Gaussian splat from a folder: photos taken all around an object, a phone-link capture, or a COLMAP / "
     "nerfstudio dataset (which already has camera positions).",
     "Entrena un splat gaussiano a partir de una carpeta: fotos tomadas alrededor de un objeto, una captura del "
     "enlace del teléfono o un conjunto de datos COLMAP / nerfstudio (que ya tiene las posiciones de cámara)."},
    {"Stop training now and keep what it has learned (the newest checkpoint) as the result.",
     "Detiene el entrenamiento ahora y conserva lo aprendido (el punto de control más reciente) como resultado."},
    {"Stop training. Nothing is saved; the last preview stays in the viewport until you Clear the splats.",
     "Detiene el entrenamiento. No se guarda nada; la última vista previa se queda en la vista hasta que Borres los "
     "splats."},
    {"Use a Brush trainer you already have (brush_app): for example one you built yourself with cargo on Linux ARM, "
     "where there is no ready-made download.",
     "Usa un entrenador Brush que ya tengas (brush_app): por ejemplo uno que compilaste tú con cargo en Linux ARM, "
     "donde no hay descarga lista."},
    {"Use a COLMAP program you already have (it finds where each photo was taken).",
     "Usa un programa COLMAP que ya tengas (averigua dónde se tomó cada foto)."},
    {"Download it once from its GitHub release; nothing else is sent.",
     "Lo descarga una vez desde su versión en GitHub; no se envía nada más."},
    // --- PanelSupportCard.cpp (tools/i18n/coverage.py) ---
    {"I'm already a member", "Ya soy miembro"},
    {"Signs in with GitHub, as member builds do (Preferences > Updates): no more membership cards, and the member "
     "builds are on.",
     "Inicia sesión con GitHub, como las versiones para miembros (Preferencias > Actualizaciones): no más tarjetas "
     "de membresía, y las versiones para miembros quedan activadas."},
    // --- PanelTexture.cpp (tools/i18n/coverage.py) ---
    {"Paint into the vertices (vertex paint): the colour resolution is the mesh's. Shown when the Part has a texture "
     "(Tool > Texture Image).",
     "Pinta en los vértices (pintura de vértices): la resolución del color es la de la malla. Se muestra cuando la "
     "Pieza tiene textura (Herramienta > Imagen de textura)."},
    {"Paint into the Part's texture: the colour resolution comes from the texture, not the mesh. The Paint brush, Rgb "
     "sculpting, fills and Paint Layers all work on it.",
     "Pinta en la textura de la Pieza: la resolución del color viene de la textura, no de la malla. El pincel Paint, "
     "la escultura con Rgb, los rellenos y las Capas de pintura funcionan sobre ella."},
    {"Start texture painting: give the Part a texture (the size chosen in Tool > Texture Image, 2K by default) so paint "
     "goes into texels instead of vertices, and the detail comes from the texture, not the mesh. Without UVs, Auto "
     "UV unwraps the model first (in the background, with its progress in the status bar).",
     "Empieza a pintar texturas: da a la Pieza una textura (del tamaño elegido en Herramienta > Imagen de textura, 2K "
     "por defecto) para que la pintura vaya a los téxeles en vez de a los vértices, y el detalle venga de la "
     "textura, no de la malla. Sin UV, UV automáticas desenvuelve antes el modelo (en segundo plano, con su progreso "
     "en la barra de estado)."},
    {"Show the painted texture on the model (multiplied over the material, like vertex paint).",
     "Muestra la textura pintada en el modelo (multiplicada sobre el material, como la pintura de vértices)."},
    {"Unwrap the Part automatically: cut it into UV islands and pack them, so any sculpt can carry a texture. "
     "Replaces existing UVs (undoable). Runs in the background; big meshes can take a minute or more.",
     "Desenvuelve la Pieza automáticamente: la corta en islas UV y las empaqueta, para que cualquier escultura pueda "
     "llevar textura. Reemplaza las UV existentes (se puede deshacer). Se ejecuta en segundo plano; las mallas "
     "grandes pueden tardar un minuto o más."},
    {"Give the Part a texture of the chosen size to paint into (white). Needs UVs: Auto UV makes them. Replaces an "
     "existing texture (undoable).",
     "Da a la Pieza una textura del tamaño elegido para pintar (blanca). Necesita UV: UV automáticas las crea. "
     "Reemplaza una textura existente (se puede deshacer)."},
    {"Show the texture with the UV layout drawn over it, below these buttons.",
     "Muestra la textura con la distribución UV dibujada encima, bajo estos botones."},
    {"Bake the vertex paint (all paint layers) into the active texture layer. Makes a 2K texture if there is none.",
     "Hornea la pintura de vértices (todas las capas de pintura) en la capa de textura activa. Crea una textura 2K "
     "si no hay ninguna."},
    {"Copy the texture into the vertex paint of every vertex (detail finer than the mesh is lost).",
     "Copia la textura en la pintura de vértices de cada vértice (se pierde el detalle más fino que la malla)."},
    {"Save the texture (every visible layer composited) as a PNG image.",
     "Guarda la textura (todas las capas visibles combinadas) como imagen PNG."},
    {"Load an image as the Part's texture (replacing its layers; undoable).",
     "Carga una imagen como textura de la Pieza (reemplaza sus capas; se puede deshacer)."},
    {"Remove the Part's texture (undoable). Painting goes back to the vertices.",
     "Quita la textura de la Pieza (se puede deshacer). La pintura vuelve a los vértices."},
    {"Voxel Remesh and Quad Remesh keep a painted texture: the new mesh gets Auto UVs and the texture (every layer) "
     "is projected onto it from the old surface, in the same undo step. Off: the texture is dropped. Merging Parts "
     "keeps textures too. Shown under Remesh and Quad Remesh when the Part has a texture.",
     "Remallado vóxel y Quad Remesh conservan una textura pintada: la malla nueva recibe UV automáticas y la "
     "textura (todas las capas) se proyecta sobre ella desde la superficie anterior, en el mismo paso de deshacer. "
     "Desactivado: la textura se descarta. Unir Piezas también conserva las texturas. Se muestra bajo Remallado y "
     "Quad Remesh cuando la Pieza tiene textura."},
    {"Keep the painted texture: the new mesh gets Auto UVs and the texture (every layer) is projected onto it from "
     "the old surface, in the same undo step (it adds the Auto UV time). Off: the texture is dropped. Scripts: "
     "keep_texture=false.",
     "Conserva la textura pintada: la malla nueva recibe UV automáticas y la textura (todas las capas) se proyecta "
     "sobre ella desde la superficie anterior, en el mismo paso de deshacer (suma el tiempo de UV automáticas). "
     "Desactivado: la textura se descarta. Scripts: keep_texture=false."},
    {"Unwrapping the Part in the background; you can keep working.",
     "Desenvolviendo la Pieza en segundo plano; puedes seguir trabajando."},
    {"Size of a new texture: 1K, 2K (the default), 4K or 8K texels square. Bigger holds finer paint and uses more "
     "memory (8K: 256 MB per layer).",
     "Tamaño de una textura nueva: 1K, 2K (por defecto), 4K u 8K téxeles de lado. Más grande admite pintura más fina "
     "y usa más memoria (8K: 256 MB por capa)."},
    {"The texture with the Part's UV layout over it (island borders on big meshes). Paint on the model; the texture "
     "fills in here.",
     "La textura con la distribución UV de la Pieza encima (bordes de islas en mallas grandes). Pinta en el modelo; "
     "la textura se rellena aquí."},
    // --- PanelTimeline.cpp (tools/i18n/coverage.py) ---
    {"Show or hide the history strip along the top or bottom of the canvas (Preferences > Interface > Timeline "
     "position): every stroke and operation as a tick. Drag to see the model at any point, click to go there; "
     "sculpting from an earlier point keeps the old continuation as a branch. Right-click a stroke to change its "
     "strength.",
     "Muestra u oculta la tira de historial arriba o abajo del lienzo (Preferencias > Interfaz > Posición de la "
     "línea de tiempo): cada trazo y operación como una marca. Arrastra para ver el modelo en cualquier punto, haz "
     "clic para ir allí; esculpir desde un punto anterior conserva la continuación anterior como rama. Clic derecho "
     "en un trazo para cambiar su fuerza."},
    {"Timeline position: the Sculpt Timeline strip runs along the top of the viewport, below the top shelf and away "
     "from the brushes on the bottom shelf (the default). Saved automatically.",
     "Posición de la línea de tiempo: la tira de la Línea de tiempo de esculpido va por arriba de la vista, bajo el "
     "estante superior y lejos de los pinceles del estante inferior (por defecto). Se guarda automáticamente."},
    {"Timeline position: the Sculpt Timeline strip runs along the bottom of the viewport, above the brush shelf. "
     "Saved automatically.",
     "Posición de la línea de tiempo: la tira de la Línea de tiempo de esculpido va por abajo de la vista, encima "
     "del estante de pinceles. Se guarda automáticamente."},
    {"Render the history of the active Part as a turntable timelapse video (1080p or 4K MP4, no captions).",
     "Renderiza el historial de la Pieza activa como vídeo timelapse en plataforma giratoria (MP4 1080p o 4K, sin "
     "subtítulos)."},
    {"Measure how responsive sculpting is on this computer: the standard scene (a 15 or 33 million triangle head; "
     "Clay small, medium and large, Move, Smooth and a 240 px brush) through the same code a pen uses, with a live "
     "display of frame times and how far the stroke trails the pointer. Writes a video and a JSON result you can "
     "share. Replaces the scene.",
     "Mide lo ágil que es esculpir en este equipo: la escena estándar (una cabeza de 15 o 33 millones de triángulos; "
     "Clay pequeño, mediano y grande, Move, Smooth y un pincel de 240 px) por el mismo código que usa un lápiz, con "
     "una muestra en vivo de los tiempos de fotograma y de cuánto se retrasa el trazo respecto al puntero. Escribe "
     "un vídeo y un resultado JSON que puedes compartir. Reemplaza la escena."},
    {"Open the history strip.", "Abre la tira de historial."},
    {"Also save the history beside the project as <name>.sfhist (strokes, masks, vertex paint, visibility; a topology "
     "change cuts it). Opening the project brings it back.",
     "Guarda también el historial junto al proyecto como <name>.sfhist (trazos, máscaras, pintura de vértices, "
     "visibilidad; un cambio de topología lo corta). Abrir el proyecto lo recupera."},
    {"Render this history as a turntable timelapse video.",
     "Renderiza este historial como vídeo timelapse en plataforma giratoria."},
    {"Fold the timeline into a tab.", "Pliega la línea de tiempo en una pestaña."},
    {"How strongly this stroke shows: 1 = as sculpted, 0 = gone, 2 = twice as strong. Later strokes are not redone: "
     "where they overlap this one they keep their own displacement on top.",
     "Cuánto se ve este trazo: 1 = tal como se esculpió, 0 = nada, 2 = el doble de fuerte. Los trazos posteriores no "
     "se rehacen: donde se solapan con este conservan su propio desplazamiento encima."},
    // --- PanelUpdate.cpp (tools/i18n/coverage.py) ---
    {"Downloading the update...", "Descargando la actualización..."},
    {"It installs when you quit.", "Se instala al salir."},
    {"Restart now", "Reiniciar ahora"},
    {"Install it when you quit SculptForge; the next start is the new version.",
     "Se instala cuando sales de SculptForge; el próximo inicio es la versión nueva."},
    {"Update banner", "Aviso de actualización"},
    {"Starting SculptForge %s...", "Iniciando SculptForge %s..."},
    {"The update didn't work: %s.", "La actualización no funcionó: %s."},
    {"Nothing was changed. Download it from the website instead, or try again.",
     "No se cambió nada. Descárgala desde el sitio web o inténtalo de nuevo."},
    {"Update with your package manager (%s)", "Actualiza con tu gestor de paquetes (%s)"},
    {"Copy", "Copiar"},
    {"Download the new package from the website and open it.",
     "Descarga el paquete nuevo desde el sitio web y ábrelo."},
    {"Try again", "Reintentar"},
    {"Update", "Actualizar"},
    {"Download the new version from SculptForge's GitHub releases, check its signature and checksum, and install it. "
     "The current version is kept until the new one has started.",
     "Descarga la versión nueva de las versiones de SculptForge en GitHub, comprueba su firma y su suma de "
     "verificación, y la instala. La versión actual se conserva hasta que arranca la nueva."},
    {"Asking GitHub...", "Preguntando a GitHub..."},
    {"On GitHub, enter this code:", "En GitHub, escribe este código:"},
    {"Open github.com/login/device", "Abrir github.com/login/device"},
    {"Waiting for GitHub...", "Esperando a GitHub..."},
    {"Member builds", "Versiones de miembros"},
    {"Member and Studio members get each release early. Sign in with the GitHub account that has the invitation to "
     "the member repository: SculptForge then also offers the member builds, checked with the same signature as "
     "every release. The sign-in can only read that repository's releases; it is kept in your system's keychain and "
     "Sign out removes it.",
     "Los miembros Member y Studio reciben cada versión antes. Inicia sesión con la cuenta de GitHub que tiene la "
     "invitación al repositorio de miembros: SculptForge ofrece entonces también las versiones de miembros, "
     "comprobadas con la misma firma que cada versión. El inicio de sesión solo puede leer las versiones de ese "
     "repositorio; se guarda en el llavero de tu sistema y Cerrar sesión lo quita."},
    {"Signed in with GitHub.", "Sesión iniciada con GitHub."},
    {"Signed in as %s on GitHub.", "Sesión iniciada como %s en GitHub."},
    {"Sign out", "Cerrar sesión"},
    {"Sign in with GitHub", "Iniciar sesión con GitHub"},
    // --- PanelUvEditor.cpp (tools/i18n/coverage.py) ---
    {"UV Editor", "Editor UV"},
    {"Open the UV Editor: the Part's UV layout in 2D over the UDIM tiles. Select vertices, edges, faces or islands "
     "(here or on the model), move, rotate and scale them, mark seams, unwrap, pack, set texel density and lay "
     "islands into trim sheet strips. Stretch and texel density show live on the model while you edit.",
     "Abre el Editor UV: la distribución UV de la Pieza en 2D sobre los mosaicos UDIM. Selecciona vértices, aristas, "
     "caras o islas (aquí o en el modelo), muévelos, rótalos y escálalos, marca costuras, desenvuelve, empaqueta, "
     "fija la densidad de téxeles y coloca islas en franjas de trim sheet. El estiramiento y la densidad de téxeles "
     "se ven en vivo en el modelo mientras editas."},
    {"Select UV vertices, edges, faces or whole islands (1-4 while the pointer is over the UV view). The selection "
     "converts when you switch.",
     "Selecciona vértices, aristas, caras o islas UV enteras (1-4 con el puntero sobre la vista UV). La selección se "
     "convierte al cambiar."},
    {"Unwrap", "Desenvolver"},
    {"Unwrap with the chosen method: the selected faces (fitted where they were), or the whole Part (packed). Least "
     "Stretch keeps angles and lengths along the marked seams; Conformal keeps angles; By Face Groups cuts along the "
     "face group borders too; Planar projects each piece; Unfold flattens the current islands again in place, "
     "keeping pins; Auto Seams finds its own. Closed pieces get cut automatically. Runs in the background (Cancel "
     "under the view stops it).",
     "Desenvuelve con el método elegido: las caras seleccionadas (ajustadas donde estaban), o toda la Pieza "
     "(empaquetada). Least Stretch conserva ángulos y longitudes a lo largo de las costuras marcadas; Conformal "
     "conserva ángulos; By Face Groups corta también por los bordes de grupos de caras; Planar proyecta cada pieza; "
     "Unfold vuelve a aplanar las islas actuales en su sitio, respetando los anclajes; Auto Seams busca sus propias "
     "costuras. Las piezas cerradas se cortan automáticamente. Se ejecuta en segundo plano (Cancelar, bajo la vista, lo detiene)."},
    {"Pack", "Empaquetar"},
    {"Pack the selected islands (or all) as large as they fit into the 0-1 tile, or across the Columns x Rows grid of "
     "UDIM tiles: turned to their smallest box, scaled to one texel density, with the margin between them. "
     "Runs in the background (Cancel under the view stops it).",
     "Empaqueta las islas seleccionadas (o todas) lo más grandes que quepan en el mosaico 0-1, o en la rejilla "
     "Columnas x Filas de mosaicos UDIM: giradas a su caja más pequeña, escaladas a una densidad de téxeles, con el "
     "margen entre ellas. Se ejecuta en segundo plano (Cancelar, bajo la vista, lo detiene)."},
    {"Packing...", "Empaquetando..."},
    {"Packing: %d%%", "Empaquetando: %d%%"},
    {"Unwrapping...", "Desenvolviendo..."},
    {"Unwrapping: %d%%", "Desenvolviendo: %d%%"},
    {"Stop the Unwrap or Pack that is running; the UVs stay as they are.", "Detiene el Desenvolver o Empaquetar en curso; las UV se quedan como están."},
    {"Mark Seam", "Marcar costura"},
    {"Mark the selected edges (or the border of the selected faces) as seams: Unwrap cuts there.",
     "Marca como costuras las aristas seleccionadas (o el borde de las caras seleccionadas): Desenvolver corta ahí."},
    {"Clear Seam", "Quitar costura"},
    {"Clear the seams on the selected edges (or the border of the selected faces).",
     "Quita las costuras de las aristas seleccionadas (o del borde de las caras seleccionadas)."},
    {"Mark every face group border as a seam.", "Marca como costura cada borde de grupo de caras."},
    {"Island Borders", "Bordes de islas"},
    {"Mark every current UV island border as a seam (keeps an imported layout's cuts for Unwrap).",
     "Marca como costura cada borde de isla UV actual (conserva los cortes de una distribución importada para "
     "Desenvolver)."},
    {"Sharp Edges", "Aristas duras"},
    {"Mark every edge sharper than the angle as a seam (hard-surface models).",
     "Marca como costura cada arista más aguda que el ángulo (modelos hard-surface)."},
    {"Clear All", "Quitar todas"},
    {"Clear every marked seam.", "Quita todas las costuras marcadas."},
    {"Seam Tool", "Costuras"},
    {"Draw seams on the model: drag along the edges you want cut (each stretch follows the shortest edge path); "
     "Alt+drag clears them. Seams show in red on the model while the UV Editor is open.",
     "Dibuja costuras en el modelo: arrastra por las aristas que quieres cortar (cada tramo sigue el camino de "
     "aristas más corto); Alt+arrastrar las quita. Las costuras se ven en rojo en el modelo mientras el Editor UV "
     "está abierto."},
    {"Select in 3D", "Elegir en 3D"},
    {"Select on the model: click or drag over it to select faces, islands, vertices or edges (the UV Editor's mode); "
     "Alt removes. The selection shows in both views.",
     "Selecciona en el modelo: haz clic o arrastra sobre él para seleccionar caras, islas, vértices o aristas (el "
     "modo del Editor UV); Alt quita. La selección se ve en las dos vistas."},
    {"Pin", "Anclar"},
    {"Pin the selected UVs: Unfold and Unwrap keep them where they are (P over the UV view).",
     "Ancla las UV seleccionadas: Unfold y Desenvolver las dejan donde están (P sobre la vista UV)."},
    {"Unpin", "Desanclar"},
    {"Unpin the selected UVs (Alt+P over the UV view).", "Desancla las UV seleccionadas (Alt+P sobre la vista UV)."},
    {"Weld", "Soldar"},
    {"Weld the selected vertices: where they meet across a seam they become one UV each (the seam goes). W over the "
     "UV view.",
     "Suelda los vértices seleccionados: donde se encuentran a través de una costura pasan a ser una sola UV cada "
     "uno (la costura desaparece). W sobre la vista UV."},
    {"Split", "Separar"},
    {"Split the selection off: the selected edges, or the border of the selected faces, become seams so the faces "
     "move on their own (Y over the UV view).",
     "Separa la selección: las aristas seleccionadas, o el borde de las caras seleccionadas, pasan a ser costuras "
     "para que las caras se muevan por su cuenta (Y sobre la vista UV)."},
    {"Straighten", "Enderezar"},
    {"Line the selected vertices up on one horizontal or vertical line (an edge loop, a border).",
     "Alinea los vértices seleccionados en una línea horizontal o vertical (un bucle de aristas, un borde)."},
    {"Flip U", "Voltear U"},
    {"Mirror the selected islands (or all) left to right.",
     "Refleja las islas seleccionadas (o todas) de izquierda a derecha."},
    {"Flip V", "Voltear V"},
    {"Mirror the selected islands (or all) top to bottom.",
     "Refleja las islas seleccionadas (o todas) de arriba abajo."},
    {"Rotate 90", "Rotar 90"},
    {"Turn the selection (or everything) a quarter turn counter-clockwise.",
     "Gira la selección (o todo) un cuarto de vuelta en sentido antihorario."},
    {"Set", "Fijar"},
    {"Scale the selected islands (or all) to the target texel density.",
     "Escala las islas seleccionadas (o todas) a la densidad de téxeles objetivo."},
    {"Match", "Igualar"},
    {"Give the selected islands (or all) one texel density: their mean, so their sizes match on the surface.",
     "Da a las islas seleccionadas (o a todas) una sola densidad de téxeles: su media, para que sus tamaños "
     "coincidan en la superficie."},
    {"Pick", "Tomar"},
    {"Take the target texel density from the selected islands (or the whole Part).",
     "Toma la densidad de téxeles objetivo de las islas seleccionadas (o de toda la Pieza)."},
    {"Pack at Density", "Empaquetar a densidad"},
    {"Pack at exactly the target texel density, using as many UDIM tiles as it takes (1001, 1002, ..., Columns wide, "
     "then the next row up): every island keeps the same texels per unit across the tiles.",
     "Empaqueta exactamente a la densidad de téxeles objetivo, usando tantos mosaicos UDIM como haga falta (1001, "
     "1002, ..., Columnas de ancho, luego la fila siguiente hacia arriba): cada isla conserva los mismos téxeles por "
     "unidad en todos los mosaicos."},
    {"Assign to Strip", "Asignar a franja"},
    {"Lay the selected islands into the chosen trim sheet strip: turned lengthwise, scaled to the strip's height and "
     "lined up along u. The texture stays put, so the islands pick up the strip's look.",
     "Coloca las islas seleccionadas en la franja de trim sheet elegida: giradas a lo largo, escaladas a la altura "
     "de la franja y alineadas a lo largo de u. La textura no se mueve, así que las islas toman el aspecto de la "
     "franja."},
    {"Fit the selection (or the whole layout) in the UV view (F over it).",
     "Encuadra la selección (o toda la distribución) en la vista UV (F sobre ella)."},
    {"What the faces show, here and on the model: Islands (a colour each), Stretch (green: true to the surface; "
     "yellow to red: stretched or squashed; magenta: folded over), Density (texel density; blue: fewer texels than "
     "the mean, red: more) or Plain.",
     "Qué muestran las caras, aquí y en el modelo: Islas (un color cada una), Estiramiento (verde: fiel a la "
     "superficie; de amarillo a rojo: estirado o aplastado; magenta: doblado), Densidad (densidad de téxeles; azul: "
     "menos téxeles que la media, rojo: más) o Simple."},
    {"Behind the layout: nothing, a checker, or the painted texture (tile 1001).",
     "Detrás de la distribución: nada, un damero o la textura pintada (mosaico 1001)."},
    {"Colours on the model", "Colores en el modelo"},
    {"Show the same colours on the model (islands, stretch, density and the selection), live while you edit.",
     "Muestra los mismos colores en el modelo (islas, estiramiento, densidad y la selección), en vivo mientras "
     "editas."},
    {"Checker", "Damero"},
    {"Checker on the model", "Damero en el modelo"},
    {"A checker on the model's UVs (tinted per UDIM tile): squares that stay square mean no stretch.",
     "Un damero sobre las UV del modelo (teñido por mosaico UDIM): los cuadrados que siguen cuadrados indican que no "
     "hay estiramiento."},
    {"Click to select (Shift adds, Ctrl removes), drag a box; drag the box handles to move, scale (corners) or rotate "
     "(the knob). Keys over the view: 1-4 modes, A all, Alt+A none, Ctrl+I invert, G / R / S move, rotate, scale (X "
     "/ Y lock an axis, Ctrl snaps), F frame, P / Alt+P pin, W weld, Y split. Wheel zooms, right or middle drag pans.",
     "Clic para seleccionar (Mayús añade, Ctrl quita), arrastra una caja; arrastra los tiradores de la caja para "
     "mover, escalar (esquinas) o rotar (el mando). Teclas sobre la vista: 1-4 modos, A todo, Alt+A nada, Ctrl+I "
     "invertir, G / R / S mover, rotar, escalar (X / Y bloquean un eje, Ctrl ajusta), F encuadrar, P / Alt+P fijar, "
     "W soldar, Y separar. La rueda acerca, arrastrar con el derecho o el medio desplaza."},
    {"Least Stretch: angles and lengths, along the marked seams (the default). Conformal (LSCM): angles only. By Face "
     "Groups: also cuts along the face group borders. Planar: each piece projected flat. Unfold Islands: the current "
     "islands flattened again where they are, keeping pins. Auto Seams: finds its own seams.",
     "Least Stretch: ángulos y longitudes, a lo largo de las costuras marcadas (por defecto). Conformal (LSCM): solo "
     "ángulos. By Face Groups: corta también por los bordes de grupos de caras. Planar: cada pieza proyectada plana. "
     "Unfold Islands: las islas actuales aplanadas de nuevo donde están, respetando los anclajes. Auto Seams: busca "
     "sus propias costuras."},
    {"Refine %.0f", "Refinar %.0f"},
    {"Refine", "Refinar"},
    {"Least-stretch steps after the conformal start: more keeps lengths better (0 = conformal).",
     "Pasos de mínimo estiramiento tras el inicio conformal: más conserva mejor las longitudes (0 = conformal)."},
    {"%.0f deg", "%.0f grados"},
    {"Sharp angle", "Ángulo agudo"},
    {"Sharp Edges marks the edges whose faces meet at more than this angle.",
     "Aristas duras marca las aristas cuyas caras se juntan a más de este ángulo."},
    {"Margin %.0f px", "Margen %.0f px"},
    {"Margin", "Margen"},
    {"Texels kept free between islands (half of it at the tile borders), at the texture's size.",
     "Téxeles libres entre islas (la mitad en los bordes del mosaico), al tamaño de la textura."},
    {"Columns %.0f", "Columnas %.0f"},
    {"Columns", "Columnas"},
    {"UDIM tiles across (along u, 1001 onwards; ten make a full UDIM row). Packing fills Columns x Rows tiles and "
     "never goes wider, also when it packs at a fixed texel density.",
     "Mosaicos UDIM a lo ancho (en u, desde 1001; diez forman una fila UDIM completa). Empaquetar llena Columnas x "
     "Filas mosaicos y nunca se hace más ancho, también al empaquetar a una densidad de téxeles fija."},
    {"UDIM rows up (along v: 1011 sits above 1001). 10 Columns x 2 Rows fills 1001-1020; more tiles, more texels per "
     "island.",
     "Filas UDIM hacia arriba (en v: 1011 queda sobre 1001). 10 Columnas x 2 Filas llena 1001-1020; más mosaicos, "
     "más téxeles por isla."},
    {"Turn islands to their smallest box and allow quarter turns, so more fit.",
     "Gira las islas a su caja más pequeña y permite cuartos de vuelta, para que quepan más."},
    {"Equal density", "Densidad igual"},
    {"Scale the islands to one texel density before packing (off: they keep their sizes relative to each other).",
     "Escala las islas a una sola densidad de téxeles antes de empaquetar (desactivado: conservan sus tamaños "
     "relativos)."},
    {"The texture size densities and margins are measured at (the Part's texture's when it has one).",
     "El tamaño de textura con el que se miden densidades y márgenes (el de la textura de la Pieza cuando tiene una)."},
    {"Target density", "Densidad objetivo"},
    {"Texels per unit of surface length that Set and Pack at Density give the islands.",
     "Téxeles por unidad de longitud de superficie que dan a las islas Fijar y Empaquetar a densidad."},
    {"Strips", "Franjas"},
    {"The sheet's horizontal strips: a count (4 equal strips), heights from the top (0.5,0.25,0.25), or v ranges "
     "(0-0.5;0.5-1). They show over tile 1001.",
     "Las franjas horizontales de la hoja: un número (4 franjas iguales), alturas desde arriba (0.5,0.25,0.25) o "
     "rangos de v (0-0.5;0.5-1). Se ven sobre el mosaico 1001."},
    {"Fit width", "Ajustar al ancho"},
    {"Squeeze or stretch the row to fill u 0-1 exactly; off keeps the islands' proportions (trims repeat in u).",
     "Comprime o estira la fila para llenar u 0-1 exactamente; desactivado conserva las proporciones de las islas "
     "(los remates se repiten en u)."},
    {"Checker %.0f per tile", "Damero %.0f por mosaico"},
    {"Paint stays on the model", "La pintura se queda en el modelo"},
    {"When the Part has a painted texture, every UV edit moves the paint with it (re-projected onto the new layout in "
     "the same undo step), so the model looks the same. Off: the texture stays put and the model shows the UVs' new "
     "place on it (what trim sheets want).",
     "Cuando la Pieza tiene una textura pintada, cada edición de UV mueve la pintura con ella (reproyectada sobre la "
     "distribución nueva en el mismo paso de deshacer), así el modelo se ve igual. Desactivado: la textura no se "
     "mueve y el modelo muestra el nuevo sitio de las UV sobre ella (lo que quieren los trim sheets)."},
    // --- PanelVdm.cpp (tools/i18n/coverage.py) ---
    {"Load a vector displacement map for the VDM brush: a 32- or 16-bit OpenEXR from Blender's VDM Brush Baker (or "
     "any EXR whose RGB are X right, Y up, Z out of the surface, in brush radii) or a .sfvdm. It is kept in your "
     "brush library. You can also drop an .exr on the window.",
     "Carga un mapa de desplazamiento vectorial para el pincel VDM: un OpenEXR de 32 o 16 bits de VDM Brush Baker de "
     "Blender (o cualquier EXR cuyo RGB sea X a la derecha, Y arriba, Z hacia fuera de la superficie, en radios de "
     "pincel) o un .sfvdm. Se guarda en tu biblioteca de pinceles. También puedes soltar un .exr en la ventana."},
    {"Add a flat VDM plane to sculpt a new brush on: seen from the top, its border masked so it stays put; the other "
     "Parts are hidden. Sculpt anything (overhangs welcome), then Create VDM.",
     "Añade un plano VDM plano sobre el que esculpir un pincel nuevo: visto desde arriba, con su borde enmascarado "
     "para que no se mueva; las demás Piezas se ocultan. Esculpe lo que quieras (los voladizos son bienvenidos) y "
     "luego Crear VDM."},
    {"Turn the sculpted VDM plane into a vector displacement brush: how far every point moved from the flat grid "
     "becomes the map, saved in your brush library and picked as the VDM brush's map Needs the VDM plane (its UVs "
     "record where each point started).",
     "Convierte el plano VDM esculpido en un pincel de desplazamiento vectorial: lo que se movió cada punto respecto "
     "a la rejilla plana pasa a ser el mapa, guardado en tu biblioteca de pinceles y elegido como mapa del pincel "
     "VDM. Necesita el plano VDM (sus UV registran dónde empezó cada punto)."},
    {"Save the VDM brush's map as a 32-bit float OpenEXR, for Blender: add it as the texture of a Draw brush with "
     "Area Plane mapping and Vector Displacement on (Help > User Guide > Sculpting > Vector displacement brushes).",
     "Guarda el mapa del pincel VDM como OpenEXR float de 32 bits, para Blender: añádelo como textura de un pincel "
     "Draw con mapeo Area Plane y Vector Displacement activado (Ayuda > Guía del usuario > Sculpting > Vector "
     "displacement brushes)."},
    {"For maps that store the surface normal in G instead of B (Y-up apps). Blender's maps and ours need nothing; "
     "maps from other apps depend on how they were exported, so try this if a stamp comes out lying on its side.",
     "Para mapas que guardan la normal de la superficie en G en vez de B (apps con Y arriba). Los mapas de Blender y "
     "los nuestros no necesitan nada; los de otras apps dependen de cómo se exportaron, así que prueba esto si un "
     "sello sale tumbado de lado."},
    {"Mirror the map's sideways (R) channel when loading: for a stamp that comes out mirrored.",
     "Refleja el canal lateral (R) del mapa al cargarlo: para un sello que sale reflejado."},
    {"Mirror the map's up (G) channel when loading: for a stamp that leans the wrong way.",
     "Refleja el canal hacia arriba (G) del mapa al cargarlo: para un sello que se inclina hacia el lado equivocado."},
    {"Negate the map's height (B) when loading: for a map that digs in where it should rise.",
     "Niega la altura (B) del mapa al cargarlo: para un mapa que se hunde donde debería subir."},
    {"Multiply a loaded map's vectors: for maps saved in other units than the brush radius (1 = as stored).",
     "Multiplica los vectores de un mapa cargado: para mapas guardados en unidades distintas del radio del pincel (1 "
     "= tal cual)."},
    {"Quads per side of a new VDM plane. 256 is a good start; divide it (Ctrl+D) for finer detail.",
     "Quads por lado de un plano VDM nuevo. 256 es un buen comienzo; divídelo (Ctrl+D) para más detalle."},
    {"What Create VDM calls the new map (a map of the same name is replaced).",
     "Cómo llama Crear VDM al mapa nuevo (un mapa con el mismo nombre se reemplaza)."},
    {"Pixels a side of the map Create VDM makes. 256 holds most shapes; 512 for fine detail (Blender suggests 512 or "
     "less).",
     "Píxeles por lado del mapa que hace Crear VDM. 256 basta para la mayoría de formas; 512 para detalle fino "
     "(Blender sugiere 512 o menos)."},
    // --- PanelVertexPaint.cpp (tools/i18n/coverage.py) ---
    {"The view's flat background colour. Choosing one switches the background to Flat Colour. It is saved "
     "automatically for the next session; projects keep their own.",
     "El color de fondo plano de la vista. Elegir uno cambia el fondo a Color plano. Se guarda automáticamente para "
     "la próxima sesión; los proyectos guardan el suyo."},
    {"The paint layer the Paint brush and fills write into. Add and arrange layers in Tool > Paint Layers.",
     "La capa de pintura en la que escriben el pincel Paint y los rellenos. Añade y ordena capas en Herramienta > "
     "Capas de pintura."},
    {"Remove all vertex paint from the Part, paint layers included (undoable).",
     "Quita toda la pintura de vértices de la Pieza, capas de pintura incluidas (se puede deshacer)."},
    {"On a Part with no vertex paint, the colour above tints its Lit Sphere material, so you can try a skin, stone or "
     "metal tone without painting. Grey and black leave the material as it is. Vertex paint, once there, shows "
     "instead.",
     "En una Pieza sin pintura de vértices, el color de arriba tiñe su material Lit Sphere, para probar un tono de "
     "piel, piedra o metal sin pintar. El gris y el negro dejan el material como está. Si hay pintura de vértices, "
     "se ve esa."},
    {"Use a Lit Sphere image of your own: a lit sphere filling a square PNG, JPG or EXR. It joins the list and is "
     "kept in Documents/SculptForge/Lit Spheres; images you put in that folder show up when a material list opens. "
     "Right-click one of your Lit Spheres to remove it.",
     "Usa una imagen Lit Sphere propia: una esfera iluminada que llene un PNG, JPG o EXR cuadrado. Se suma a la "
     "lista y se guarda en Documents/SculptForge/Lit Spheres; las imágenes que pongas en esa carpeta aparecen al "
     "abrir una lista de materiales. Clic derecho en una de tus Lit Spheres para quitarla."},
    {"Viewport Shading", "Sombreado de vista"},
    {"Environment", "Entorno"},
    {"The light around the model: Studio (generated), Photo Studio, Small Studio and Outdoor Sky. HDR files (.hdr, "
     ".exr) in the folders of SCULPTFORGE_HDRI_PATH are listed after them.",
     "La luz alrededor del modelo: Studio (generado), Photo Studio, Small Studio y Outdoor Sky. Los archivos HDR "
     "(.hdr, .exr) de las carpetas de SCULPTFORGE_HDRI_PATH se listan tras ellos."},
    {"Brightness of the light in stops: +1 doubles it.", "Brillo de la luz en pasos: +1 lo duplica."},
    {"Exposure %+.1f EV", "Exposición %+.1f EV"},
    {"Turns the environment about the up axis, moving the highlights.",
     "Gira el entorno sobre el eje vertical, moviendo los brillos."},
    {"Tone map", "Mapeo tonal"},
    {"How bright light maps to the screen: ACES (filmic contrast), AgX (softer, keeps bright colours from going "
     "flat), Reinhard (gentle) or None.",
     "Cómo pasa la luz brillante a la pantalla: ACES (contraste fílmico), AgX (más suave, evita que los colores "
     "brillantes queden planos), Reinhard (suave) o Ninguno."},
    {"Show environment", "Mostrar entorno"},
    {"Show the environment behind the model instead of the background.",
     "Muestra el entorno detrás del modelo en vez del fondo."},
    {"How soft the environment behind the model is.", "Lo suave que es el entorno detrás del modelo."},
    {"Roughness of Parts without a material texture (their colour comes from vertex paint or the base colour).",
     "Rugosidad de las Piezas sin textura de material (su color viene de la pintura de vértices o del color base)."},
    {"Roughness %.2f", "Rugosidad %.2f"},
    {"Metallic of Parts without a material texture.", "Metálico de las Piezas sin textura de material."},
    {"Metallic %.2f", "Metálico %.2f"},
    {"How much the sculpt's creases darken, so detail reads under even light. 0 is plain PBR.",
     "Cuánto se oscurecen los pliegues de la escultura, para que el detalle se lea con luz uniforme. 0 es PBR puro."},
    // --- PanelView.cpp (tools/i18n/coverage.py) ---
    {"See through the other Parts to the active one (again: solid). With only the active Part shown, it is "
     "see-through itself (#80, #91).",
     "Ver la Pieza activa a través de las demás (otra vez: sólidas). Con solo la Pieza activa a la vista, ella misma "
     "se vuelve transparente (#80, #91)."},
    {"Save Render writes a PNG (lossless).", "Guardar render escribe un PNG (sin pérdidas)."},
    {"Save Render writes a JPG (smaller files, for sharing).",
     "Guardar render escribe un JPG (archivos más pequeños, para compartir)."},
    {"Save Render's longer side: twice the view (at most 4096 pixels), or 1080, 1920, 2048 or 4096 pixels.",
     "El lado más largo de Guardar render: el doble de la vista (como mucho 4096 píxeles), o 1080, 1920, 2048 o 4096 "
     "píxeles."},
    {"Light", "Luz"},
    {"Light direction", "Dirección de la luz"},
    {"Drag on the ball to move the light around the model (the dot is the light); double-click puts it back where the "
     "material has it. Lit Sphere and studio shading.",
     "Arrastra sobre la bola para mover la luz alrededor del modelo (el punto es la luz); doble clic la devuelve a "
     "donde la tiene el material. Sombreado Lit Sphere y de estudio."},
    {"How bright the light is: 1 is the material as made.",
     "Lo brillante que es la luz: 1 es el material tal como se hizo."},
    {"Ambient", "Ambiente"},
    {"Even light from everywhere that lifts the shadows, so the dark side still reads.",
     "Luz uniforme desde todas partes que levanta las sombras, para que el lado oscuro se siga leyendo."},
    {"Ambient %.2f", "Ambiente %.2f"},
    {"The material's own light again: direction, intensity and no ambient.",
     "Vuelve a la luz propia del material: dirección, intensidad y sin ambiente."},
    {"PBR: Viewport Shading's rotation and exposure", "PBR: rotación y exposición de Sombreado de vista"},
    // --- Pickers.cpp (tools/i18n/coverage.py) ---
    {"Sort", "Ordenar"},
    {"quad remesh Guides: draw curves on the model for Quad Remesh's edge loops to follow (a curve that ends where it "
     "started makes a loop). Alt+click a curve to delete it.",
     "Guías de quad remesh: dibuja curvas en el modelo para que las sigan los bucles de aristas de Quad Remesh (una "
     "curva que acaba donde empezó forma un bucle). Alt+clic en una curva la borra."},
    {"Brushes whose names start with this letter. Click to show only them.",
     "Pinceles cuyo nombre empieza por esta letra. Haz clic para ver solo esos."},
    {"Brush packs", "Paquetes de pinceles"},
    {"How far spray dabs land from the cursor, as a fraction of the brush size.",
     "A qué distancia del cursor caen los toques de Spray, como fracción del tamaño del pincel."},
    {"Stamp this vector displacement map with the VDM brush (Drag Stamp).",
     "Estampa este mapa de desplazamiento vectorial con el pincel VDM (Drag Stamp)."},
    {"Use any grayscale image as the alpha: white = full effect, black = none.",
     "Usa cualquier imagen en escala de grises como alpha: blanco = efecto completo, negro = nada."},
    {"Load image alphas as their negative (black = full effect).",
     "Carga los alphas de imagen en negativo (negro = efecto completo)."},
    {"The vertex paint colour (C over the model picks one).",
     "El color de pintura de vértices (C sobre el modelo toma uno)."},
    {"Click to swap with the second colour.", "Clic para intercambiarlo con el segundo color."},
    {"Click to swap it with the main colour.", "Clic para intercambiarlo con el color principal."},
    // --- Precision.cpp (tools/i18n/coverage.py) ---
    {"Steady radius %.0f px", "Radio estable %.0f px"},
    {"Steady radius", "Radio estable"},
    {"Length of the string the brush trails on: longer = steadier lines.",
     "Longitud de la cuerda de la que tira el pincel: más larga = líneas más estables."},
    {"Precision Intensity %.0f", "Intensidad de precisión %.0f"},
    {"Precision Intensity", "Intensidad de precisión"},
    {"Hold Shift+Alt while drawing: the brush moves at a quarter of the pen's speed for careful placement, and dabs "
     "at this intensity. Let go and it glides back under the pen.",
     "Mantén Mayús+Alt mientras dibujas: el pincel se mueve a un cuarto de la velocidad del lápiz para colocarlo con "
     "cuidado, y da toques con esta intensidad. Suelta y vuelve deslizándose bajo el lápiz."},
    {"Brush pressure curve", "Curva de presión del pincel"},
    {"How pen pressure (left to right) becomes this brush's pressure (bottom to top), after the tablet's curve. Each "
     "brush keeps its own. Drag a point; double-click the curve to add one (up to 5); right-click a point to remove "
     "it.",
     "Cómo la presión del lápiz (de izquierda a derecha) pasa a la presión de este pincel (de abajo arriba), tras la "
     "curva de la tableta. Cada pincel guarda la suya. Arrastra un punto; doble clic en la curva para añadir uno "
     "(hasta 5); clic derecho en un punto para quitarlo."},
    // --- RadialMenu.cpp (tools/i18n/coverage.py) ---
    {"Radial Menus", "Menús radiales"},
    {"Pie menus of your own: hold a menu's key (or its mouse or pen button), flick toward an item and let go to run "
     "it. Tap the key instead and the pie stays open: click an item, or beside the pie to close it. Up to 8 menus of "
     "8 items; an item is anything the command palette (F3) finds.",
     "Menús radiales propios: mantén la tecla de un menú (o su botón del ratón o del lápiz), desliza hacia un "
     "elemento y suelta para ejecutarlo. Si en cambio tocas la tecla, el menú se queda abierto: haz clic en un "
     "elemento, o junto al menú para cerrarlo. Hasta 8 menús de 8 elementos; un elemento es cualquier cosa que "
     "encuentre la paleta de comandos (F3)."},
    {"Add menu", "Añadir menú"},
    {"A new radial menu (up to 8). Give it a key, then items.",
     "Un menú radial nuevo (hasta 8). Dale una tecla y luego elementos."},
    {"Key", "Tecla"},
    {"Click, then press the key that opens this menu (Esc cancels, Backspace clears). A key another action has moves "
     "here. Preferences > Hotkeys lists it too.",
     "Haz clic y pulsa la tecla que abre este menú (Esc cancela, Retroceso borra). Una tecla que tenía otra acción "
     "pasa aquí. Preferencias > Atajos de teclado también la muestra."},
    {"Button", "Botón"},
    {"A mouse or pen button that opens this menu while held, besides the key. It then stops doing what it did before "
     "(a pen button's Preferences > Tablet mapping, the middle button's pan, the right button's turn). Mouse buttons "
     "open it only when no modifier key is held: Alt+middle still pans, Alt+right still zooms (Maya style), "
     "Shift+right still turns the view.",
     "Un botón del ratón o del lápiz que abre este menú mientras se mantiene, además de la tecla. Deja entonces de "
     "hacer lo que hacía (la asignación de un botón del lápiz en Preferencias > Tableta, el desplazamiento del botón "
     "medio, el giro del derecho). Los botones del ratón solo lo abren sin teclas modificadoras: Alt+medio sigue "
     "desplazando, Alt+derecho sigue acercando (estilo Maya), Mayús+derecho sigue girando la vista."},
    {"Item", "Elemento"},
    {"Click to change its label, icon or action.", "Clic para cambiar su etiqueta, icono o acción."},
    {"Label", "Etiqueta"},
    {"What the pie shows for it; empty: the action's own name.",
     "Lo que muestra el menú para él; vacío: el nombre propio de la acción."},
    {"Icon", "Icono"},
    {"The icon the pie shows for it.", "El icono que muestra el menú para él."},
    {"Save this menu as a .json file to share or keep (action names only).",
     "Guarda este menú como archivo .json para compartirlo o conservarlo (solo nombres de acciones)."},
    {"Load a menu someone exported. Only action names are read; ones this version doesn't have are left out. Goes "
     "into this menu if it's empty, otherwise into a new one.",
     "Carga un menú que alguien exportó. Solo se leen nombres de acciones; los que esta versión no tiene se omiten. "
     "Va a este menú si está vacío; si no, a uno nuevo."},
    {"Empty this menu. Its key stays; an empty menu does nothing.",
     "Vacía este menú. Su tecla se queda; un menú vacío no hace nada."},
    {"Restore default menus", "Restaurar menús de fábrica"},
    {"Back to the three built-in menus: Brushes (T), Masking (Y) and Strokes (J).",
     "Vuelve a los tres menús incluidos: Pinceles (T), Enmascarado (Y) y Trazos (J)."},
    // --- Shell.cpp (tools/i18n/coverage.py) ---
    {"Sculpt brushes also paint the colour.", "Los pinceles de esculpir también pintan el color."},
    {"Front faces only: brushes leave the far side of thin parts alone.",
     "Solo caras frontales: los pinceles no tocan el otro lado de las partes finas."},
    {"Brushes only reach the surface joined to the point under the cursor, within the brush size: the other leg, the "
     "next finger or a lip across a gap stay as they are (Blender's Connected Only). Saved with the preferences and "
     "the project.",
     "Los pinceles solo llegan a la superficie unida al punto bajo el cursor, dentro del tamaño del pincel: la otra "
     "pierna, el dedo de al lado o un labio al otro lado de un hueco no cambian (Connected Only de Blender). Se "
     "guarda con las preferencias y el proyecto."},
    {"The brush falloff is measured on screen: everything inside the brush circle is reached, through the model and "
     "out the back, as a cylinder along the view (Blender's Projected from View). Turn on Connected only too to keep "
     "it to one piece.",
     "La atenuación del pincel se mide en pantalla: se alcanza todo lo que está dentro del círculo del pincel, a "
     "través del modelo y por detrás, como un cilindro en la dirección de la vista (Projected from View de Blender). "
     "Activa también Solo conectado para limitarlo a una pieza."},
    {"Ctrl+drag paints the mask with the brush; Ctrl+Alt+drag erases it. Ctrl+drag on the background clears it.",
     "Ctrl+arrastrar pinta la máscara con el pincel; Ctrl+Alt+arrastrar la borra. Ctrl+arrastrar en el fondo la "
     "quita."},
    {"Ctrl+drag draws a lasso and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.",
     "Ctrl+arrastrar dibuja un lazo y enmascara todo lo que hay dentro; Ctrl+Alt+arrastrar quita la máscara. Incluye "
     "el lado de atrás."},
    {"Ctrl+drag draws a rectangle and masks everything inside it; Ctrl+Alt+drag unmasks. The far side is included.",
     "Ctrl+arrastrar dibuja un rectángulo y enmascara todo lo que hay dentro; Ctrl+Alt+arrastrar quita la máscara. "
     "Incluye el lado de atrás."},
    {"Ctrl+Shift+drag a rectangle to keep what's inside and hide the rest; add Alt to hide the inside.",
     "Ctrl+Mayús+arrastra un rectángulo para conservar lo de dentro y ocultar el resto; añade Alt para ocultar lo de "
     "dentro."},
    {"Ctrl+Shift+drag a lasso to keep what's inside and hide the rest; add Alt to hide the inside.",
     "Ctrl+Mayús+arrastra un lazo para conservar lo de dentro y ocultar el resto; añade Alt para ocultar lo de "
     "dentro."},
    {"Ctrl+Shift+drag a line: the shaded side is cut (Alt: the other side). Away removes it through the whole Part "
     "and closes the cut flat; In steps it down by Depth; Add raises it; Clip presses it flat onto the line.",
     "Ctrl+Mayús+arrastra una línea: se corta el lado sombreado (Alt: el otro lado). Quitar lo elimina a través de "
     "toda la Pieza y cierra el corte plano; Hundir lo baja en Profundidad; Añadir lo eleva; Recortar lo aplasta "
     "contra la línea."},
    {"Ctrl+Shift+drag a lasso: Away cuts a hole through the Part, In a pocket Depth deep, Add raises the shape, Clip "
     "presses what lies outside it onto its outline.",
     "Ctrl+Mayús+arrastra un lazo: Quitar abre un agujero a través de la Pieza, Hundir un hueco de Profundidad, "
     "Añadir eleva la forma, Recortar aplasta lo que queda fuera contra su contorno."},
    {"Ctrl+Shift+drag a rectangle: Away cuts a hole through the Part, In a pocket Depth deep, Add a block, Clip "
     "presses what lies outside it onto its edges.",
     "Ctrl+Mayús+arrastra un rectángulo: Quitar abre un agujero a través de la Pieza, Hundir un hueco de "
     "Profundidad, Añadir un bloque, Recortar aplasta lo que queda fuera contra sus bordes."},
    {"Ctrl+Shift+click points: click the first point to cut the polygon, double-click or Enter to cut along an open "
     "bent line (Alt: the other side), Esc to drop it.",
     "Ctrl+Mayús+clic en puntos: haz clic en el primer punto para cortar el polígono, doble clic o Intro para cortar "
     "a lo largo de una línea quebrada abierta (Alt: el otro lado), Esc para descartarla."},
    {"Away", "Quitar"},
    {"Remove what the shape covers, through the whole Part, and close the cut with a flat cap.",
     "Elimina lo que cubre la forma, a través de toda la Pieza, y cierra el corte con una tapa plana."},
    {"In", "Hundir"},
    {"Cut a flat-bottomed pocket Depth deep where the shape covers the Part.",
     "Talla un hueco de fondo plano de Profundidad donde la forma cubre la Pieza."},
    {"Add", "Añadir"},
    {"Add the shape, extruded Depth out of the surface, joined to the Part.",
     "Añade la forma, extruida Profundidad fuera de la superficie, unida a la Pieza."},
    {"Clip", "Recortar"},
    {"Remove nothing: press the shaded side of a line, or everything outside a lasso or rectangle, flat onto the "
     "shape's outline, through the whole Part (#102). Hard-surface planes and clean silhouettes; the mask holds "
     "parts back.",
     "No quita nada: aplasta el lado sombreado de una línea, o todo lo que queda fuera de un lazo o un rectángulo, "
     "contra el contorno de la forma, a través de toda la Pieza (#102). Planos hard-surface y siluetas limpias; la "
     "máscara retiene partes."},
    {"Pen pressure scales the brush strength.", "La presión del lápiz escala la fuerza del pincel."},
    {"Pen pressure scales the brush size.", "La presión del lápiz escala el tamaño del pincel."},
    {"Mirror strokes across X (left and right).", "Refleja los trazos en X (izquierda y derecha)."},
    {"Mirror strokes across Y (top and bottom).", "Refleja los trazos en Y (arriba y abajo)."},
    {"Mirror strokes across Z (front and back).", "Refleja los trazos en Z (delante y detrás)."},
    {"Radial symmetry: repeat strokes around an axis (Radial Copies copies, Transform palette).",
     "Simetría radial: repite los trazos alrededor de un eje (Copias radiales copias, paleta Transformar)."},
    {"Local symmetry: mirror and turn strokes about the active Part's centre instead of the world origin.",
     "Simetría local: refleja y gira los trazos sobre el centro de la Pieza activa en vez del origen del mundo."},
    {"Browse, search and install brushes other sculptors shared.",
     "Explora, busca e instala pinceles que compartieron otros escultores."},
    {"Wire", "Malla"},
    {"Wireframe: show the polygons of the active Part (Shift+F).",
     "Malla de alambre: muestra los polígonos de la Pieza activa (Mayús+F)."},
    {"Colour each face group of the active Part.", "Colorea cada grupo de caras de la Pieza activa."},
    {"Show or hide the mask (masked areas drawn darker). Hiding it keeps the mask: masked areas stay protected from "
     "brushes until you clear it.",
     "Muestra u oculta la máscara (las zonas enmascaradas se ven más oscuras). Ocultarla conserva la máscara: las "
     "zonas enmascaradas siguen protegidas de los pinceles hasta que la borres."},
    {"Show vertex paint colours (off: plain material).",
     "Muestra los colores de la pintura de vértices (desactivado: material simple)."},
    {"Draw the sculpt.", "Dibuja la escultura."},
    {"Open this menu. Its sections are the palettes of the Classic layout.",
     "Abre este menú. Sus secciones son las paletas del diseño Clásico."},
    {"Camera locked", "Cámara bloqueada"},
    {"Find any command, palette, brush or material by name and run it.",
     "Busca por nombre cualquier comando, paleta, pincel o material y ejecútalo."},
    {"Click to open or close the tray, drag to resize it. Palettes go in trays from their header icon.",
     "Clic para abrir o cerrar la bandeja, arrastra para cambiar su tamaño. Las paletas van a las bandejas desde el "
     "icono de su cabecera."},
    {"Shell", "Interfaz"},
    {"Brush radius in screen pixels (0.5 to 800), and in mm on the surface under the cursor. [ and ] change it by "
     "10%, Shift+[ and Shift+] by 1 px, Ctrl+Shift+right-drag scrubs it finely; Ctrl+click to type.",
     "Radio del pincel en píxeles de pantalla (0,5 a 800), y en mm sobre la superficie bajo el cursor. [ y ] lo "
     "cambian un 10%, Mayús+[ y Mayús+] 1 px, Ctrl+Mayús+arrastrar con el derecho lo ajusta fino; Ctrl+clic para "
     "escribir."},
    {"Triangles you can see and sculpt (visible Parts, hidden parts left out) and in the whole scene. Micro: the "
     "micro triangles of the visible Parts' micro levels (Geometry > Micro).",
     "Triángulos que puedes ver y esculpir (Piezas visibles, sin las partes ocultas) y en toda la escena. Micro: los "
     "microtriángulos de los niveles micro de las Piezas visibles (Geometría > Micro)."},
    {"Ctrl held: the mask brush (Ctrl+Shift: the selection brush). Click, or press B with Ctrl held, to pick Mask "
     "Pen, Lasso Mask or Rectangle Mask (Select Rectangle or Select Lasso).",
     "Con Ctrl pulsado: el pincel de máscara (Ctrl+Mayús: el de selección). Haz clic, o pulsa B con Ctrl pulsado, "
     "para elegir Mask Pen, Lasso Mask o Rectangle Mask (Select Rectangle o Select Lasso)."},
    {"Ctrl held: the mask's stroke, Dots (Mask Pen), Lasso or Rect (Ctrl+Shift: the selection's). Click with Ctrl "
     "held to choose.",
     "Con Ctrl pulsado: el trazo de la máscara, Dots (Mask Pen), Lasso o Rect (Ctrl+Mayús: el de la selección). Haz "
     "clic con Ctrl pulsado para elegir."},
    // --- SpaceMouse.cpp (tools/i18n/coverage.py) ---
    {"Copy device report", "Copiar informe del dispositivo"},
    {"Copies what SculptForge sees (each joystick's name, USB ids and why it was or wasn't used) to paste into a bug "
     "report.",
     "Copia lo que ve SculptForge (el nombre de cada joystick, sus ids USB y por qué se usó o no) para pegarlo en un "
     "informe de error."},
    {"Experimental. A 3Dconnexion SpaceMouse moves the view with one hand while the other holds the pen: push, pull "
     "and twist the cap to pan, zoom and turn the model about the pivot. Read directly as a USB device, without "
     "3Dconnexion's driver; tested only with simulated input so far.",
     "Experimental. Un SpaceMouse de 3Dconnexion mueve la vista con una mano mientras la otra sujeta el lápiz: "
     "empuja, tira y gira el capuchón para desplazar, acercar y girar el modelo sobre el pivote. Se lee directamente "
     "como dispositivo USB, sin el controlador de 3Dconnexion; de momento solo se ha probado con entrada simulada."},
    {"Experimental. Off: the SpaceMouse is ignored.", "Experimental. Desactivado: el SpaceMouse se ignora."},
    {"How fast the view moves for the same push on the cap.",
     "Lo rápido que se mueve la vista con el mismo empuje del capuchón."},
    {"How far the cap must move before the view does, so a hand resting on it or a slightly off-centre cap doesn't "
     "drift the view.",
     "Cuánto debe moverse el capuchón antes de que lo haga la vista, para que una mano apoyada o un capuchón algo "
     "descentrado no desplacen la vista."},
    {"The model stays upright: twisting turns it about the world's up axis, tilting looks over or under it, and "
     "rolling the cap does nothing.",
     "El modelo se queda derecho: girar lo gira sobre el eje vertical del mundo, inclinar mira por encima o por "
     "debajo, y rodar el capuchón no hace nada."},
    {"Hold during strokes", "Retener durante los trazos"},
    {"While you sculpt, paint or mask, the SpaceMouse leaves the view alone and its buttons do nothing, so a hand "
     "resting on the cap never moves the model under the pen. Off: the view turns mid-stroke.",
     "Mientras esculpes, pintas o enmascaras, el SpaceMouse no toca la vista y sus botones no hacen nada, así una "
     "mano apoyada en el capuchón nunca mueve el modelo bajo el lápiz. Desactivado: la vista gira en mitad del trazo."},
    {"Only the direction you push the most moves the view: a twist doesn't also pan a little.",
     "Solo mueve la vista la dirección en la que más empujas: un giro no desplaza también un poco."},
    {"Move the view the other way for this push or turn of the cap.",
     "Mueve la vista al revés para este empuje o giro del capuchón."},
    {"What the SpaceMouse reports now: Pan X, Zoom, Pan Y, Tilt, Roll, Spin. Grey: inside the dead zone.",
     "Lo que informa ahora el SpaceMouse: Pan X, Zoom, Pan Y, Tilt, Roll, Spin. Gris: dentro de la zona muerta."},
    {"On, speed 1, dead zone 8%, nothing inverted, free rotation, held during strokes, all axes at once; left button "
     "Frame, right button Front view.",
     "Activado, velocidad 1, zona muerta 8%, nada invertido, rotación libre, retenido durante los trazos, todos los "
     "ejes a la vez; botón izquierdo Encuadrar, botón derecho Vista frontal."},
    // --- StrokePopup.cpp (tools/i18n/coverage.py) ---
    {"Stroke popup", "Ventana de trazos"},
    {"A grid at the cursor with the stroke types, the modifiers (Add / Sub, Smooth, Mask, Select) and the mask and "
     "Cut shapes. Hold the key, point at one and let go; or tap the key and click. Smooth, Mask, Select and the "
     "shapes stay on without holding Shift or Ctrl, until you pick them again or another brush.",
     "Una rejilla en el cursor con los tipos de trazo, los modificadores (Add / Sub, Smooth, Mask, Select) y las "
     "formas de máscara y de Cortar. Mantén la tecla, apunta a uno y suelta; o toca la tecla y haz clic. Smooth, "
     "Mask, Select y las formas se quedan activos sin mantener Mayús ni Ctrl, hasta que los vuelvas a elegir u otro "
     "pincel."},
    // --- Tablet.cpp (tools/i18n/coverage.py) ---
    {"Pressure", "Presión"},
    {"No pressure? In Wacom Tablet Properties > Mapping, switch \"Use Windows Ink\" (on for most tablets, off for "
     "some older drivers), then bring the pen back. Without pressure the pen draws at full strength.",
     "¿Sin presión? En Wacom Tablet Properties > Mapping, cambia \"Use Windows Ink\" (activado para la mayoría de "
     "tabletas, desactivado para algunos controladores antiguos) y vuelve a acercar el lápiz. Sin presión el lápiz "
     "dibuja a toda fuerza."},
    {"Whether the pen reports pressure. Without it the pen draws at full strength; check the tablet in your desktop's "
     "tablet settings.",
     "Si el lápiz informa de la presión. Sin ella el lápiz dibuja a toda fuerza; revisa la tableta en los ajustes de "
     "tableta de tu escritorio."},
    {"The pen SculptForge last saw, and whether it is in range or touching.",
     "El último lápiz que vio SculptForge, y si está en rango o tocando."},
    {"Turn on if the tablet driver is set to Mouse mode: the pen moves the cursor by how far it moves, and lifting it "
     "and putting it down elsewhere leaves the cursor where it was. Off (Pen mode): the cursor is where the pen is.",
     "Actívalo si el controlador de la tableta está en modo Ratón: el lápiz mueve el cursor según cuánto se mueve, y "
     "levantarlo y apoyarlo en otro sitio deja el cursor donde estaba. Desactivado (modo Lápiz): el cursor está "
     "donde está el lápiz."},
    {"The pen's pressure as the tablet reports it, and after the pressure curve below (what brushes get).",
     "La presión del lápiz tal como la informa la tableta, y tras la curva de presión de abajo (lo que reciben los "
     "pinceles)."},
    {"The pen's tilt, as the tablet reports it (brushes don't use it yet).",
     "La inclinación del lápiz, tal como la informa la tableta (los pinceles aún no la usan)."},
    {"Draw here with the pen: the stroke is as thick as the brush pressure, and the line at the bottom plots it along "
     "the stroke.",
     "Dibuja aquí con el lápiz: el trazo es tan grueso como la presión del pincel, y la línea de abajo la traza a lo "
     "largo del trazo."},
    {"How pen pressure (left to right) becomes brush pressure (bottom to top), before each brush's own curve. The dot "
     "is the pen right now.",
     "Cómo la presión del lápiz (de izquierda a derecha) pasa a presión del pincel (de abajo arriba), antes de la "
     "curva propia de cada pincel. El punto es el lápiz ahora mismo."},
    {"Below 1: soft, a light touch gives a lot. Above 1: firm, press harder.",
     "Bajo 1: blando, un toque ligero da mucho. Sobre 1: firme, presiona más."},
    {"Brush pressure at the lightest touch.", "Presión del pincel con el toque más ligero."},
    {"Brush pressure when pressing fully.", "Presión del pincel al presionar a fondo."},
    {"Pen pressure below this counts as the lightest touch (ignores a resting hand).",
     "La presión del lápiz por debajo de esto cuenta como el toque más ligero (ignora una mano apoyada)."},
    {"Pen pressure that already counts as pressing fully (for a light hand).",
     "La presión del lápiz que ya cuenta como presionar a fondo (para una mano ligera)."},
    {"Eraser", "Borrador"},
    {"What the pen's eraser end does. Invert: brushes subtract, like holding Alt (with Ctrl it erases mask). Erase "
     "mask: removes mask wherever it draws.",
     "Lo que hace la goma del lápiz. Invertir: los pinceles restan, como mantener Alt (con Ctrl borra máscara). "
     "Borrar máscara: quita máscara por donde dibuja."},
    {"Eraser: Invert; lower button: Right-click; upper button: Invert (Alt); third button: Pan view; View speed 1.00. "
     "The curve and Mouse mode stay.",
     "Borrador: Invertir; botón inferior: Clic derecho; botón superior: Invertir (Alt); tercer botón: Desplazar "
     "vista; Velocidad de vista 1.00. La curva y el modo Ratón se quedan."},
    // --- UserGuide.cpp (tools/i18n/coverage.py) ---
    {"Go back to the page you were reading.", "Vuelve a la página que estabas leyendo."},
    {"Back (Alt+Left)", "Atrás (Alt+Izquierda)"},
    {"Go forward again.", "Avanza otra vez."},
    {"Forward (Alt+Right)", "Adelante (Alt+Derecha)"},
    {"The guide's first page: every chapter.", "La primera página de la guía: todos los capítulos."},
    {"Find every place in the guide that mentions all of these words.",
     "Busca cada lugar de la guía que menciona todas estas palabras."},
    {"Clear the search", "Borrar la búsqueda"},
    {"Read this page on the SculptForge website.", "Lee esta página en el sitio web de SculptForge."},
    // --- Shell.cpp (tools/i18n/coverage.py) ---
    {"Brushes change the shape. Turn on Rgb to paint colour at the same time.",
     "Los pinceles cambian la forma. Activa Rgb para pintar color a la vez."},
    {"Paint colour without sculpting. Alt paints back to white, Shift still smooths.",
     "Pinta color sin esculpir. Alt pinta de vuelta a blanco, Mayús sigue suavizando."},
    {"Brushes add to the surface. Hold Alt to subtract for one stroke.",
     "Los pinceles suman a la superficie. Mantén Alt para restar durante un trazo."},
    {"Sub", "Restar"},
    {"Brushes subtract from the surface. Hold Alt to add for one stroke.",
     "Los pinceles restan a la superficie. Mantén Alt para sumar durante un trazo."},
    {"Dynamic topology: brushes add triangles where they need detail and remove them where they don't, so you can "
     "pull out horns or fingers without stretching. Detail sets how small the triangles get; zoom in or shrink the "
     "brush for finer ones. Needs a mesh without subdivision levels; quads under the brush become triangles and UVs "
     "are dropped (Undo brings them back).",
     "Topología dinámica: los pinceles añaden triángulos donde necesitan detalle y los quitan donde no, para sacar "
     "cuernos o dedos sin estirar. Detalle fija lo pequeños que son los triángulos; acerca la vista o reduce el "
     "pincel para hacerlos más finos. Necesita una malla sin niveles de subdivisión; los quads bajo el pincel pasan "
     "a triángulos y las UV se descartan (Deshacer las recupera)."},
    {"Lit Sphere shading (the default): the chosen material sphere or the studio lights, made for reading forms while "
     "sculpting.",
     "Sombreado Lit Sphere (por defecto): la esfera de material elegida o las luces de estudio, pensado para leer "
     "las formas mientras esculpes."},
    {"PBR shading: the texture's material channels (base colour, roughness, metallic, normal, height, AO, emissive) "
     "lit by an environment, as a game engine or renderer shows them. Meshes without a material use the roughness "
     "and metallic set here.",
     "Sombreado PBR: los canales de material de la textura (color base, rugosidad, metálico, normal, altura, AO, "
     "emisión) iluminados por un entorno, como los muestra un motor de juego o un renderizador. Las mallas sin "
     "material usan la rugosidad y el metálico fijados aquí."},
    {"Smooth", "Suave"},
    {"Smooth shading (the default): normals blend across polygons, so the surface reads as one form.",
     "Sombreado suave (por defecto): las normales se mezclan entre polígonos, así la superficie se lee como una sola "
     "forma."},
    {"Flat shading: every polygon shows its own face, so the mesh's structure and each stroke's effect on it read "
     "clearly. Parts set to Smooth or Flat keep their own. A preference, saved automatically.",
     "Sombreado plano: cada polígono muestra su propia cara, así se leen claramente la estructura de la malla y el "
     "efecto de cada trazo. Las Piezas fijadas en Suave o Plano conservan el suyo. Una preferencia, se guarda "
     "automáticamente."},
    {"The view's background is the charcoal radial gradient (the default).",
     "El fondo de la vista es el degradado radial carbón (por defecto)."},
    {"The view's background is one solid colour (pick it below), without the gradient. A preference, saved "
     "automatically; projects keep their own.",
     "El fondo de la vista es un solo color (elígelo abajo), sin el degradado. Una preferencia, se guarda "
     "automáticamente; los proyectos guardan el suyo."},
    {"Fill the screen with SculptForge (again: back to a window).",
     "Llena la pantalla con SculptForge (otra vez: vuelve a ventana)."},
    {"Show the Gaussian splat capture.", "Muestra la captura de splats gaussianos."},
    {"Show only the active Part (again: show the others).",
     "Muestra solo la Pieza activa (otra vez: muestra las demás)."},
    {"Drag on this button to pan the view (or Alt+drag the background).",
     "Arrastra sobre este botón para desplazar la vista (o Alt+arrastra el fondo)."},
    {"Drag up and down on this button to zoom (or use the mouse wheel).",
     "Arrastra arriba y abajo sobre este botón para acercar (o usa la rueda del ratón)."},
    {"Drag on this button to orbit the view (or drag the background).",
     "Arrastra sobre este botón para orbitar la vista (o arrastra el fondo)."},
    {"Add a model file as a new Part.", "Añade un archivo de modelo como Pieza nueva."},
    {"Save the visible Parts as Wavefront OBJ.", "Guarda las Piezas visibles como Wavefront OBJ."},
    {"One GLB per level of detail.", "Un GLB por nivel de detalle."},
    {"OpenUSD for Apple, Omniverse and film pipelines.", "OpenUSD para Apple, Omniverse y pipelines de cine."},
    {"FBX sized and named for Unreal Engine, with LODs.", "FBX con tamaño y nombres para Unreal Engine, con LOD."},
    {"Save the Gaussian splats (PLY, glTF or USD).", "Guarda los splats gaussianos (PLY, glTF o USD)."},
    {"Close SculptForge (asks to save changes first).", "Cierra SculptForge (pregunta antes si guardar los cambios)."},
    {"Rebuild as an even, watertight surface (like Voxel Remesh): merges parts, fixes scans and generated models.",
     "Reconstruye como superficie uniforme y estanca (como Remallado vóxel): une partes, arregla escaneos y "
     "modelos generados."},
    {"With Tool > Remesh > Quick Remesh on: remesh the active Part at the Voxel Remesh resolution, in the background.",
     "Con Herramienta > Remallado > Remallado rápido activado: remalla la Pieza activa a la resolución de Remallado "
     "por vóxeles, en segundo plano."},
    {"Quick Remesh (hold and drag)", "Remallado rápido (mantén y arrastra)"},
    {"With Tool > Remesh > Quick Remesh on: hold this key and click or drag on the background (not on the model) to "
     "remesh the active Part.",
     "Con Herramienta > Remallado > Remallado rápido activado: mantén esta tecla y haz clic o arrastra sobre el "
     "fondo (no sobre el modelo) para remallar la Pieza activa."},
    {"Turn the sculpt into Gaussian splats.", "Convierte la escultura en splats gaussianos."},
    {"Fill the unmasked area with the colour.", "Rellena la zona sin máscara con el color."},
    {"Hide or show the shelves and trays around the canvas.",
     "Oculta o muestra los estantes y las bandejas alrededor del lienzo."},
    {"Show or hide the palette bar under the title.", "Muestra u oculta la barra de paletas bajo el título."},
    {"Open or close the left tray (or click its divider).",
     "Abre o cierra la bandeja izquierda (o haz clic en su divisor)."},
    {"Open or close the right tray (or click its divider).",
     "Abre o cierra la bandeja derecha (o haz clic en su divisor)."},
    {"Menus by task (File, Edit, Sculpt, Paint, Mesh, View, Render, Help), a status bar and a one-row tool shelf.",
     "Menús por tarea (Archivo, Editar, Esculpir, Pintar, Malla, Vista, Render, Ayuda), una barra de estado y un "
     "estante de herramientas de una fila."},
    {"The alphabetical palette bar and the two-row top shelf, as experienced sculptors expect.",
     "La barra de paletas alfabética y el estante superior de dos filas, como esperan los escultores con experiencia."},
    {"Palettes", "Paletas"},
    // --- Config.cpp (tools/i18n/coverage.py) ---
    {"Press a key for '%s'", "Pulsa una tecla para '%s'"},
    {"Now:", "Ahora:"},
    {"none", "ninguna"},
    {"Esc cancels, Backspace clears. A key that's taken moves here.",
     "Esc cancela, Retroceso borra. Una tecla ocupada pasa aquí."},
    {"No shortcut matches \"%s\".", "Ningún atajo coincide con \"%s\"."},
    {"Double-click a row, or Ctrl+Alt+click any button, to assign a key.",
     "Doble clic en una fila, o Ctrl+Alt+clic en cualquier botón, para asignar una tecla."},
    // --- Input.cpp (tools/i18n/coverage.py) ---
    {"With Alt", "Con Alt"},
    {"With Ctrl", "Con Ctrl"},
    // --- PanelUvEditor.cpp (tools/i18n/coverage.py) ---
    {"On model", "En el modelo"},
    {"Seams (%zu edges)", "Costuras (%zu aristas)"},
    {"Select", "Seleccionar"},
    // --- RadialMenu.cpp (tools/i18n/coverage.py) ---
    {"Right-drag opens this menu. Turn the view with %s.",
     "Arrastrar con el derecho abre este menú. Gira la vista con %s."},
    {"Action's own", "La de la acción"},
    {"Change action", "Cambiar acción"},
    {"Add item", "Añadir elemento"},
    {"No matching actions", "Ninguna acción coincide"},
    {"Your menus save themselves", "Tus menús se guardan solos"},
    // --- SpaceMouse.cpp (tools/i18n/coverage.py) ---
    {"No SpaceMouse found", "No se encontró ningún SpaceMouse"},
    {"Last button pressed: %d", "Último botón pulsado: %d"},
    // --- Tablet.cpp (tools/i18n/coverage.py) ---
    {"Pressure: no pen seen yet", "Presión: ningún lápiz aún"},
    {"Pressure: on", "Presión: activada"},
    {"Pressure: not detected", "Presión: no detectada"},
    {"Pressure: touch the tablet to check", "Presión: toca la tableta para comprobarla"},
    {"Troubleshooting", "Problemas"},
    {"No pen seen yet", "Ningún lápiz aún"},
    {"Tilt: not reported by this pen", "Inclinación: este lápiz no la informa"},
    {"Troubleshooting details", "Detalles para solucionar problemas"},
    {"Copy details", "Copiar detalles"},
    {"Classic pen timing", "Sincronización clásica del lápiz"},
    {"The pen as in SculptForge 0.11.15 and before: strokes move without waiting for each pen sample's pressure, and a pressure sample the system lost is not held. Turn on only if pressure got worse with your tablet since 0.11.16, and tell us (Copy details below).", "El lápiz como en SculptForge 0.11.15 y anteriores: los trazos avanzan sin esperar la presión de cada muestra del lápiz, y no se conserva una muestra de presión que el sistema perdió. Actívalo solo si la presión empeoró con tu tableta desde 0.11.16, y avísanos (Copiar detalles, abajo)."},
    {"Copies the lines above for a bug report about the pen: the SDL version, the display system, how SculptForge was installed and the pen's events.", "Copia las líneas de arriba para un informe de error sobre el lápiz: la versión de SDL, el sistema de pantalla, cómo se instaló SculptForge y los eventos del lápiz."},
    {"Running through XWayland. If the pen's pressure is poor, start SculptForge with SDL_VIDEO_DRIVER=wayland to use Wayland directly.", "Se ejecuta a través de XWayland. Si la presión del lápiz va mal, inicia SculptForge con SDL_VIDEO_DRIVER=wayland para usar Wayland directamente."},
    {"Pressure curve (all brushes)", "Curva de presión (todos los pinceles)"},
    // --- UserGuide.cpp (tools/i18n/coverage.py) ---
    {"SculptForge is free and open source, and stays that way. If it helps your work, you can support its development.",
     "SculptForge es libre y de código abierto, y seguirá siéndolo. Si te ayuda en tu trabajo, puedes apoyar su "
     "desarrollo."},
    {"Nothing in the guide matches \"%s\".", "Nada en la guía coincide con \"%s\"."},
    // --- core/RadialMenu.cpp (tools/i18n/coverage.py) ---
    {"None", "Ninguno"},
    {"Middle mouse button", "Botón central del ratón"},
    {"Mouse back button", "Botón atrás del ratón"},
    {"Mouse forward button", "Botón adelante del ratón"},
    {"Pen lower button", "Botón inferior del lápiz"},
    {"Pen upper button", "Botón superior del lápiz"},
    {"Pen third button", "Tercer botón del lápiz"},
    {"Right mouse button", "Botón derecho del ratón"},
    // --- PanelUvEditor.cpp (tools/i18n/coverage.py) ---
    {" (folds)", " (pliegues)"},
    {"%zu seam edges marked", "%zu aristas de costura marcadas"},
    {"%u islands  |  %zu selected  |  stretch %.3f%s  |  %.0f tx/u at %dK  |  %s",
     "%u islas  |  %zu seleccionadas  |  estiramiento %.3f%s  |  %.0f tx/u a %dK  |  %s"},
    {"Density", "Densidad"},
    {"Edge", "Arista"},
    {"Face", "Cara"},
    {"Island", "Isla"},
    {"Islands", "Islas"},
    {"No Image", "Sin imagen"},
    {"Plain", "Simple"},
    {"Selected", "Seleccionadas"},
    {"Stretch", "Estiramiento"},
    {"Texel Density", "Densidad de téxeles"},
    {"Trim Sheet", "Trim sheet"},
    {"Unwrap Selected", "Desenvolver selección"},
    // --- Brush palette (tools/i18n/coverage.py) ---
    {"Filter brushes", "Filtrar pinceles"},
    {"Alphas", "Alphas"},
    {"Built-in", "Incluidos"},
    {"Custom", "Propios"},
    {"Custom brushes", "Pinceles propios"},
    {"Larger brush by 1 px", "Pincel 1 px más grande"},
    {"Smaller brush by 1 px", "Pincel 1 px más pequeño"},
    {"On", "Activado"},
    {"Use global", "Usar global"},
    {"Reset Brush to Default", "Restablecer pincel"},
    {"Save Brush Settings", "Guardar ajustes del pincel"},
    {"Sort: Recent", "Ordenar: Recientes"},
    {"Stamp rotation", "Rotación del sello"},
    {"Vector displacement", "Desplazamiento vectorial"},
    {"World Size", "Tamaño en el mundo"},
    // Live mode (PanelAiLink.cpp)
    {"\"%s\" on this computer asks to sculpt in this window.", "«%s», en este equipo, pide esculpir en esta ventana."},
    {"%d steps", "%d pasos"},
    {"%s wants to run:", "%s quiere ejecutar:"},
    {"AI connected: ", "IA conectada: "},
    {"AI connected: %s", "IA conectada: %s"},
    {"AI connection declined", "Conexión de IA rechazada"},
    {"AI connection request timed out", "La solicitud de conexión de IA ha caducado"},
    {"AI step declined", "Paso de IA rechazado"},
    {"AI tool disconnected", "Herramienta de IA desconectada"},
    {"AI tools can ask to control this window (Preferences > AI)", "Las herramientas de IA pueden pedir controlar esta ventana (Preferencias > IA)"},
    {"AI tools can't control this window", "Las herramientas de IA no pueden controlar esta ventana"},
    {"Allow once", "Permitir una vez"},
    {"Allow this AI step?", "¿Permitir este paso de IA?"},
    {"Approve", "Aprobar"},
    {"Deny", "Denegar"},
    {"Disconnect AI tool", "Desconectar herramienta de IA"},
    {"Disconnect", "Desconectar"},
    {"It can run SculptForge's commands and see the viewport; what it sees goes to that tool's AI service. Every step is in Undo. Files outside the project, scripts and the network ask you first.", "Puede ejecutar los comandos de SculptForge y ver el visor; lo que ve va al servicio de IA de esa herramienta. Cada paso queda en Deshacer. Los archivos fuera del proyecto, los scripts y la red te preguntan antes."},
    {"Let AI tools control this window", "Permitir que la IA controle esta ventana"},
    {"Let an AI tool control this window?", "¿Permitir que una herramienta de IA controle esta ventana?"},
    {"Live mode can't start: ", "El modo en vivo no puede iniciarse: "},
    {"Live mode: an AI tool on this computer (sculptforge-cli --mcp --attach) can ask to sculpt in this window while you watch. You approve each connection; files outside the project, scripts and the network ask first. Nothing listens on the network.", "Modo en vivo: una herramienta de IA de este equipo (sculptforge-cli --mcp --attach) puede pedir esculpir en esta ventana mientras miras. Apruebas cada conexión; los archivos fuera del proyecto, los scripts y la red preguntan antes. Nada escucha en la red."},
    {"Stop the AI tool controlling this window now (its steps stay in Undo).", "Detener ahora la herramienta de IA que controla esta ventana (sus pasos quedan en Deshacer)."},
    {"Stop the AI tool now. Its steps stay in Undo.", "Detener ahora la herramienta de IA. Sus pasos quedan en Deshacer."},
    {"Your AI tool asks: ", "Tu herramienta de IA pide: "},
    // The in-app guide (Help > Connect an AI tool, PanelConnectAi.cpp) and its dialogs, in the guide's words
    {"1. Pick your tool", "1. Elige tu herramienta"},
    {"2. Let it work in this window", "2. Déjala trabajar en esta ventana"},
    {"3. What happens next", "3. Qué pasa después"},
    {"Add this to its configuration file (Settings > Developer > Edit Config), then restart Claude Desktop.", "Añade esto a su archivo de configuración (Settings > Developer > Edit Config) y reinicia Claude Desktop."},
    {"An AI tool on this computer can ask to work in this window while you watch. You approve it with a 6-digit code. Every step lands in Undo. Files outside the project, scripts and the network ask you first. What it sees goes to that tool's AI service. Nothing listens on the network. To set up your tool: Help > Connect an AI tool.", "Una herramienta de IA de este equipo puede pedir trabajar en esta ventana mientras miras. La apruebas con un código de 6 dígitos. Cada paso queda en Deshacer. Los archivos fuera del proyecto, los scripts y la red te preguntan antes. Lo que ve va al servicio de IA de esa herramienta. Nada escucha en la red. Para configurar tu herramienta: Ayuda > Conectar una herramienta de IA."},
    {"Approval lasts until it disconnects. Every step lands in Undo. Files outside the project, scripts and the network ask you first. What it sees goes to that tool's AI service.", "La aprobación dura hasta que se desconecte. Cada paso queda en Deshacer. Los archivos fuera del proyecto, los scripts y la red te preguntan antes. Lo que ve va al servicio de IA de esa herramienta."},
    {"Ask your AI tool for something (below). SculptForge shows a 6-digit code: click Approve, or tell the AI the code.", "Pide algo a tu herramienta de IA (ejemplos abajo). SculptForge muestra un código de 6 dígitos: pulsa Aprobar o dile el código a la IA."},
    {"Block out a creature head with horns for me to sculpt.", "Encaja una cabeza de criatura con cuernos para que yo la esculpa."},
    {"Click Approve, or tell your AI tool this code:", "Pulsa Aprobar o dile este código a tu herramienta de IA:"},
    {"Every step lands in Undo as \"AI: ...\", so you can step back.", "Cada paso queda en Deshacer como «AI: ...», así que puedes volver atrás."},
    {"It asks you first before files outside the project, scripts and the network.", "Te pregunta antes de usar archivos fuera del proyecto, scripts o la red."},
    {"It sees the viewport; what it sees goes to that tool's AI service.", "Ve el visor; lo que ve va al servicio de IA de esa herramienta."},
    {"Look at my model from all sides and tell me what to fix.", "Mira mi modelo desde todos los lados y dime qué corregir."},
    {"Make three rock variations I can refine.", "Haz tres variaciones de roca que yo pueda pulir."},
    {"On: your AI tool can ask to connect.", "Activado: tu herramienta de IA puede pedir conectarse."},
    {"Plans and sign-in", "Planes e inicio de sesión"},
    {"Teach me: walk me through sculpting an ear, step by step.", "Enséñame: guíame paso a paso para esculpir una oreja."},
    {"The AI blocks out a starting point and shows you options; you do the art.", "La IA prepara un punto de partida y te muestra opciones; el arte lo haces tú."},
    {"Try asking", "Prueba a pedir"},
    {"While it works, the AI connected badge sits at the bottom right. Disconnect stops it.", "Mientras trabaja, el distintivo «IA conectada» queda abajo a la derecha. Desconectar la detiene."},

    // --- Help > Connect an AI tool (PanelConnectAi.cpp, ConnectAi.cpp) ---
    {"Add to Cursor's global MCP file (or .cursor/mcp.json in a project).",
     "Añádelo al archivo MCP global de Cursor (o a .cursor/mcp.json en un proyecto)."},
    {"Add to Kiro's user MCP file.",
     "Añádelo al archivo MCP de usuario de Kiro."},
    {"Add to Rovo Dev's MCP file (acli rovodev mcp opens it).",
     "Añádelo al archivo MCP de Rovo Dev (acli rovodev mcp lo abre)."},
    {"Add to Zed's settings file (command palette: zed: open settings file).",
     "Añádelo al archivo de ajustes de Zed (paleta de comandos: zed: open settings file)."},
    {"Add to opencode.json.",
     "Añádelo a opencode.json."},
    {"Add to the global MCP file, then use it from Copilot Chat in Agent mode.",
     "Añádelo al archivo MCP global y úsalo desde Copilot Chat en modo Agent."},
    {"Add to:",
     "Añadir a:"},
    {"Add under extensions in Goose's configuration file (or goose configure > Add Extension > Command-Line Extension).",
     "Añádelo en extensions del archivo de configuración de Goose (o goose configure > Add Extension > Command-Line Extension)."},
    {"Claude Code uses your Pro, Max or Ultra plan when you sign in with your Claude account. If ANTHROPIC_API_KEY is set, it uses that key and API billing instead.",
     "Claude Code usa tu plan Pro, Max o Ultra cuando inicias sesión con tu cuenta de Claude. Si ANTHROPIC_API_KEY está definida, usa esa clave y se factura como API."},
    {"Claude on the web and on phones can't start programs on your computer: use Claude Desktop or Claude Code.",
     "Claude en la web y en el móvil no puede iniciar programas en tu ordenador: usa Claude Desktop o Claude Code."},
    {"Connect an AI tool",
     "Conectar una herramienta de IA"},
    {"Connect an AI tool...",
     "Conectar una herramienta de IA..."},
    {"Copied to the clipboard",
     "Copiado al portapapeles"},
    {"Fields:",
     "Campos:"},
    {"For Gemini Code Assist Standard or Enterprise, or a Gemini API key. Run once in a terminal.",
     "Para Gemini Code Assist Standard o Enterprise, o una clave de la API de Gemini. Ejecútalo una vez en una terminal."},
    {"Google's CLI for personal Google accounts (free, Google AI Pro and Ultra). Add this to its MCP file, then sign in with Google.",
     "La CLI de Google para cuentas personales de Google (gratis, Google AI Pro y Ultra). Añade esto a su archivo MCP e inicia sesión con Google."},
    {"In a terminal:",
     "En una terminal:"},
    {"Other tools",
     "Otras herramientas"},
    {"Program tab > Install > Edit mcp.json, then paste this.",
     "Pestaña Program > Install > Edit mcp.json, y pega esto."},
    {"Rovo in Atlassian's web apps only takes public HTTPS servers added by an admin: use Rovo Dev CLI.",
     "Rovo en las aplicaciones web de Atlassian solo acepta servidores HTTPS públicos añadidos por un administrador: usa Rovo Dev CLI."},
    {"Run once in a terminal (Cline CLI), or paste into Cline's MCP settings (MCP Servers > Configure).",
     "Ejecútalo una vez en una terminal (Cline CLI), o pégalo en los ajustes MCP de Cline (MCP Servers > Configure)."},
    {"Run once in a terminal, or add to the user MCP file.",
     "Ejecútalo una vez en una terminal, o añádelo al archivo MCP de usuario."},
    {"Run once in a terminal, or open the command MCP: Open User Configuration and add the entry.",
     "Ejecútalo una vez en una terminal, o abre el comando MCP: Open User Configuration y añade la entrada."},
    {"Run once in a terminal, then choose Sign in with ChatGPT when Codex starts.",
     "Ejecútalo una vez en una terminal y elige Sign in with ChatGPT cuando Codex se inicie."},
    {"Run once in a terminal.",
     "Ejecútalo una vez en una terminal."},
    {"Run once in a terminal; it signs in with your Claude plan.",
     "Ejecútalo una vez en una terminal; inicia sesión con tu plan de Claude."},
    {"Save as a file in your workspace's .continue/mcpServers folder; tools work in agent mode.",
     "Guárdalo como archivo en la carpeta .continue/mcpServers de tu espacio de trabajo; las herramientas funcionan en modo agent."},
    {"Settings > Agents > MCP servers > Add, then paste this.",
     "Settings > Agents > MCP servers > Add, y pega esto."},
    {"Settings > MCP Servers > +, then fill in these fields.",
     "Settings > MCP Servers > +, y rellena estos campos."},
    {"Settings > Tools > AI Assistant > Model Context Protocol (MCP): Add, choose STDIO and paste this.",
     "Settings > Tools > AI Assistant > Model Context Protocol (MCP): Add, elige STDIO y pega esto."},
    {"Since June 2026 personal Google accounts (free, Google AI Pro, Google AI Ultra) use Antigravity CLI; Gemini CLI is for Gemini Code Assist and API keys.",
     "Desde junio de 2026 las cuentas personales de Google (gratis, Google AI Pro, Google AI Ultra) usan Antigravity CLI; Gemini CLI es para Gemini Code Assist y claves de API."},
    {"The ChatGPT app only connects to remote (HTTPS) servers, and SculptForge doesn't run one: nothing of yours listens on the network. Codex CLI runs on your computer and signs in with your ChatGPT plan.",
     "La aplicación de ChatGPT solo se conecta a servidores remotos (HTTPS), y SculptForge no ejecuta ninguno: nada tuyo escucha en la red. Codex CLI se ejecuta en tu ordenador e inicia sesión con tu plan de ChatGPT."},
    {"The Grok app (web, phone, X) can't reach programs on your computer, only public HTTPS servers: use Grok Build. xAI's documentation doesn't say which plans Grok Build's sign-in includes; it also takes an xAI API key.",
     "La aplicación de Grok (web, móvil, X) no llega a los programas de tu ordenador, solo a servidores HTTPS públicos: usa Grok Build. La documentación de xAI no dice qué planes incluye el inicio de sesión de Grok Build; también acepta una clave de la API de xAI."},
    {"Their documentation",
     "Su documentación"},
    {"Turn this off in Preferences > AI.",
     "Desactívalo en Preferencias > IA."},
    {"Let an AI tool (Claude Code, Claude Desktop, Cursor and others) work in this window while you watch: pick your tool, copy its command, turn on live mode. The AI blocks out a starting point; you do the art. Nothing listens on the network.",
     "Deja que una herramienta de IA (Claude Code, Claude Desktop, Cursor y otras) trabaje en esta ventana mientras miras: elige tu herramienta, copia su comando y activa el modo en vivo. La IA prepara un punto de partida; el arte lo haces tú. Nada escucha en la red."},
    {"User guide: AI agents",
     "Guía del usuario: agentes de IA"},
    {"Your own agent in Python (uses the OpenAI API with an API key).",
     "Tu propio agente en Python (usa la API de OpenAI con una clave de API)."},
    {"Your own code with the OpenAI, Gemini or xAI API: sculptforge-cli --schema prints every tool as JSON (examples in the user guide, Scripting > AI agents).",
     "Tu propio código con la API de OpenAI, Gemini o xAI: sculptforge-cli --schema imprime cada herramienta como JSON (ejemplos en la guía del usuario, Scripting > AI agents)."},
    {"Your own code with the xAI API: sculptforge-cli --schema gives the tool definitions (examples in the user guide, Scripting > AI agents). API usage is billed by xAI.",
     "Tu propio código con la API de xAI: sculptforge-cli --schema da las definiciones de las herramientas (ejemplos en la guía del usuario, Scripting > AI agents). xAI factura el uso de la API."},
    {"xAI's coding agent (the grok command). Run once in a terminal; it signs in with your xAI account in the browser.",
     "El agente de programación de xAI (el comando grok). Ejecútalo una vez en una terminal; inicia sesión con tu cuenta de xAI en el navegador."},
    // --- Studio controls: Preferences > AI (PanelAi.cpp, ai/Providers.cpp, core/ManagedPolicy.cpp) ---
    {"AI tools (MCP)", "Herramientas de IA (MCP)"},
    {"AI tools can't connect: your studio turned them off.", "Las herramientas de IA no pueden conectarse: tu estudio las desactivó."},
    {"AI tools may use only the kinds of command your studio allows.", "Las herramientas de IA solo pueden usar los tipos de comando que permite tu estudio."},
    {"AI tools work on their own scene; your studio doesn't let them control this window.", "Las herramientas de IA trabajan en su propia escena; tu estudio no les permite controlar esta ventana."},
    {"API key", "Clave de API"},
    {"Tier", "Nivel"},
    {"Off until you add your own key: hyper3d.ai > API dashboard (a Creator plan or higher).", "Desactivado hasta que añadas tu propia clave: hyper3d.ai > API dashboard (plan Creator o superior)."},
    {"Using your key from the system keychain.", "Usando tu clave del llavero del sistema."},
    {"Sends your image or description to Hyper3D (api.hyper3d.com), which builds the model. Each generation is billed to your Hyper3D account. Basic API access covers Extreme Low and Low.", "Envía tu imagen o descripción a Hyper3D (api.hyper3d.com), que crea el modelo. Cada generación se cobra en tu cuenta de Hyper3D. El acceso básico a la API incluye Extreme Low y Low."},
    {"Audit log", "Registro de auditoría"},
    {"Connecting...", "Conectando..."},
    {"Copy code", "Copiar código"},
    {"Copy path", "Copiar ruta"},
    {"Endpoint", "Endpoint"},
    {"Every AI action (AI tools, AI Brush, Generate 3D) is recorded on this computer: time, tool, command and settings, never your artwork or keys.", "Cada acción de IA (herramientas de IA, AI Brush, Generar 3D) se registra en este equipo: hora, herramienta, comando y ajustes, nunca tus obras ni tus claves."},
    {"Find models", "Buscar modelos"},
    {"In your browser, sign in and enter this code:", "En tu navegador, inicia sesión y escribe este código:"},
    {"Managed by", "Gestionado por"},
    {"Managed by your studio", "Gestionado por tu estudio"},
    {"Model", "Modelo"},
    {"Open the sign-in page", "Abrir la página de inicio de sesión"},
    {"Provider", "Proveedor"},
    {"Requests go only to the endpoint above. Keys are kept in your system keychain (never in a settings file); leave empty to use the environment (ANTHROPIC_API_KEY, OPENAI_API_KEY). For private, offline use, pick Ollama or LM Studio.", "Las solicitudes van solo al endpoint de arriba. Las claves se guardan en el llavero del sistema (nunca en un archivo de ajustes); déjala vacía para usar el entorno (ANTHROPIC_API_KEY, OPENAI_API_KEY). Para un uso privado y sin conexión, elige Ollama o LM Studio."},
    {"Saved in your system keychain.", "Guardada en el llavero del sistema."},
    {"SculptForge has no telemetry: nothing about you or your work is collected.", "SculptForge no tiene telemetría: no se recopila nada sobre ti ni sobre tu trabajo."},
    {"Sign in with your studio account", "Iniciar sesión con la cuenta del estudio"},
    {"Signed in as", "Sesión iniciada como"},
    {"Signed out of your studio account", "Sesión de la cuenta del estudio cerrada"},
    {"Signs in through your studio's own login page in your browser. Nothing on this computer listens for it; the sign-in is kept in your system keychain.", "Inicia sesión con la página de acceso de tu estudio en tu navegador. Nada en este equipo queda a la escucha; la sesión se guarda en el llavero del sistema."},
    {"Studio sign-in", "Inicio de sesión del estudio"},
    {"The studio policy can't be used, so AI, network features and AI tools are off:", "La política del estudio no se puede usar, así que la IA, las funciones de red y las herramientas de IA están desactivadas:"},
    {"Uses your studio sign-in (Preferences > AI > Studio sign-in)", "Usa el inicio de sesión del estudio (Preferencias > IA > Inicio de sesión del estudio)"},
    {"Using the key from the environment.", "Se usa la clave del entorno."},
    {"Your studio requires the sign-in for AI features.", "Tu estudio exige iniciar sesión para usar la IA."},
    {"Free - no account or key", "Gratis - sin cuenta ni clave"},
    {"Anthropic (Claude)", "Anthropic (Claude)"},
    {"OpenAI", "OpenAI"},
    {"Ollama (this computer)", "Ollama (este equipo)"},
    {"LM Studio (this computer)", "LM Studio (este equipo)"},
    {"vLLM server", "Servidor vLLM"},
    {"llama.cpp server", "Servidor llama.cpp"},
    {"LiteLLM proxy (studio gateway)", "Proxy LiteLLM (pasarela del estudio)"},
    {"OpenRouter", "OpenRouter"},
    {"Other OpenAI-compatible server", "Otro servidor compatible con OpenAI"},
    {"An open model hosted by Pollinations.ai. Only your brush description and settings are sent.", "Un modelo abierto alojado por Pollinations.ai. Solo se envían la descripción y los ajustes del pincel."},
    {"Your Anthropic API key (console.anthropic.com).", "Tu clave de API de Anthropic (console.anthropic.com)."},
    {"Your OpenAI API key (platform.openai.com); then Find models.", "Tu clave de API de OpenAI (platform.openai.com); luego, Buscar modelos."},
    {"Private: the model runs on this computer. No key.", "Privado: el modelo se ejecuta en este equipo. Sin clave."},
    {"Private: start LM Studio's server (Developer tab), then Find models. No key.", "Privado: inicia el servidor de LM Studio (pestaña Developer) y luego Buscar modelos. Sin clave."},
    {"A vLLM server (vllm serve <model>): this computer or a GPU server at your studio.", "Un servidor vLLM (vllm serve <modelo>): este equipo o un servidor GPU de tu estudio."},
    {"llama-server -m model.gguf: this computer or a server at your studio. No key.", "llama-server -m model.gguf: este equipo o un servidor de tu estudio. Sin clave."},
    {"Your studio's gateway: one address for every model your studio allows (Bedrock, Vertex AI, Azure OpenAI and others behind it). A virtual key, or your studio sign-in.", "La pasarela de tu estudio: una dirección para todos los modelos que permite tu estudio (Bedrock, Vertex AI, Azure OpenAI y otros detrás). Una clave virtual o el inicio de sesión del estudio."},
    {"Many models through one key (openrouter.ai/keys).", "Muchos modelos con una sola clave (openrouter.ai/keys)."},
    {"Any server with an OpenAI-style /v1/chat/completions endpoint.", "Cualquier servidor con un endpoint /v1/chat/completions al estilo de OpenAI."},
    {"Your studio's policy doesn't allow AI requests to this address", "La política de tu estudio no permite solicitudes de IA a esta dirección"},
    {"AI features are turned off by your studio's policy", "La política de tu estudio desactivó las funciones de IA"},
    {"Rate limited to a request every few seconds; if it can't be reached, a running Ollama is used instead.", "Limitado a una solicitud cada pocos segundos; si no se puede conectar, se usa Ollama si está en marcha."},
    {"Your studio doesn't allow this provider: pick another", "Tu estudio no permite este proveedor: elige otro"},
    // Review fixes (fix/review-voidart-2)
    {"Ctrl+Z brings them back.", "Ctrl+Z los recupera."},
    {"Remove Levels Above", "Quitar niveles superiores"},
    {"Remove Levels Below", "Quitar niveles inferiores"},
    {"Delete view", "Borrar vista"},
    {"Destructive: four times the polygons as one plain mesh, with no lower levels to step back to. On a Part with subdivision levels it removes them (it asks first; Ctrl+Z brings them back). To keep levels, use Add Level instead.", "Destructivo: cuatro veces los polígonos en una sola malla, sin niveles inferiores a los que volver. En una Parte con niveles de subdivisión los quita (pregunta antes; Ctrl+Z los recupera). Para conservar niveles, usa Añadir nivel."},
    {"Don't ask again", "No volver a preguntar"},
    {"Dynamic needs one plain mesh: Freeze keeps the level shown and removes the others (Ctrl+Z brings them back).", "Dinámica necesita una sola malla: Congelar conserva el nivel mostrado y quita los demás (Ctrl+Z los recupera)."},
    {"Extract makes a shell from the masked area: mask it first (Ctrl+drag on the model).", "Extraer crea una cáscara del área enmascarada: enmascárala primero (Ctrl+arrastrar sobre el modelo)."},
    {"Forget this saved view.", "Olvidar esta vista guardada."},
    {"Freeze Levels + Dynamic", "Congelar niveles + Dinámica"},
    {"Freeze Levels and Turn On Dynamic", "Congelar niveles y activar Dinámica"},
    {"Go back to the view saved or restored last (View > Views).", "Volver a la última vista guardada o restaurada (Vista > Vistas)."},
    {"Go to this saved view (Numpad 0 goes to the last one used).", "Ir a esta vista guardada (Numpad 0 va a la última usada)."},
    {"How dark masked areas show (also in Preferences > Interface).", "Qué tan oscuras se ven las áreas enmascaradas (también en Preferencias > Interfaz)."},
    {"How dark masked areas show: 0 = not at all, 1 = nearly black. The default keeps the form readable under the mask.", "Qué tan oscuras se ven las áreas enmascaradas: 0 = nada, 1 = casi negro. El valor por defecto deja leer la forma bajo la máscara."},
    {"Keep the subdivision level shown as a plain mesh, delete the other levels and turn Dynamic on (one undo step for the levels: Ctrl+Z brings them back).", "Conserva el nivel de subdivisión mostrado como una malla simple, borra los demás niveles y activa Dinámica (un solo paso de deshacer para los niveles: Ctrl+Z los recupera)."},
    {"Mask Darkness", "Oscuridad de máscara"},
    {"Mask Darkness %.2f", "Oscuridad de máscara %.2f"},
    {"Mask darkness", "Oscuridad de máscara"},
    {"Mask darkness %.2f", "Oscuridad de máscara %.2f"},
    {"Mask first (Ctrl+drag)", "Enmascara primero (Ctrl+arrastrar)"},
    {"Non-destructive: add a subdivision level: four times the polygons, and the lower levels are kept, so you can step down to shape big forms and back up to the detail. Quads stay quads (Catmull-Clark). Smooth off divides without smoothing.", "No destructivo: añade un nivel de subdivisión: cuatro veces los polígonos, y los niveles inferiores se conservan, así puedes bajar para dar forma a lo grande y volver a subir al detalle. Los quads siguen siendo quads (Catmull-Clark). Con Suavizar apagado divide sin suavizar."},
    {"Remember this camera as a named view (View > Views; the name field names it). Saved with the project.", "Recordar esta cámara como una vista con nombre (Vista > Vistas; el campo de nombre la nombra). Se guarda con el proyecto."},
    {"Restore View", "Restaurar vista"},
    {"Save View", "Guardar vista"},
    {"Saved view", "Vista guardada"},
    {"This Part has %d subdivision levels (%s).", "Esta Parte tiene %d niveles de subdivisión (%s)."},
    {"This removes your %d subdivision levels. Undo brings them back.", "Esto quita tus %d niveles de subdivisión. Deshacer los recupera."},
    {"Views", "Vistas"},
    {"name (optional)", "nombre (opcional)"},
    // --- Preferences > AI and live mode's confirm reasons (fix/i18n-ai-prefs) ---
    {"AI Brush and Generate 3D. Off: they are hidden and SculptForge makes no AI network calls.",
     "Pincel IA y Generar 3D. Desactivado: se ocultan y SculptForge no hace ninguna llamada de red de IA."},
    {"AI services & privacy",
     "Servicios de IA y privacidad"},
    {"Set by this computer (SF_AI or --no-ai).",
     "Lo fija este equipo (SF_AI o --no-ai)."},
    {"Your art is never uploaded or used for training. AI features run only when you use them and can be turned off.",
     "Tu arte nunca se sube ni se usa para entrenar. Las funciones de IA solo se ejecutan cuando las usas y se pueden desactivar."},
    {"Hugging Face token (hf_...)",
     "Token de Hugging Face (hf_...)"},
    {"Connects to the network",
     "Se conecta a la red"},
    {"Runs a macro or script",
     "Ejecuta una macro o un script"},
    {"Runs outside programs (splat training)",
     "Ejecuta programas externos (entrenamiento de splats)"},
    {"Deletes a file from your library",
     "Borra un archivo de tu biblioteca"},
    {"Writes a program or settings file: %s",
     "Escribe un programa o un archivo de configuración: %s"},
    {"Overwrites %s",
     "Sobrescribe %s"},
    {"Writes %s (outside the project folder and the files you picked)",
     "Escribe %s (fuera de la carpeta del proyecto y de los archivos que elegiste)"},
    {"Reads %s (outside the project folder and the files you picked)",
     "Lee %s (fuera de la carpeta del proyecto y de los archivos que elegiste)"},
    {"Saves over the project",
     "Guarda sobre el proyecto"},
    {"Replaces the scene: your unsaved changes would be lost",
     "Reemplaza la escena: se perderían los cambios sin guardar"},
    // --- Labels that were shown without tr() until 11-06 (coverage.py --raw-check), Generate 3D first ---
    {"Hyper3D Rodin (direct) - paid, your key", "Hyper3D Rodin (directo): de pago, tu clave"},
    {"Gen-2.5 Extreme Low (fastest)", "Gen-2.5 Extreme Low (el más rápido)"},
    {"Gen-2.5 Extreme High (more credits)", "Gen-2.5 Extreme High (más créditos)"},
    {"Hide the Parts in %s", "Ocultar las Piezas de %s"},
    {"Show the Parts in %s", "Mostrar las Piezas de %s"},
    {"%d frames, %.1f s",
     "%d fotogramas, %.1f s"},
    {"%s  connected",
     "%s  conectado"},
    {"%s: not seen for %.0f s",
     "%s: sin verse desde hace %.0f s"},
    {"Fill Layer",
     "Capa de relleno"},
    {"Filter alphas",
     "Filtrar alphas"},
    {"Filter shortcuts and gestures",
     "Filtrar atajos y gestos"},
    {"New",
     "Nuevo"},
    {"Save now",
     "Guardar ahora"},
    {"Sculpted before? Take the quick tour",
     "¿Ya has esculpido? Haz el recorrido rápido"},
    {"Search actions",
     "Buscar acciones"},
    {"Search the guide (Ctrl+F)",
     "Buscar en la guía (Ctrl+F)"},
    {"See memberships",
     "Ver membresías"},
    {"Show library folder",
     "Mostrar la carpeta de la biblioteca"},
    {"Show me",
     "Muéstramelo"},
    {"Start from a head",
     "Empezar con una cabeza"},
    {"Start the 10-minute lesson",
     "Empezar la lección de 10 minutos"},
    {"Waiting for your phone...",
     "Esperando a tu teléfono..."},
    {"  Designing brush%.*s",
     "  Diseñando el pincel%.*s"},
    {"%.1f%% of texels found no surface: raise Front / Rear",
     "El %.1f%% de los téxeles no encontró superficie: sube Delante / Detrás"},
    {"%d commands",
     "%d comandos"},
    {"%d items",
     "%d elementos"},
    {"%dx%d in %.1f s from %zu tris",
     "%dx%d en %.1f s desde %zu triángulos"},
    {"%dx%d texture, %d layer",
     "Textura de %dx%d, %d capa"},
    {"%dx%d texture, %d layers",
     "Textura de %dx%d, %d capas"},
    {"%s  -  %s unpacked",
     "%s  -  %s descomprimido"},
    {"%s %zu branch",
     "%s %zu rama"},
    {"%s %zu branches",
     "%s %zu ramas"},
    {"%s elapsed",
     "%s transcurrido"},
    {"%s elapsed  -  %d photos",
     "%s transcurrido  -  %d fotos"},
    {"%s elapsed, about %s left",
     "%s transcurrido, quedan unos %s"},
    {"%s of memory free now (%s)",
     "%s de memoria libre ahora (%s)"},
    {"%s on '%s'",
     "%s en '%s'"},
    {"%s: %s, %s, %d%%\nClick: on / off. Right-click: blend and opacity.",
     "%s: %s, %s, %d%%\nClic: activar / desactivar. Clic derecho: fusión y opacidad."},
    {"%s: roughness %.2f, metallic %.0f",
     "%s: rugosidad %.2f, metálico %.0f"},
    {"%zu points, height %.3g, reach %.2f radii",
     "%zu puntos, altura %.3g, alcance %.2f radios"},
    {"%zu result",
     "%zu resultado"},
    {"%zu results",
     "%zu resultados"},
    {"%zu results (best 50)",
     "%zu resultados (los 50 mejores)"},
    {"%zu splats (SH %d)",
     "%zu splats (SH %d)"},
    {"(contains)  Backspace: undo a letter  Enter: first match",
     "(contiene)  Retroceso: borrar una letra  Intro: primera coincidencia"},
    {"(root selected)",
     "(raíz seleccionada)"},
    {"(sphere %d selected)",
     "(esfera %d seleccionada)"},
    {"1. Install Ollama (or use your package manager, e.g. sudo pacman -S ollama):\n      curl -fsSL https://ollama.com/install.sh | sh\n2. Start it if needed:\n      sudo systemctl enable --now ollama\n3. Download the model:\n      ollama pull qwen3:4b-instruct\n4. Pick Ollama above, then Find models.",
     "1. Instala Ollama (o usa tu gestor de paquetes, p. ej. sudo pacman -S ollama):\n      curl -fsSL https://ollama.com/install.sh | sh\n2. Inícialo si hace falta:\n      sudo systemctl enable --now ollama\n3. Descarga el modelo:\n      ollama pull qwen3:4b-instruct\n4. Elige Ollama arriba y luego Buscar modelos."},
    {"1. Install Ollama from ollama.com/download and drag it to Applications.\n2. Download the model (in Terminal):\n      ollama pull qwen3:4b-instruct\n3. Ollama runs in the menu bar. Pick Ollama above, then Find models.",
     "1. Instala Ollama desde ollama.com/download y arrástralo a Aplicaciones.\n2. Descarga el modelo (en Terminal):\n      ollama pull qwen3:4b-instruct\n3. Ollama se ejecuta en la barra de menús. Elige Ollama arriba y luego Buscar modelos."},
    {"1. Install Ollama from ollama.com/download, or in PowerShell:\n      winget install Ollama.Ollama\n2. Download the model (in PowerShell):\n      ollama pull qwen3:4b-instruct\n3. Ollama runs in the system tray. Pick Ollama above, then Find models.",
     "1. Instala Ollama desde ollama.com/download, o en PowerShell:\n      winget install Ollama.Ollama\n2. Descarga el modelo (en PowerShell):\n      ollama pull qwen3:4b-instruct\n3. Ollama se ejecuta en la bandeja del sistema. Elige Ollama arriba y luego Buscar modelos."},
    {"A live display shows the frame rate, frame times (median, 95th, 99th percentile), how far the stroke trails the pointer, and memory. The result is saved as JSON beside a video of the run:",
     "Una pantalla en vivo muestra los fotogramas por segundo, los tiempos de fotograma (mediana, percentil 95 y 99), cuánto se retrasa el trazo respecto al puntero y la memoria. El resultado se guarda como JSON junto a un vídeo de la prueba:"},
    {"A neutral human head with clean quads and eye and mouth loops, ready for a portrait.",
     "Una cabeza humana neutra con cuadriláteros limpios y bucles de ojos y boca, lista para un retrato."},
    {"A two-second check of the GPU, the CPU and memory, so big scenes stay smooth. Once, then again only when the graphics card, its driver or SculptForge changes. Nothing leaves this computer.",
     "Una comprobación de dos segundos de la GPU, la CPU y la memoria, para que las escenas grandes sigan fluidas. Una vez, y de nuevo solo si cambian la tarjeta gráfica, su controlador o SculptForge. Nada sale de este ordenador."},
    {"AI features:",
     "Funciones de IA:"},
    {"AI provider",
     "Proveedor de IA"},
    {"About %d quads, then 1 subdivision level",
     "Unos %d cuadriláteros, luego 1 nivel de subdivisión"},
    {"Add a brush to the shelf",
     "Añadir un pincel al estante"},
    {"Add to bottom shelf",
     "Añadir al estante inferior"},
    {"All tags",
     "Todas las etiquetas"},
    {"All visible Parts",
     "Todas las piezas visibles"},
    {"Android Chrome says \"This site can't be reached\"\n(ERR_TOO_MANY_RETRIES)? Recent Chrome versions can\nblock every home-network address, even your router.\nUse Firefox on the phone instead and allow it to\nreach local devices when it asks.",
     "¿Chrome en Android dice \"No se puede acceder a este sitio\"\n(ERR_TOO_MANY_RETRIES)? Las versiones recientes de Chrome\npueden bloquear todas las direcciones de la red doméstica,\nincluso el router. Usa Firefox en el teléfono y deja que\nacceda a dispositivos locales cuando lo pida."},
    {"Any local model or script: {input} = image, {output} = .glb/.obj to write. See docs/generators.md.",
     "Cualquier modelo o script local: {input} = imagen, {output} = .glb/.obj a escribir. Consulta docs/generators.md."},
    {"Append another Part first",
     "Añade antes otra pieza"},
    {"Auto",
     "Auto"},
    {"Back to automatic",
     "Volver a automático"},
    {"Back to the tour",
     "Volver al recorrido"},
    {"Background Preview (experimental): a big brush shows under the pen while it's still being worked out.",
     "Vista previa en segundo plano (experimental): un pincel grande se muestra bajo el lápiz mientras aún se calcula."},
    {"Backspace: undo a letter  Enter: first match",
     "Retroceso: borrar una letra  Intro: primera coincidencia"},
    {"Baked: %s (%.1f s, exports with GLB / USD)",
     "Horneado: %s (%.1f s, se exporta con GLB / USD)"},
    {"Be the first: sculptforge.app/support.html",
     "Sé el primero: sculptforge.app/support.html"},
    {"Benchmark results",
     "Resultados del benchmark"},
    {"Big meshes are faster now",
     "Las mallas grandes ahora van más rápido"},
    {"Block out a base mesh from connected spheres, then Make Mesh to sculpt it.",
     "Bloquea una malla base con esferas conectadas y luego usa Crear malla para esculpirla."},
    {"Branch from step %zu: %zu step, ending with \"%s\"\nClick to make it the active branch",
     "Rama desde el paso %zu: %zu paso, termina con \"%s\"\nHaz clic para que sea la rama activa"},
    {"Branch from step %zu: %zu steps, ending with \"%s\"\nClick to make it the active branch",
     "Rama desde el paso %zu: %zu pasos, termina con \"%s\"\nHaz clic para que sea la rama activa"},
    {"Break-up %.2f",
     "Ruptura %.2f"},
    {"Bridge folder: %s",
     "Carpeta de Bridge: %s"},
    {"Brush Lab",
     "Laboratorio de pinceles"},
    {"Brush is a free, open-source Gaussian splat trainer (Apache-2.0) that runs on your graphics card - NVIDIA, AMD, Intel or Apple, no CUDA needed. SculptForge runs it as a separate program.",
     "Brush es un entrenador de splats gaussianos libre y de código abierto (Apache-2.0) que se ejecuta en tu tarjeta gráfica: NVIDIA, AMD, Intel o Apple, sin necesidad de CUDA. SculptForge lo ejecuta como un programa aparte."},
    {"Brushes",
     "Pinceles"},
    {"Brushes are data only (no code), shared under CC0. Installed brushes join your library.",
     "Los pinceles son solo datos (sin código), compartidos bajo CC0. Los pinceles instalados se suman a tu biblioteca."},
    {"Builds a whole model from the description with a generative 3D model, then makes it sculptable.",
     "Crea un modelo completo a partir de la descripción con un modelo 3D generativo y lo deja listo para esculpir."},
    {"Builds the whole model - limbs, face, back - from one picture with a reconstruction model, then makes it sculptable. Characters on a transparent background work best.",
     "Crea el modelo completo (extremidades, cara, espalda) a partir de una sola imagen con un modelo de reconstrucción y lo deja listo para esculpir. Los personajes sobre fondo transparente dan los mejores resultados."},
    {"Built with SDL 3 (zlib), Dear ImGui (MIT), GLM (MIT), stb (MIT/public domain), ufbx (MIT/public domain), xatlas (MIT), MikkTSpace (zlib), the Inter and JetBrains Mono typefaces (SIL OFL 1.1) and the MIT-licensed colours of the Forge Terminal look's schemes. Quad Remesh follows Instant Meshes (BSD-3-Clause). The phone link uses QR Code generator (MIT), libdatachannel and libjuice (MPL-2.0), usrsctp (BSD-3-Clause) and Mbed TLS (Apache-2.0). Uses libcurl (curl license) where available.",
     "Creado con SDL 3 (zlib), Dear ImGui (MIT), GLM (MIT), stb (MIT/dominio público), ufbx (MIT/dominio público), xatlas (MIT), MikkTSpace (zlib), las tipografías Inter y JetBrains Mono (SIL OFL 1.1) y los colores con licencia MIT de los esquemas del aspecto Forge Terminal. Quad Remesh sigue a Instant Meshes (BSD-3-Clause). El enlace con el teléfono usa QR Code generator (MIT), libdatachannel y libjuice (MPL-2.0), usrsctp (BSD-3-Clause) y Mbed TLS (Apache-2.0). Usa libcurl (licencia curl) donde está disponible."},
    {"Bust, full body, hand, foot, ear, eye, stylised and creature heads, torso and bevelled primitives: Library > Tool > Starter meshes.",
     "Busto, cuerpo entero, mano, pie, oreja, ojo, cabezas estilizadas y de criaturas, torso y primitivas biseladas: Biblioteca > Herramienta > Mallas iniciales."},
    {"C over the model picks its colour",
     "C sobre el modelo toma su color"},
    {"C over the model picks its material",
     "C sobre el modelo toma su material"},
    {"COLMAP (BSD licence) works out where each photo was taken, which training needs. SculptForge runs it as a separate program.",
     "COLMAP (licencia BSD) calcula dónde se tomó cada foto, algo que el entrenamiento necesita. SculptForge lo ejecuta como un programa aparte."},
    {"Can't reach the brush gallery: %s",
     "No se puede acceder a la galería de pinceles: %s"},
    {"Capture again",
     "Capturar de nuevo"},
    {"Captures/%s",
     "Capturas/%s"},
    {"Centre and scale to the sculpt",
     "Centrar y escalar a la escultura"},
    {"Check that the gallery folder exists and has an index.json. Brushes you already installed keep working.",
     "Comprueba que la carpeta de la galería existe y tiene un index.json. Los pinceles que ya instalaste siguen funcionando."},
    {"Check your internet connection and try again. Brushes you already installed keep working.",
     "Comprueba tu conexión a internet y vuelve a intentarlo. Los pinceles que ya instalaste siguen funcionando."},
    {"Clay at 25, 60 and 120 px, Move, Smooth, a 240 px brush, then an orbit: the scene the website's table uses (same as sculptforge --benchmark %s)",
     "Clay a 25, 60 y 120 px, Move, Smooth, un pincel de 240 px y luego una órbita: la escena que usa la tabla del sitio web (igual que sculptforge --benchmark %s)"},
    {"Clean floaters",
     "Limpiar flotantes"},
    {"Clear Base",
     "Borrar base"},
    {"Clear chat",
     "Borrar el chat"},
    {"Clear layer",
     "Borrar la capa"},
    {"Click %s.",
     "Haz clic en %s."},
    {"Click a level to inspect it in the viewport.",
     "Haz clic en un nivel para verlo en el visor."},
    {"ComfyUI - your server",
     "ComfyUI: tu servidor"},
    {"Coming from:",
     "Vienes de:"},
    {"Community gallery (or a URL / folder for a studio mirror)",
     "Galería de la comunidad (o una URL / carpeta para una réplica del estudio)"},
    {"Connected (%s), no face for %.0f s",
     "Conectado (%s), sin cara desde hace %.0f s"},
    {"Connected - %d model installed",
     "Conectado: %d modelo instalado"},
    {"Connected - %d models installed",
     "Conectado: %d modelos instalados"},
    {"Contrast %.2f",
     "Contraste %.2f"},
    {"Copy brush code",
     "Copiar el código del pincel"},
    {"Copyright (C) 2026 The SculptForge contributors. This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License, version 3 or later. It comes with ABSOLUTELY NO WARRANTY.",
     "Copyright (C) 2026 Los colaboradores de SculptForge. Este programa es software libre: puedes redistribuirlo y/o modificarlo según los términos de la Licencia Pública General de GNU, versión 3 o posterior. Se ofrece SIN NINGUNA GARANTÍA."},
    {"Create",
     "Crear"},
    {"Custom bridge folder (optional)",
     "Carpeta de Bridge personalizada (opcional)"},
    {"Delete '%s' from your brush library?",
     "¿Eliminar '%s' de tu biblioteca de pinceles?"},
    {"Delete Sphere",
     "Eliminar esfera"},
    {"Describe a brush the way you'd describe it to another sculptor.",
     "Describe un pincel como se lo describirías a otro escultor."},
    {"Describe one object: \"a stylised dragon head with curled horns\", \"a cartoon fox sitting\".",
     "Describe un objeto: \"una cabeza de dragón estilizada con cuernos curvos\", \"un zorro de dibujos animados sentado\"."},
    {"Detected %s, %s -> %s",
     "Detectado %s, %s -> %s"},
    {"DirectX (Y-)",
     "DirectX (Y-)"},
    {"Displacement: mid-grey = 0, white = +%.4g",
     "Desplazamiento: gris medio = 0, blanco = +%.4g"},
    {"Downloading...",
     "Descargando..."},
    {"Draft brushes: always",
     "Pinceles de borrador: siempre"},
    {"Draft brushes: automatic",
     "Pinceles de borrador: automático"},
    {"Draft brushes: never",
     "Pinceles de borrador: nunca"},
    {"Drag the dots to move the landmarks (the other side follows). The lines are the loops Quad Remesh will follow.",
     "Arrastra los puntos para mover las referencias (el otro lado los sigue). Las líneas son los bucles que seguirá Quad Remesh."},
    {"Drop on a shelf to add",
     "Suelta en un estante para añadir"},
    {"Drop on a shelf to move, elsewhere to remove",
     "Suelta en un estante para mover, en otro sitio para quitar"},
    {"Effort",
     "Esfuerzo"},
    {"Either way: Preferences > Interface > Look changes it later.",
     "En cualquier caso: Preferencias > Interfaz > Aspecto lo cambia más tarde."},
    {"Exchange folder: %s",
     "Carpeta de intercambio: %s"},
    {"Export Bridge file...",
     "Exportar archivo de Bridge..."},
    {"Faces down",
     "Mira hacia abajo"},
    {"Faces the camera",
     "Mira a la cámara"},
    {"Faces up",
     "Mira hacia arriba"},
    {"Fit",
     "Ajustar"},
    {"Flip",
     "Voltear"},
    {"Folder: %s",
     "Carpeta: %s"},
    {"Founding Members",
     "Miembros fundadores"},
    {"Free and open-source digital sculpting.",
     "Escultura digital libre y de código abierto."},
    {"From image",
     "Desde imagen"},
    {"From text",
     "Desde texto"},
    {"From text, Hunyuan3D 3.1 Rapid runs. Choose a Rodin model to use Rodin for text.",
     "Desde texto se usa Hunyuan3D 3.1 Rapid. Elige un modelo Rodin para usar Rodin con texto."},
    {"Full 3D model (AI reconstruction)",
     "Modelo 3D completo (reconstrucción con IA)"},
    {"Full guide: Windows, macOS, Linux, LM Studio",
     "Guía completa: Windows, macOS, Linux, LM Studio"},
    {"GLB + blend shapes",
     "GLB + blend shapes"},
    {"GPU memory budget (GB)",
     "Presupuesto de memoria de GPU (GB)"},
    {"Gallery",
     "Galería"},
    {"Gamma %.2f",
     "Gamma %.2f"},
    {"Generate 3D model",
     "Generar modelo 3D"},
    {"Generate LOD chain",
     "Generar cadena de LOD"},
    {"Generate the shapes first.",
     "Genera primero las formas."},
    {"Get a key at fal.ai/dashboard/keys",
     "Consigue una clave en fal.ai/dashboard/keys"},
    {"Get a key at meshy.ai (API settings)",
     "Consigue una clave en meshy.ai (ajustes de API)"},
    {"Getting to know this computer",
     "Conociendo este ordenador"},
    {"Got it",
     "Entendido"},
    {"HF token",
     "Token de HF"},
    {"Height",
     "Altura"},
    {"Height %.2f",
     "Altura %.2f"},
    {"Height alpha",
     "Alpha de altura"},
    {"Hide layer",
     "Ocultar la capa"},
    {"Hold Alt for the scripting parameters.",
     "Mantén Alt para ver los parámetros de script."},
    {"Hold Ctrl over any control to see what it does. The User Guide (Shift+F1) explains every tool, and the quick tour shows what is where.",
     "Mantén Ctrl sobre cualquier control para ver qué hace. La Guía del usuario (Mayús+F1) explica cada herramienta y el recorrido rápido muestra dónde está cada cosa."},
    {"How to share results",
     "Cómo compartir los resultados"},
    {"Hugging Face - free models",
     "Hugging Face: modelos gratuitos"},
    {"Hunyuan3D 2 (Tencent) - free",
     "Hunyuan3D 2 (Tencent): gratis"},
    {"Hunyuan3D 2 mini Turbo (Tencent) - free, fastest",
     "Hunyuan3D 2 mini Turbo (Tencent): gratis, el más rápido"},
    {"Hunyuan3D 2.1 (Tencent) - free, no token",
     "Hunyuan3D 2.1 (Tencent): gratis, sin token"},
    {"Hunyuan3D 3.1 Rapid (Tencent) - fast",
     "Hunyuan3D 3.1 Rapid (Tencent): rápido"},
    {"Hunyuan3D licence",
     "Licencia de Hunyuan3D"},
    {"Hunyuan3D models are under the Tencent Hunyuan Community License, which doesn't cover use in the EU, UK or South Korea. There, choose TRELLIS.2 (free token) or another model.",
     "Los modelos Hunyuan3D están bajo la Tencent Hunyuan Community License, que no cubre su uso en la UE, el Reino Unido ni Corea del Sur. Allí, elige TRELLIS.2 (token gratuito) u otro modelo."},
    {"Import Bridge file...",
     "Importar archivo de Bridge..."},
    {"Imports (or updates) the asset in the open Unreal Editor.\nNeeds the Python Editor Script Plugin and Project Settings > Python > Enable Remote Execution.",
     "Importa (o actualiza) el recurso en el Unreal Editor abierto.\nNecesita el Python Editor Script Plugin y Project Settings > Python > Enable Remote Execution."},
    {"In ComfyUI use Workflow > Export (API). The Load Image node (title it sf_image) gets the picture; the first mesh it saves is imported. See docs/comfyui.md.",
     "En ComfyUI usa Workflow > Export (API). El nodo Load Image (titúlalo sf_image) recibe la imagen; se importa la primera malla que guarde. Consulta docs/comfyui.md."},
    {"In ComfyUI use Workflow > Export (API). The node titled sf_prompt gets the description; the first mesh it saves is imported. See docs/comfyui.md.",
     "En ComfyUI usa Workflow > Export (API). El nodo titulado sf_prompt recibe la descripción; se importa la primera malla que guarde. Consulta docs/comfyui.md."},
    {"In Unreal: Project Settings > Plugins > Python > Advanced:\nMulticast Time-To-Live = 1, Multicast Bind Address = 0.0.0.0",
     "En Unreal: Project Settings > Plugins > Python > Advanced:\nMulticast Time-To-Live = 1, Multicast Bind Address = 0.0.0.0"},
    {"In high %.2f",
     "Entrada alta %.2f"},
    {"In low %.2f",
     "Entrada baja %.2f"},
    {"Inflates the outline into a rounded blob - good for simple shapes, not characters.",
     "Infla el contorno en una masa redondeada: va bien para formas sencillas, no para personajes."},
    {"Insert a copy of another Part into the active one",
     "Insertar una copia de otra pieza en la activa"},
    {"Install .sfbrush...",
     "Instalar .sfbrush..."},
    {"Install a brush someone shared: a brush code (sfbrush1:...) or .sfbrush text on the clipboard (Ctrl+V in the viewport)",
     "Instala un pincel que alguien compartió: un código de pincel (sfbrush1:...) o texto .sfbrush en el portapapeles (Ctrl+V en el visor)"},
    {"Its .sfbrush file is removed. Copy its brush code first if you might want it back.",
     "Se elimina su archivo .sfbrush. Copia antes su código de pincel si quizá lo quieras recuperar."},
    {"Keep my layout",
     "Mantener mi diseño"},
    {"KelvinGrab moves like Move with elastic, volume-keeping falloff. KelvinTwist and KelvinScale act where you press: drag right to turn clockwise or swell, left to turn back or pinch. KelvinSnake pulls tapering horns. Detail Clone stamps the VDM brush's map along the stroke: capture one from the model with Capture from view.",
     "KelvinGrab mueve como Move con una caída elástica que conserva el volumen. KelvinTwist y KelvinScale actúan donde presionas: arrastra a la derecha para girar en sentido horario o hinchar, a la izquierda para girar al revés o pellizcar. KelvinSnake estira cuernos que se afinan. Detail Clone estampa el mapa del pincel VDM a lo largo del trazo: captura uno del modelo con Capturar desde la vista."},
    {"LOD chain (GLB per level)",
     "Cadena de LOD (un GLB por nivel)"},
    {"LODs",
     "LOD"},
    {"Laptop / Budget",
     "Portátil / Económico"},
    {"Learns from frames while the view moves.",
     "Aprende de los fotogramas mientras la vista se mueve."},
    {"Legacy",
     "Clásico"},
    {"Level %.2f",
     "Nivel %.2f"},
    {"Light areas rise: logos, coins, emblems, height maps, texture photos.",
     "Las zonas claras se elevan: logotipos, monedas, emblemas, mapas de altura, fotos de texturas."},
    {"Linux: allow port %d (TCP and UDP) through the\nfirewall, e.g. sudo ufw allow from\n<your network>/24 to any port %d",
     "Linux: permite el puerto %d (TCP y UDP) en el\ncortafuegos, p. ej. sudo ufw allow from\n<tu red>/24 to any port %d"},
    {"Lit Spheres (no folder)",
     "Lit Spheres (sin carpeta)"},
    {"Live over %s, %.0f fps",
     "En vivo por %s, %.0f fps"},
    {"Load",
     "Cargar"},
    {"Loading the brush gallery...",
     "Cargando la galería de pinceles..."},
    {"Lock against painting",
     "Bloquear contra la pintura"},
    {"Make relief",
     "Crear relieve"},
    {"Manual overrides",
     "Ajustes manuales"},
    {"Mask brushes (Ctrl)",
     "Pinceles de máscara (Ctrl)"},
    {"Mask stroke (Ctrl)",
     "Trazo de máscara (Ctrl)"},
    {"Material Channels",
     "Canales del material"},
    {"Maybe later",
     "Quizá más tarde"},
    {"Meshy - paid, your key",
     "Meshy: de pago, tu clave"},
    {"Model id",
     "ID del modelo"},
    {"More starter meshes",
     "Más mallas iniciales"},
    {"More",
     "Más"},
    {"Motion proxy from (M triangles)",
     "Proxy de movimiento desde (M triángulos)"},
    {"Move to folder",
     "Mover a una carpeta"},
    {"My generator command",
     "Mi comando generador"},
    {"Name of the new VDM",
     "Nombre del nuevo VDM"},
    {"Needs the SculptForge add-on in Blender\n(Help > User Guide > Working with Blender).",
     "Necesita el complemento de SculptForge en Blender\n(Ayuda > Guía del usuario > Trabajar con Blender)."},
    {"Needs the SculptForge shelf script in Maya\n(Help > User Guide > Working with Maya).",
     "Necesita el script de estante de SculptForge en Maya\n(Ayuda > Guía del usuario > Trabajar con Maya)."},
    {"Neutral grey panels with one orange accent, and the SculptForge layout: menus by task, a status bar and a one-row tool shelf. Your layout stays as it is unless you try it.",
     "Paneles gris neutro con un acento naranja, y el diseño de SculptForge: menús por tarea, una barra de estado y un estante de herramientas de una fila. Tu diseño se queda como está salvo que lo pruebes."},
    {"New folder...",
     "Nueva carpeta..."},
    {"New from Starter Mesh",
     "Nuevo desde malla inicial"},
    {"New to SculptForge? A quick tour",
     "¿Nuevo en SculptForge? Un recorrido rápido"},
    {"New to SculptForge? A two-minute tour shows where everything is.",
     "¿Nuevo en SculptForge? Un recorrido de dos minutos muestra dónde está todo."},
    {"Next",
     "Siguiente"},
    {"No UVs: Auto UV makes them",
     "Sin UV: Auto UV las crea"},
    {"No UVs: Tool > Texture Image > Auto UV",
     "Sin UV: Herramienta > Imagen de textura > Auto UV"},
    {"No alpha matches \"%s\"",
     "Ningún alpha coincide con \"%s\""},
    {"No brush matches \"%s\"",
     "Ningún pincel coincide con \"%s\""},
    {"No brushes match your search.",
     "Ningún pincel coincide con tu búsqueda."},
    {"No camera? Open this in the phone's browser:",
     "¿Sin cámara? Abre esto en el navegador del teléfono:"},
    {"No default bridge folder on this system;\nBridge files still import and export.",
     "No hay carpeta de Bridge predeterminada en este sistema;\nlos archivos de Bridge se siguen importando y exportando."},
    {"No face found: Place by hand, or Finish without face loops.",
     "No se encontró ninguna cara: Colocar a mano, o Terminar sin bucles faciales."},
    {"No menus",
     "Sin menús"},
    {"No parameters.",
     "Sin parámetros."},
    {"No thanks",
     "No, gracias"},
    {"Noise size %.3f",
     "Tamaño del ruido %.3f"},
    {"Normal view",
     "Vista normal"},
    {"Not enough free memory for %s: it will be skipped. Close other apps first.",
     "No hay memoria libre suficiente para %s: se omitirá. Cierra antes otras aplicaciones."},
    {"Not now",
     "Ahora no"},
    {"Terms & refunds", "Términos y reembolsos"},
    {"Nothing about memberships for a week.",
     "Nada sobre membresías durante una semana."},
    {"Nothing else is sent: no folders, no user name, no model details. Needs the Discord app running.",
     "No se envía nada más: ni carpetas, ni nombre de usuario, ni detalles del modelo. Necesita la aplicación de Discord abierta."},
    {"Off (raw mesh)",
     "Desactivado (malla original)"},
    {"Off in this run: tests, renders and recordings use fixed settings.",
     "Desactivado en esta ejecución: las pruebas, renders y grabaciones usan ajustes fijos."},
    {"Off: choose an exchange folder first.",
     "Desactivado: elige antes una carpeta de intercambio."},
    {"Offline - showing the gallery from your last visit (%s).",
     "Sin conexión: se muestra la galería de tu última visita (%s)."},
    {"Offline and instant, but only a puffy blockout of the outline - not a real reconstruction.",
     "Sin conexión e instantáneo, pero solo un bloqueo hinchado del contorno, no una reconstrucción real."},
    {"Offline blockout (inflate silhouette)",
     "Bloqueo sin conexión (inflar silueta)"},
    {"Offline: no outside connections. Updates come only from your studio's channel.",
     "Sin conexión: ninguna conexión externa. Las actualizaciones llegan solo desde el canal de tu estudio."},
    {"On a dense scan with micro levels, big brushes now keep up with the pen and the view turns smoothly right after a stroke.",
     "En un escaneo denso con micro niveles, los pinceles grandes ahora siguen el ritmo del lápiz y la vista gira con fluidez justo después de un trazo."},
    {"On the phone: %s",
     "En el teléfono: %s"},
    {"On the phone: Face capture. The front camera tracks 52 expressions on the phone; only the numbers come here.",
     "En el teléfono: Captura facial. La cámara frontal sigue 52 expresiones en el teléfono; aquí solo llegan los números."},
    {"One line of text to share in chat, email or a forum",
     "Una línea de texto para compartir en un chat, un correo o un foro"},
    {"Only brushes in your own library can be deleted",
     "Solo se pueden eliminar los pinceles de tu propia biblioteca"},
    {"Onto %s (%zu tris, UVs)",
     "Sobre %s (%zu triángulos, UV)"},
    {"Open or drop a .ply capture (Polycam, Luma, Postshot, Scaniverse...) to sculpt against it.",
     "Abre o suelta una captura .ply (Polycam, Luma, Postshot, Scaniverse...) para esculpir sobre ella."},
    {"Open the User Guide",
     "Abrir la Guía del usuario"},
    {"OpenGL (Y+)",
     "OpenGL (Y+)"},
    {"Opens it in SculptForge; the tour waits at the bottom.",
     "Lo abre en SculptForge; el recorrido espera abajo."},
    {"Opens sculptforge.app/support.html in your browser.",
     "Abre sculptforge.app/support.html en tu navegador."},
    {"Or drop a sketch, concept, logo, photo or height map onto the window.",
     "O suelta un boceto, concepto, logotipo, foto o mapa de altura en la ventana."},
    {"Other file (API format)...",
     "Otro archivo (formato API)..."},
    {"Output folder (default: last export's)",
     "Carpeta de salida (predeterminada: la de la última exportación)"},
    {"Output folder (default: project / library)",
     "Carpeta de salida (predeterminada: proyecto / biblioteca)"},
    {"Own micro level",
     "Micro nivel propio"},
    {"Own top subdivision level",
     "Nivel de subdivisión superior propio"},
    {"Page won't open? Use Firefox on the phone: some Chrome versions block home-network addresses.",
     "¿No se abre la página? Usa Firefox en el teléfono: algunas versiones de Chrome bloquean las direcciones de la red doméstica."},
    {"Pause",
     "Pausa"},
    {"Pen detected",
     "Lápiz detectado"},
    {"Performance settings",
     "Ajustes de rendimiento"},
    {"Phone and computer on the same Wi-Fi. No app to install.",
     "Teléfono y ordenador en la misma Wi-Fi. No hay que instalar ninguna aplicación."},
    {"Pick a look:",
     "Elige un aspecto:"},
    {"Play",
     "Reproducir"},
    {"Preparing the model for the phone...",
     "Preparando el modelo para el teléfono..."},
    {"Press Esc to stop the run.",
     "Pulsa Esc para detener la prueba."},
    {"Pressure -",
     "Presión -"},
    {"Project page",
     "Página del proyecto"},
    {"Projects the image onto the model from the current view as surface detail (uses the active layer).",
     "Proyecta la imagen sobre el modelo desde la vista actual como detalle de superficie (usa la capa activa)."},
    {"Proof mode measures how responsive sculpting is on this computer. For each mesh size it builds a sphere with subdivision levels, then plays a scripted pen stroke through the same code your pen goes through (240 samples a second), with a normal and a large brush, and turns the view.",
     "El modo prueba mide lo ágil que es esculpir en este ordenador. Para cada tamaño de malla crea una esfera con niveles de subdivisión, reproduce un trazo de lápiz programado por el mismo código que usa tu lápiz (240 muestras por segundo), con un pincel normal y uno grande, y gira la vista."},
    {"Q W E R: Draw, Move, Scale, Rotate; Shift+W: All",
     "Q W E R: Dibujar, Mover, Escalar, Rotar; Shift+W: Todo"},
    {"Quad Remesh (default): clean quads ready for sculpting and subdividing, with one subdivision level holding the detail. Voxel remesh: an even, watertight triangle surface. Off: the model as the service made it.",
     "Quad Remesh (predeterminado): cuadriláteros limpios listos para esculpir y subdividir, con un nivel de subdivisión que guarda el detalle. Remallado vóxel: una superficie de triángulos uniforme y cerrada. Desactivado: el modelo tal como lo hizo el servicio."},
    {"Quad Remesh 2",
     "Quad Remesh 2"},
    {"Questions, tips and work in progress: opens the invite in your browser.",
     "Preguntas, consejos y trabajos en curso: abre la invitación en tu navegador."},
    {"Quick shapes (offline)",
     "Formas rápidas (sin conexión)"},
    {"Radius %.0f px",
     "Radio %.0f px"},
    {"Receive",
     "Recibir"},
    {"Received",
     "Recibido"},
    {"Receiving %s (%s)",
     "Recibiendo %s (%s)"},
    {"Reduction",
     "Reducción"},
    {"Refresh",
     "Actualizar"},
    {"Refresh failed: %s",
     "Error al actualizar: %s"},
    {"Regenerate",
     "Regenerar"},
    {"Result: %s",
     "Resultado: %s"},
    {"Reveal in folder",
     "Mostrar en la carpeta"},
    {"Right-click to share, rename or delete",
     "Clic derecho para compartir, renombrar o eliminar"},
    {"Rodin 2.5 Fast (Hyper3D) - cheaper",
     "Rodin 2.5 Fast (Hyper3D): más barato"},
    {"Root sphere",
     "Esfera raíz"},
    {"Run a model on this computer",
     "Ejecutar un modelo en este ordenador"},
    {"Runs a workflow on your own ComfyUI - this computer or a studio GPU server - with whatever models it has (Hunyuan3D, TRELLIS and more). Nothing goes anywhere else.",
     "Ejecuta un flujo de trabajo en tu propio ComfyUI (este ordenador o un servidor GPU del estudio) con los modelos que tenga (Hunyuan3D, TRELLIS y más). Nada va a ningún otro sitio."},
    {"Runs free on Hugging Face's shared GPUs with a small daily allowance. A free token (huggingface.co/settings/tokens, read access) raises it; TRELLIS.2 needs one.",
     "Se ejecuta gratis en las GPU compartidas de Hugging Face con una pequeña cuota diaria. Un token gratuito (huggingface.co/settings/tokens, acceso de lectura) la aumenta; TRELLIS.2 lo necesita."},
    {"Runs on Hugging Face's shared GPUs with your account's free daily allowance.",
     "Se ejecuta en las GPU compartidas de Hugging Face con la cuota diaria gratuita de tu cuenta."},
    {"Same Wi-Fi: not a guest network, not mobile data.",
     "La misma Wi-Fi: ni una red de invitados ni datos móviles."},
    {"Save .sfbrush...",
     "Guardar .sfbrush..."},
    {"Save as .sfbrush...",
     "Guardar como .sfbrush..."},
    {"Save settings",
     "Guardar ajustes"},
    {"Save splats...",
     "Guardar splats..."},
    {"Save your brush from Brush Lab, then open a pull request on the gallery.\nOpens %s",
     "Guarda tu pincel desde el Laboratorio de pinceles y luego abre un pull request en la galería.\nAbre %s"},
    {"Saved in %s/Captures. Only devices on your network can connect, and only while this window is open.",
     "Se guarda en %s/Captures. Solo pueden conectarse dispositivos de tu red, y solo mientras esta ventana esté abierta."},
    {"Scan with your phone's camera",
     "Escanea con la cámara del teléfono"},
    {"Sculpt a stroke to see what it costs.",
     "Esculpe un trazo para ver lo que cuesta."},
    {"Sculpt expressions & correctives;\nexported as glTF morph targets.",
     "Esculpe expresiones y correctivos;\nse exportan como morph targets de glTF."},
    {"Sculpt to splats",
     "Esculpir sobre splats"},
    {"SculptForge has a new look",
     "SculptForge tiene un aspecto nuevo"},
    {"SculptForge is free. A membership adds betas and early builds, a vote on what we build next, Member Requests (one request a month, built first and credited to you) and your name in the credits. %s",
     "SculptForge es gratis. Una membresía añade betas y versiones anticipadas, voto sobre lo próximo que hacemos, Peticiones de miembros (una petición al mes, la hacemos primero y con tu nombre) y tu nombre en los créditos. %s"},
    {"Search brushes (scales, skin, stone...)",
     "Buscar pinceles (escamas, piel, piedra...)"},
    {"Selection brushes (Ctrl+Shift)",
     "Pinceles de selección (Ctrl+Mayús)"},
    {"Selection stroke (Ctrl+Shift)",
     "Trazo de selección (Ctrl+Mayús)"},
    {"Send",
     "Enviar"},
    {"Sending to Unreal...",
     "Enviando a Unreal..."},
    {"Server",
     "Servidor"},
    {"Several networks? Pick the Wi-Fi one under Network.",
     "¿Varias redes? Elige la de Wi-Fi en Red."},
    {"Share yours",
     "Comparte el tuyo"},
    {"Shift snaps (15 deg, grid, 10%). Alt+drag the gizmo: move it only. Alt+click the model: pivot there.",
     "Mayús ajusta (15 grados, cuadrícula, 10%). Alt+arrastrar el gizmo: solo lo mueve. Alt+clic en el modelo: pivote ahí."},
    {"Show layer",
     "Mostrar la capa"},
    {"Show only the active Part",
     "Mostrar solo la pieza activa"},
    {"Show the folder",
     "Mostrar la carpeta"},
    {"Show the memberships",
     "Ver las membresías"},
    {"Show the other Parts again",
     "Volver a mostrar las otras piezas"},
    {"Sign in with GitHub (the member builds' sign-in): no more cards.",
     "Inicia sesión con GitHub (el inicio de sesión de las versiones para miembros): no más tarjetas."},
    {"Silhouette blockout",
     "Bloqueo de silueta"},
    {"Skeletal mesh with %zu morph targets.",
     "Malla esquelética con %zu morph targets."},
    {"Skip",
     "Omitir"},
    {"Softness %.2f",
     "Suavidad %.2f"},
    {"Software (safe mode)",
     "Software (modo seguro)"},
    {"Splat training needs Brush",
     "El entrenamiento de splats necesita Brush"},
    {"Splat training needs COLMAP",
     "El entrenamiento de splats necesita COLMAP"},
    {"Stable Fast 3D (Stability AI) - free, quick and simple",
     "Stable Fast 3D (Stability AI): gratis, rápido y sencillo"},
    {"Stamp from this view",
     "Estampar desde esta vista"},
    {"Stamp onto model",
     "Estampar en el modelo"},
    {"Stamp with Drag Stamp (drag sets size and turn) or Slide Stamp. Intensity 50 presses the map at its own size; Alt digs it in. A VDM moves points sideways, so it needs a dense mesh: divide first, and stamp at the highest subdivision level.",
     "Estampa con Drag Stamp (arrastrar fija tamaño y giro) o Slide Stamp. Intensidad 50 presiona el mapa a su propio tamaño; Alt lo hunde. Un VDM mueve los puntos de lado, así que necesita una malla densa: divide primero y estampa en el nivel de subdivisión más alto."},
    {"Start",
     "Iniciar"},
    {"Static mesh with %d LODs.",
     "Malla estática con %d LOD."},
    {"Step %d: %s (%s)\n%u %s, %s%s\nClick or drag to show the model here; right-click to re-tune",
     "Paso %d: %s (%s)\n%u %s, %s%s\nHaz clic o arrastra para ver el modelo aquí; clic derecho para reajustar"},
    {"Stop",
     "Detener"},
    {"Stop download",
     "Detener la descarga"},
    {"Strokes",
     "Trazos"},
    {"Subdivision limit (M triangles)",
     "Límite de subdivisión (M triángulos)"},
    {"Supporters",
     "Colaboradores"},
    {"TRELLIS.2 (Microsoft) - detailed, open model",
     "TRELLIS.2 (Microsoft): detallado, modelo abierto"},
    {"TRELLIS.2 (Microsoft, MIT) - Best detail (needs token)",
     "TRELLIS.2 (Microsoft, MIT): el mejor detalle (necesita token)"},
    {"TRELLIS.2 needs a free Hugging Face token; without one, Hunyuan3D 2.1 runs instead.",
     "TRELLIS.2 necesita un token gratuito de Hugging Face; sin él se usa Hunyuan3D 2.1."},
    {"Take the tour",
     "Hacer el recorrido"},
    {"Tap View in AR on the phone: the active Part (or the scene) stands on your desk.",
     "Toca Ver en RA en el teléfono: la pieza activa (o la escena) aparece sobre tu mesa."},
    {"Target: automatic",
     "Objetivo: automático"},
    {"Ten short slides: remeshing, dynamic topology, retopology, levels, masking, cut, brushes and baking, each with a Show me button.",
     "Diez diapositivas breves: remallado, topología dinámica, retopología, niveles, máscaras, corte, pinceles y horneado, cada una con un botón Muéstramelo."},
    {"Test connection",
     "Probar la conexión"},
    {"Text to 3D (AI)",
     "Texto a 3D (IA)"},
    {"Thank you to everyone who supports SculptForge:",
     "Gracias a todas las personas que apoyan SculptForge:"},
    {"The %s brush of the same name is used again.",
     "Se vuelve a usar el pincel %s del mismo nombre."},
    {"The %s brush stays as it is; the renamed copy goes into your library.",
     "El pincel %s se queda como está; la copia renombrada va a tu biblioteca."},
    {"The 2.5 GB model needs about 4 GB of free memory and works without a graphics card (10-20 s per brush on a laptop CPU, a few seconds with a GPU).",
     "El modelo de 2,5 GB necesita unos 4 GB de memoria libre y funciona sin tarjeta gráfica (10-20 s por pincel en la CPU de un portátil, unos segundos con GPU)."},
    {"The 52 Apple ARKit blend shapes for rigs and live face capture, fitted to this head.",
     "Las 52 blend shapes de Apple ARKit para rigs y captura facial en vivo, ajustadas a esta cabeza."},
    {"The current scene is replaced. Save your work first.",
     "Se reemplaza la escena actual. Guarda antes tu trabajo."},
    {"The first %d members are Founding Members: a badge, their name in the credits and their price locked for good.",
     "Los primeros %d miembros son Miembros fundadores: una insignia, su nombre en los créditos y su precio fijo para siempre."},
    {"The first %d members, with our thanks for good:",
     "Los primeros %d miembros, con nuestro agradecimiento para siempre:"},
    {"The gallery is empty.",
     "La galería está vacía."},
    {"The page may be closed or the phone asleep: open it again to carry on.",
     "Puede que la página esté cerrada o el teléfono en reposo: ábrela otra vez para seguir."},
    {"The phone can't open the page?",
     "¿El teléfono no puede abrir la página?"},
    {"The phone link couldn't start.",
     "No se pudo iniciar el enlace con el teléfono."},
    {"Theme (64 characters at most)",
     "Tema (64 caracteres como máximo)"},
    {"There is no ready-made %s for this computer. %s",
     "No hay un %s listo para este ordenador. %s"},
    {"Thickness",
     "Grosor"},
    {"Third-party notices",
     "Avisos de terceros"},
    {"This build has no WebRTC, so the phone's face page can't connect.",
     "Esta versión no tiene WebRTC, así que la página facial del teléfono no puede conectarse."},
    {"This code expires in %.0f s",
     "Este código caduca en %.0f s"},
    {"This computer isn't on a local network.",
     "Este ordenador no está en una red local."},
    {"This stays in the Model tooltip.",
     "Esto queda en la información sobre Modelo."},
    {"Threads (next start)",
     "Hilos (próximo inicio)"},
    {"Tier: automatic",
     "Nivel: automático"},
    {"Tools",
     "Herramientas"},
    {"Train from capture",
     "Entrenar desde captura"},
    {"Trainer: Brush is not installed yet (it is offered when you start).",
     "Entrenador: Brush aún no está instalado (se ofrece al empezar)."},
    {"Training - progress is over the viewport",
     "Entrenando: el progreso se muestra sobre el visor"},
    {"Training splat - %s",
     "Entrenando splat - %s"},
    {"Try an idea below, or ask for changes to the selected brush, like \"sharper\" or \"less buildup\".",
     "Prueba una idea de abajo o pide cambios al pincel seleccionado, como \"más nítido\" o \"menos acumulación\"."},
    {"Try it",
     "Probarlo"},
    {"Turn 90",
     "Girar 90"},
    {"Turn off VPNs on the phone and the computer.",
     "Desactiva las VPN del teléfono y del ordenador."},
    {"Turn upside down (most captures are)",
     "Dar la vuelta (la mayoría de capturas lo están)"},
    {"Two settings in Preferences > Performance help most on big meshes:",
     "Dos ajustes de Preferencias > Rendimiento son los que más ayudan con mallas grandes:"},
    {"Type a letter to narrow the brushes, then press its orange key",
     "Escribe una letra para filtrar los pinceles y luego pulsa su tecla naranja"},
    {"Type help and press Enter to list the commands.",
     "Escribe help y pulsa Intro para ver la lista de comandos."},
    {"UVs, no texture yet",
     "UV, aún sin textura"},
    {"Unlock (allow painting)",
     "Desbloquear (permitir pintar)"},
    {"Uploads per frame (MB)",
     "Subidas por fotograma (MB)"},
    {"Use",
     "Usar"},
    {"Using FAL_KEY from the environment.",
     "Se usa FAL_KEY del entorno."},
    {"Using MESHY_API_KEY from the environment.",
     "Se usa MESHY_API_KEY del entorno."},
    {"Value %.2f",
     "Valor %.2f"},
    {"View budget (M triangles)",
     "Presupuesto de la vista (M triángulos)"},
    {"View in AR",
     "Ver en RA"},
    {"View resolution 75%: draws about half the pixels, for integrated graphics and big screens.",
     "Resolución de vista 75%: dibuja más o menos la mitad de los píxeles, para gráficos integrados y pantallas grandes."},
    {"Remove flat background",
     "Quitar el fondo plano"},
    {"Drops the flat plane a picture's background often becomes (a wall behind or ground under the subject) before the model lands. A relief or coin on its own is kept. Undo brings a removed plane back.",
     "Quita el plano en que suele convertirse el fondo de una imagen (una pared detrás o un suelo bajo el sujeto) antes de que llegue el modelo. Un relieve o una moneda solos se conservan. Deshacer recupera un plano quitado."},
    {"Voxel remesh",
     "Remallado vóxel"},
    {"Welcome to SculptForge",
     "Te damos la bienvenida a SculptForge"},
    {"Windows: allow SculptForge on private networks\nwhen the firewall asks.",
     "Windows: permite SculptForge en redes privadas\ncuando el cortafuegos lo pregunte."},
    {"Workflow",
     "Flujo de trabajo"},
    {"Working...",
     "Trabajando..."},
    {"Workstation",
     "Estación de trabajo"},
    {"Your first sculpt: a cute owl head from this sphere, step by step, in about ten minutes.",
     "Tu primera escultura: una tierna cabeza de búho a partir de esta esfera, paso a paso, en unos diez minutos."},
    {"and type the code  %.3s %.3s",
     "y escribe el código  %.3s %.3s"},
    {"by %s  -  %s  -  %s",
     "por %s  -  %s  -  %s"},
    {"e.g. bark of an old oak tree",
     "p. ej. corteza de un roble viejo"},
    {"editable",
     "editable"},
    {"face_weights isn't in this build: the model doesn't move yet.",
     "face_weights no está en esta versión: el modelo aún no se mueve."},
    {"fal.ai - paid, your key",
     "fal.ai: de pago, tu clave"},
    {"help, or a command such as  remesh resolution=200",
     "help, o un comando como  remesh resolution=200"},
    {"high",
     "alto"},
    {"low",
     "bajo"},
    {"medium",
     "medio"},
    {"macOS: allow incoming connections for SculptForge\n(System Settings > Network > Firewall).",
     "macOS: permite las conexiones entrantes de SculptForge\n(Ajustes del Sistema > Red > Firewall)."},
    {"not found (photos without positions need it)",
     "no encontrado (las fotos sin posiciones lo necesitan)"},
    {"not found (videos need it)",
     "no encontrado (los vídeos lo necesitan)"},
    {"not installed",
     "no instalado"},
    {"off",
     "desactivado"},
    {"opacity %.2f",
     "opacidad %.2f"},
    {"weight %.2f",
     "peso %.2f"},
    {"x%.2f / level",
     "x%.2f / nivel"},
    {"triangles",
     "triángulos"},
    {"vertices",
     "vértices"},
    {"Studio mode",
     "Modo estudio"},
    {"Studio build for %s",
     "Versión de estudio para %s"},
    // Brush sets (#143)
    {"Brush set", "Conjunto de pinceles"},
    {"Delete brush set", "Eliminar conjunto de pinceles"},
    {"Delete this brush set. The brushes and the shelf stay as they are.", "Elimina este conjunto de pinceles. Los pinceles y el estante quedan como están."},
    {"Keep the bottom shelf's brushes and their keys as a named set (Wood, Metal, Stone...). Switch between sets in one step from the Brush palette's Brush set menu, the bottom shelf's right-click menu or here.", "Guarda los pinceles del estante inferior y sus teclas como un conjunto con nombre (Madera, Metal, Piedra...). Cambia de conjunto en un paso desde el menú Conjunto de pinceles de la paleta Pincel, el menú del clic derecho del estante inferior o aquí."},
    {"Name, e.g. Wood, Metal or Stone", "Nombre, p. ej. Madera, Metal o Piedra"},
    {"Named bottom shelves with their keys (Wood, Metal, Stone...): pick one to swap the shelf's brushes and their keys in one step. Save keeps the shelf as it is now, with its keys, as a set. Sets live in your brush library; F3 finds them too.", "Estantes inferiores con nombre y sus teclas (Madera, Metal, Piedra...): elige uno para cambiar los pinceles del estante y sus teclas en un paso. Guardar conserva el estante tal como está, con sus teclas, como conjunto. Los conjuntos se guardan en tu biblioteca de pinceles; F3 también los encuentra."},
    {"No brush sets yet", "Aún no hay conjuntos de pinceles"},
    {"Only the set goes: the brushes and the shelf stay as they are.", "Solo se elimina el conjunto: los pinceles y el estante quedan como están."},
    {"Save Shelf as Brush Set", "Guardar estante como conjunto de pinceles"},
    {"none yet", "ninguno todavía"},
    {"this shelf (not saved)", "este estante (sin guardar)"},
    {"%d brush", "%d pincel"},
    {"%d brushes", "%d pinceles"},
    {"Save Brush Set", "Guardar conjunto de pinceles"},
    {"Delete Brush Set?", "¿Eliminar conjunto de pinceles?"},
    {"The bottom shelf's %d brushes, in order, and their keys. Switching to the set later puts both back.", "Los %d pinceles del estante inferior, en orden, y sus teclas. Al cambiar a este conjunto más tarde vuelven ambos."},
    {"Replaces the set %s.", "Reemplaza el conjunto %s."},
    {"Delete the brush set %s?", "¿Eliminar el conjunto de pinceles %s?"},
    {"Switch the bottom shelf to this set's brushes and give them the set's keys.", "Cambia el estante inferior a los pinceles de este conjunto y les da sus teclas."},
    {"%d key", "%d tecla"},
    {"%d keys", "%d teclas"},
    {"Brush set: %s (%s, %s)", "Conjunto de pinceles: %s (%s, %s)"},
    {"Brush set saved: %s (%s, %s)", "Conjunto de pinceles guardado: %s (%s, %s)"},
    {"Brush set updated: %s (%s, %s)", "Conjunto de pinceles actualizado: %s (%s, %s)"},
    {"Brush set deleted: %s (the shelf stays as it is)", "Conjunto de pinceles eliminado: %s (el estante queda como está)"},
    {"No brush set called %s", "No hay ningún conjunto de pinceles llamado %s"},
    {"No brush sets yet: Save Shelf as Brush Set makes one", "Aún no hay conjuntos de pinceles: Guardar estante como conjunto de pinceles crea uno"},
    {"A brush set's name must be 1 to 64 characters", "El nombre de un conjunto de pinceles debe tener de 1 a 64 caracteres"},
    // --- Alpha > Stencil (#155, StencilOverlay.cpp) ---
    {"Stencil",
     "Plantilla"},
    {"Load Stencil...",
     "Cargar plantilla..."},
    {"Show Stencil",
     "Mostrar plantilla"},
    {"Adjust Stencil",
     "Ajustar plantilla"},
    {"Use alpha",
     "Usar alpha"},
    {"Tile",
     "Repetir"},
    {"Paint its colours",
     "Pintar sus colores"},
    {"Hide while navigating",
     "Ocultar al navegar"},
    {"Reset placement",
     "Restablecer posición"},
    {"Lay an image over the view and sculpt or paint through it: the sculpt brushes take its brightness (white = full effect), Vertex Paint its colours, only where it covers the model.",
     "Pon una imagen sobre la vista y esculpe o pinta a través de ella: los pinceles de esculpir toman su brillo (blanco = efecto completo), Pintura de vértices sus colores, solo donde cubre el modelo."},
    {"Hidden, the Stencil is off: the brushes work as if it weren't there.",
     "Oculta, la plantilla está apagada: los pinceles trabajan como si no estuviera."},
    {"Handles on the Stencil: drag inside it to move it, a corner to scale it, the round handle to turn it (Shift: 15 degree steps). Arrow keys move it, - and = scale it, comma and period turn it (Shift: finely). Enter or Esc goes back to sculpting.",
     "Tiradores en la plantilla: arrastra dentro para moverla, una esquina para escalarla, el tirador redondo para girarla (Shift: pasos de 15 grados). Las flechas la mueven, - y = la escalan, coma y punto la giran (Shift: con precisión). Enter o Esc vuelve a esculpir."},
    {"Make the brush's alpha (above, or one from the Library) the Stencil.",
     "Usa el alpha del pincel (arriba, o uno de la Biblioteca) como plantilla."},
    {"Lay an image over the view, then sculpt or paint through it. Move it and repeat.",
     "Pon una imagen sobre la vista y esculpe o pinta a través de ella. Muévela y repite."},
    {"Take the Stencil off the view.",
     "Quita la plantilla de la vista."},
    {"Use the image's negative: black works, white leaves the surface alone.",
     "Usa el negativo de la imagen: el negro trabaja, el blanco deja la superficie como está."},
    {"Repeat the image across the whole view, for skin, fabric and other patterns.",
     "Repite la imagen por toda la vista, para piel, tela y otros patrones."},
    {"Vertex Paint lays the image's own colours. Off, its brightness masks the paint colour instead.",
     "Pintura de vértices pone los colores de la imagen. Desactivado, su brillo enmascara el color de pintura."},
    {"Fade the Stencil while the view turns, moves or zooms, so you can see the model under it.",
     "Atenúa la plantilla mientras la vista gira, se mueve o hace zoom, para ver el modelo debajo."},
    {"How strongly the Stencil shows. The brushes always take all of it.",
     "Cuánto se ve la plantilla. Los pinceles siempre la toman entera."},
    {"The Stencil's height, in percent of the view's height.",
     "La altura de la plantilla, en porcentaje de la altura de la vista."},
    {"Turn the Stencil, in degrees counter-clockwise; Ctrl+click to type.",
     "Gira la plantilla, en grados en sentido antihorario; Ctrl+clic para escribir."},
    {"Centre the Stencil again, upright, 80% of the view's height.",
     "Centra la plantilla de nuevo, derecha, al 80% de la altura de la vista."},
    // --- Maya Live and the Live link (PanelMaya.cpp, Shell.cpp) ---
    {"Keep Maya in step as you sculpt: after each stroke (once you pause for 300 ms), the Parts it changed go to the exchange folder by themselves, and what Maya sends loads by itself. Still files only: no network. Turn on Live on Maya's SculptForge shelf too.",
     "Mantén Maya al día mientras esculpes: tras cada trazo (en cuanto pausas 300 ms), las Piezas que cambió van solas a la carpeta de intercambio, y lo que envía Maya se carga solo. Solo archivos: sin red. Activa también Live en el estante de SculptForge de Maya."},
    {"Live link",
     "Enlace en vivo"},
    {"Maya follows your strokes as you draw them, over a connection on this computer (the shelf's Link button). Asks first, every time; off again at every start.",
     "Maya sigue tus trazos mientras los dibujas, mediante una conexión en este ordenador (el botón Link del estante). Pregunta antes, cada vez; se desactiva en cada inicio."},
    {"Live link: connected to %s",
     "Enlace en vivo: conectado a %s"},
    {"Live link on: waiting for Maya",
     "Enlace en vivo activado: esperando a Maya"},
    {"Pairing code: %s",
     "Código de emparejamiento: %s"},
    {"Copy pairing code",
     "Copiar código de emparejamiento"},
    {"Turn the Live link off",
     "Desactivar el enlace en vivo"},
    {"Turn on the Maya Live link?",
     "¿Activar el enlace en vivo con Maya?"},
    {"The Live link lets the SculptForge shelf in Maya follow your strokes as you sculpt.",
     "El enlace en vivo permite que el estante de SculptForge en Maya siga tus trazos mientras esculpes."},
    {"It opens a connection on this computer.",
     "Abre una conexión en este ordenador."},
    {"Anything running on this computer that knows the pairing code could send meshes into SculptForge. It carries meshes only: never commands, files or scripts. Other computers and web pages can't reach it.",
     "Cualquier programa de este ordenador que conozca el código de emparejamiento podría enviar mallas a SculptForge. Solo transporta mallas: nunca comandos, archivos ni scripts. Otros ordenadores y las páginas web no pueden alcanzarlo."},
    {"Turn it off when you're not using it: click \"Live link on\" in the top bar. It is off again every time SculptForge starts.",
     "Desactívalo cuando no lo uses: haz clic en \"Enlace en vivo activado\" en la barra superior. Se desactiva cada vez que SculptForge se inicia."},
    {"Turn on",
     "Activar"},
    {"Live link on",
     "Enlace en vivo activado"},
    {"Click to turn the Maya Live link off.",
     "Haz clic para desactivar el enlace en vivo con Maya."},
    {"Top bar",
     "Barra superior"},
    {"Status bar",
     "Barra de estado"},
    {"%s: flat shading. Click: as the scene", "%s: sombreado plano. Clic: como la escena"},
    {"%s: shading as the scene (%s). Click: Smooth", "%s: sombreado como la escena (%s). Clic: Suave"},
    {"%s: smooth shading. Click: Flat", "%s: sombreado suave. Clic: Plano"},
    {"Delete Masked", "Eliminar enmascarado"},
    {"Delete masked", "Eliminar enmascarados"},
    {"Delete the masked triangles (average mask above 50%); undo brings them back", "Elimina los triángulos enmascarados (máscara media por encima del 50%); deshacer los recupera"},
    {"Delete the masked triangles (the average mask of their corners above 50%) of the active Part. Ctrl+Z brings them back.", "Elimina los triángulos enmascarados (la máscara media de sus vértices por encima del 50%) de la pieza activa. Ctrl+Z los recupera."},
    {"Move the hidden triangles of the active Part into a new Part just below it, shown. One undo step.", "Mueve los triángulos ocultos de la pieza activa a una pieza nueva justo debajo, visibles. Un solo paso de deshacer."},
    {"Move the masked triangles (the average mask of their corners above 50%) into a new, unmasked Part just below. One undo step.", "Mueve los triángulos enmascarados (la máscara media de sus vértices por encima del 50%) a una pieza nueva sin máscara justo debajo. Un solo paso de deshacer."},
    {"Move the triangles that are not masked into a new Part just below; the masked ones stay. One undo step.", "Mueve los triángulos sin máscara a una pieza nueva justo debajo; los enmascarados se quedan. Un solo paso de deshacer."},
    {"Split Hidden", "Separar ocultos"},
    {"Split Masked", "Separar enmascarados"},
    {"Split Unmasked", "Separar no enmascarados"},
    {"flat", "plano"},
    {"smooth", "suave"},
    {"Hidden", "Ocultos"},
    {"Masked", "Enmascarados"},
    {"Unmasked", "Sin máscara"},
    {"Split hidden: the hidden triangles become a new Part", "Separar ocultos: los triángulos ocultos pasan a una pieza nueva"},
    {"Split masked: the masked triangles (average mask above 50%) become a new Part", "Separar enmascarados: los triángulos enmascarados (máscara media por encima del 50%) pasan a una pieza nueva"},
    {"Split unmasked: the unmasked triangles become a new Part; the masked ones stay", "Separar sin máscara: los triángulos sin máscara pasan a una pieza nueva; los enmascarados se quedan"},
    {"Keep original", "Conservar original"},
    {"Split copies the triangles into the new Part and leaves this Part as it is", "Separar copia los triángulos a la pieza nueva y deja esta pieza como está"},
    {"%zu point will merge", "%zu punto se fusionará"},
    {"%zu points will merge", "%zu puntos se fusionarán"},
    {"Check", "Comprobar"},
    {"Find Problems", "Buscar problemas"},
    {"Count what makes the active Part hard to sculpt, remesh, print or export: non-manifold edges, holes, "
     "duplicate, degenerate and flipped triangles. Changes nothing; Tool > Geometry > Find Problems lists each "
     "with its Fix.",
     "Cuenta lo que hace difícil esculpir, remallar, imprimir o exportar la Pieza activa: aristas no variedad, agujeros, "
     "triángulos duplicados, degenerados e invertidos. No cambia nada; Herramienta > Geometría > Buscar problemas "
     "muestra cada uno con su Corregir."},
    {"Degenerate triangles", "Triángulos degenerados"},
    {"Distance", "Distancia"},
    {"Duplicate triangles", "Triángulos duplicados"},
    {"Edges shared by three or more triangles. Fix gives each extra triangle its own copy of the edge, so it comes loose; nothing is deleted.", "Aristas compartidas por tres o más triángulos. Corregir da a cada triángulo de más su propia copia de la arista, así queda suelto; no se elimina nada."},
    {"Fix", "Corregir"},
    {"Fix all", "Corregir todo"},
    {"Fix every problem listed except holes, as one undo step, checking again until none is left. Holes stay open: a mesh can be open on purpose (use their own Fix to cap them). Whatever can't be fixed is listed below the rows, with why.", "Corrige todos los problemas de la lista salvo los agujeros, en un solo paso de deshacer, comprobando de nuevo hasta que no quede ninguno. Los agujeros quedan abiertos: una malla puede estar abierta a propósito (usa su propio Corregir para taparlos). Lo que no se pueda corregir aparece bajo las filas, con el motivo."},
    {"Not fixed:", "Sin corregir:"},
    {"Flipped triangles", "Triángulos invertidos"},
    {"Holes", "Agujeros"},
    {"Isolated points", "Puntos aislados"},
    {"Let go of the slider to count", "Suelta el deslizador para contar"},
    {"Look for what makes a mesh hard to sculpt, remesh, print or export. Checking changes nothing; each Fix changes only what its row names, as one undo step.", "Busca lo que hace difícil esculpir, remallar, imprimir o exportar una malla. Comprobar no cambia nada; cada Corregir cambia solo lo que nombra su fila, en un solo paso de deshacer."},
    {"Merge", "Fusionar"},
    {"Merge by Distance", "Fusionar por distancia"},
    {"Merge the points of the active Part that are closer than the distance into one (an unwelded import, seams left by another program). Triangles that collapse are removed. One undo step.", "Fusiona en uno los puntos de la pieza activa que están más cerca que la distancia (una importación sin soldar, costuras que dejó otro programa). Los triángulos que colapsan se eliminan. Un solo paso de deshacer."},
    {"No problems found", "No se encontraron problemas"},
    {"Non-manifold edges", "Aristas no múltiples"},
    {"Not checked yet", "Aún sin comprobar"},
    {"Open boundaries. Fix caps each one with a patch, as Fill Holes does.", "Bordes abiertos. Corregir tapa cada uno con un parche, como Cerrar agujeros."},
    {"Points no triangle uses. Fix removes them.", "Puntos que ningún triángulo usa. Corregir los elimina."},
    {"Points this close (in scene units) or closer merge. 0 merges only points exactly on top of each other. The points that would merge show on the model in orange.", "Se fusionan los puntos a esta distancia (en unidades de escena) o menos. 0 fusiona solo los puntos exactamente superpuestos. Los puntos que se fusionarían se ven en naranja sobre el modelo."},
    {"The mesh changed: check again", "La malla cambió: vuelve a comprobar"},
    {"Triangles on the same three points as another. Fix deletes the copies.", "Triángulos sobre los mismos tres puntos que otro. Corregir elimina las copias."},
    {"Triangles that use one point twice. Fix deletes them.", "Triángulos que usan un punto dos veces. Corregir los elimina."},
    {"Triangles whose three points are in one line. Fix collapses their shortest edge.", "Triángulos cuyos tres puntos están en una línea. Corregir colapsa su arista más corta."},
    {"Triangles wound against their neighbours, or whole closed pieces inside out. Fix turns them to match.", "Triángulos orientados al revés que sus vecinos, o piezas cerradas enteras del revés. Corregir los gira para que coincidan."},
    {"Zero-area triangles", "Triángulos de área cero"},
    // Compact bars (@factoberplay)
    {"%zu branch",
     "%zu rama"},
    {"%zu branches",
     "%zu ramas"},
    {"Compact timeline",
     "Línea de tiempo compacta"},
    {"Drag to make the Library's thumbnails bigger or smaller.",
     "Arrastra para agrandar o achicar las miniaturas de la Biblioteca."},
    {"Fold the header row into the track's row: a thinner strip.",
     "Pliega la fila de cabecera en la fila de la pista: una tira más delgada."},
    {"Full timeline",
     "Línea de tiempo completa"},
    {"Library size",
     "Tamaño de la Biblioteca"},
    {"Show the timeline with its header row (Preferences > Interface > Compact timeline).",
     "Muestra la línea de tiempo con su fila de cabecera (Preferencias > Interfaz > Línea de tiempo compacta)."},
    {"Size of the Library's thumbnails (64 by default); dragging the strip's edge toward the canvas does the same.",
     "Tamaño de las miniaturas de la Biblioteca (64 por defecto); arrastrar el borde de la tira hacia el lienzo hace lo mismo."},
    {"The Sculpt Timeline as one thin row: the step count, the track and icon buttons (the default). Off: the full strip, with a header row above the track. Saved automatically.",
     "La Línea de tiempo de esculpido en una sola fila delgada: el número de pasos, la pista y botones de icono (por defecto). Desactivado: la tira completa, con una fila de cabecera sobre la pista. Se guarda automáticamente."},
    {"Thumbnail size",
     "Tamaño de miniatura"},
    {"Thumbnails %.0f px",
     "Miniaturas %.0f px"},
    // Membership moments (PanelSupportCard.cpp)
    {"Member", "Miembro"},
    {"You've opened SculptForge %d times. Members keep it free and growing.",
     "Has abierto SculptForge %d veces. Los miembros lo mantienen gratis y en crecimiento."},
    {"You've sculpted %d hours in SculptForge. Members keep it free and growing.",
     "Has esculpido %d horas en SculptForge. Los miembros lo mantienen gratis y en crecimiento."},
    {"You've saved your work %d times in SculptForge. Members keep it free and growing.",
     "Has guardado tu trabajo %d veces en SculptForge. Los miembros lo mantienen gratis y en crecimiento."},
    {"Members funded this month: %s.",
     "Financiado por los miembros este mes: %s."},
    {"Founding Members so far: %d of %d. A badge, your name in the credits and your price locked for good.",
     "Miembros fundadores hasta ahora: %d de %d. Una insignia, tu nombre en los créditos y tu precio fijo para siempre."},
    {"%d change in this release was a member's request.",
     "%d cambio de esta versión fue una petición de un miembro."},
    {"%d changes in this release were members' requests.",
     "%d cambios de esta versión fueron peticiones de miembros."},
    {"Thank you for being a member: you keep SculptForge free and growing.",
     "Gracias por ser miembro: mantienes SculptForge gratis y en crecimiento."},
    {"SculptForge is free and open source. Members keep it free and growing (Help > Support SculptForge).",
     "SculptForge es libre y de código abierto. Los miembros lo mantienen gratis y en crecimiento (Ayuda > Apoya a SculptForge)."},
    // Recent colours (Oslen on Discord)
    {"Recent colour",
     "Color reciente"},
    {"A colour you painted with lately: click to paint with it again.",
     "Un color con el que pintaste hace poco: haz clic para volver a pintar con él."},
    // --- Tube (Tool > Tube, PanelTube.cpp) ---
    // (Shape, Point spacing, Simplify: GABO on Discord)
    {"A flat ribbon, lying flat towards you as you draw: straps, cloth strips, leaves, hair cards (for new tubes, and the active Tube Part). Thickness sets how flat.",
     "Una cinta plana, tendida hacia ti mientras dibujas: correas, tiras de tela, hojas, mechones de pelo (para los tubos nuevos y la pieza Tube activa). Grosor ajusta lo plana que es."},
    {"A round cross-section (for new tubes, and the active Tube Part).",
     "Una sección redonda (para los tubos nuevos y la pieza Tube activa)."},
    {"A square cross-section with sharp corners: beams, frames, planks (for new tubes, and the active Tube Part).",
     "Una sección cuadrada con esquinas vivas: vigas, marcos, tablones (para los tubos nuevos y la pieza Tube activa)."},
    {"A triangular cross-section: ridges, blades, spines (for new tubes, and the active Tube Part).",
     "Una sección triangular: crestas, filos, espinas (para los tubos nuevos y la pieza Tube activa)."},
    {"Circle",
     "Círculo"},
    {"Fewer points: takes out about a third of them, the ones the curve needs least (the ends stay). Press again for fewer still; undo brings them back.",
     "Menos puntos: quita alrededor de un tercio, los que la curva menos necesita (los extremos se quedan). Pulsa otra vez para quitar más; deshacer los recupera."},
    {"How far apart a drawn tube's points are, in tube radii: higher gives fewer points, easier to edit; lower follows the stroke more closely.",
     "Cuánto se separan los puntos de un tubo dibujado, en radios del tubo: más alto da menos puntos, más fáciles de editar; más bajo sigue el trazo más de cerca."},
    {"How much of the length narrows to a point at the end (0: none, 1: all of it).",
     "Cuánta parte de la longitud se estrecha hasta una punta al final (0: nada, 1: toda)."},
    {"How much of the length narrows to a point at the start (0: none, 1: all of it): horns, claws, spikes.",
     "Cuánta parte de la longitud se estrecha hasta una punta al inicio (0: nada, 1: toda): cuernos, garras, púas."},
    {"How thick the ribbon is, as a fraction of its width.",
     "Qué tan gruesa es la cinta, como fracción de su anchura."},
    {"Point spacing",
     "Separación de puntos"},
    {"Point spacing %.2f",
     "Separación de puntos %.2f"},
    {"Ribbon",
     "Cinta"},
    {"Simplify",
     "Simplificar"},
    {"Square",
     "Cuadrado"},
    {"A hollow tube with open ends: sleeves, pipes, bracelets, hoses (for new tubes, and the active Tube Part). Spread sets how thick the wall is.",
     "Un tubo hueco con los extremos abiertos: mangas, tuberías, pulseras, mangueras (para tubos nuevos y la Pieza Tube activa). Extensión fija el grosor de la pared."},
    {"Braid",
     "Trenza"},
    {"Each stroke makes a Curve Part: a guide line you can draw on or over the surface, move its points, Resample, and then turn into a tube with Tube from curve.",
     "Cada trazo crea una Pieza Curve: una línea guía que puedes dibujar sobre la superficie o sobre ella, mover sus puntos, remuestrear y luego convertir en tubo con Tubo desde curva."},
    {"Make the active Curve Part a tube, with the Shape, Ends and modifiers set here. Undo brings the curve back.",
     "Convierte la Pieza Curve activa en un tubo, con la forma, los extremos y los modificadores de aquí. Deshacer devuelve la curva."},
    {"Replace the curve's points with exactly this many, evenly spaced along it (the ends stay). Fewer points give a smoother, easier to edit curve; more follow it closely.",
     "Sustituye los puntos de la curva por exactamente esta cantidad, repartidos por igual a lo largo de ella (los extremos se quedan). Menos puntos dan una curva más suave y fácil de editar; más la siguen de cerca."},
    {"Resample",
     "Remuestrear"},
    {"Resample %.0f points",
     "Remuestrear %.0f puntos"},
    {"Rope",
     "Cuerda"},
    {"Rope and braid: how far apart the strands are (0.5: touching). Sleeve: how thick the wall is.",
     "Cuerda y trenza: cuánto se separan los hilos (0.5: se tocan). Manga: el grosor de la pared."},
    {"Sleeve",
     "Manga"},
    {"Three flat strands woven across each other: plaits, straps, braided hair (for new tubes, and the active Tube Part). Spread sets how far apart the strands are.",
     "Tres hilos planos entrelazados: trenzas, correas, pelo trenzado (para tubos nuevos y la Pieza Tube activa). Extensión fija cuánto se separan los hilos."},
    {"Three strands wound round the curve: rope, cable, twine. Spread sets how far apart the strands are, Twist and Spin turn them (for new tubes, and the active Tube Part).",
     "Tres hilos enrollados alrededor de la curva: cuerda, cable, cordel. Extensión fija cuánto se separan los hilos; Torcer y Giro los giran (para tubos nuevos y la Pieza Tube activa)."},
    {"Tube from curve",
     "Tubo desde curva"},
    {"Turns the cross-section a little more along the tube, so it has turned this many degrees by the far end: twisted ribbons, beams and ropes.",
     "Gira la sección un poco más a lo largo del tubo, de modo que en el extremo final haya girado estos grados: cintas, vigas y cuerdas retorcidas."},
    {"Turns the cross-section steadily, this many degrees per tube radius of length: a screw thread, a barley twist.",
     "Gira la sección de forma constante, estos grados por cada radio del tubo de longitud: una rosca, un fuste salomónico."},
    {"Spread", "Extensión"},
    {"Spread %.2f", "Extensión %.2f"},
    {"Wall", "Pared"},
    {"Wall %.2f", "Pared %.2f"},
    {"Taper end",
     "Afinar final"},
    {"Taper end %.2f",
     "Afinar final %.2f"},
    {"Taper start",
     "Afinar inicio"},
    {"Taper start %.2f",
     "Afinar inicio %.2f"},
    {"Thickness %.2f",
     "Grosor %.2f"},
    {"Triangle",
     "Triángulo"},
    {"Turn %.0f\xc2\xb0",
     "Girar %.0f\xc2\xb0"},
    {"Turns the cross-section about the curve, in degrees.",
     "Gira la sección alrededor de la curva, en grados."},
    {"Delete Point", "Borrar punto"},
    {"Ends", "Extremos"},
    {"Even edges", "Aristas uniformes"},
    {"Fixed", "Fija"},
    {"Insert Point", "Insertar punto"},
    {"Distance %.2f", "Distancia %.2f"},
    {"New Part", "Nueva pieza"},
    {"(point %d selected)", "(punto %d seleccionado)"},
    {"%d points", "%d puntos"},
    {"Pressure %.2f", "Presión %.2f"},
    {"Radius %.4g", "Radio %.4g"},
    {"Draw Tube", "Dibujar Tube"},
    {"Result", "Resultado"},
    {"Rounded", "Redondeado"},
    {"Select a Tube Part to edit its points.", "Selecciona una pieza Tube para editar sus puntos."},
    {"Smoothing %.2f", "Suavizado %.2f"},
    {"Snap to surfaces", "Ajustar a superficies"},
    {"Add a point halfway between the selected point and the next one.",
     "Añade un punto a medio camino entre el punto seleccionado y el siguiente."},
    {"Bake re-sweeps the tube in triangles of one size (good for dynamic topology) instead of keeping its quads.",
     "Bake vuelve a generar el tubo en triángulos de un solo tamaño (bueno para la topología dinámica) en lugar de mantener sus cuadriláteros."},
    {"Delete the selected point (the one last dragged, in orange). Alt+click a point does the same.",
     "Borra el punto seleccionado (el último que arrastraste, en naranja). Alt+clic en un punto hace lo mismo."},
    {"Drag anywhere in the view to draw a clay tube in the air, like a 3D pen: on the model or off it. Pen pressure makes it thinner, Symmetry X draws the mirror copy. Alt+drag still moves the view.",
     "Arrastra en cualquier lugar de la vista para dibujar un tubo de arcilla en el aire, como un bolígrafo 3D: sobre el modelo o fuera de él. La presión del lápiz lo hace más fino; Simetría X dibuja la copia reflejada. Alt+arrastrar sigue moviendo la vista."},
    {"Each stroke goes into the active Part, with even edge lengths close to that Part's own, ready for dynamic topology.",
     "Cada trazo entra en la pieza activa, con aristas uniformes cercanas a las de esa pieza, listo para la topología dinámica."},
    {"Each stroke makes a Tube Part whose points stay editable until Bake.",
     "Cada trazo crea una pieza Tube cuyos puntos siguen siendo editables hasta Bake."},
    {"Flat ends (for new tubes, and the active Tube Part).",
     "Extremos planos (para los tubos nuevos y la pieza Tube activa)."},
    {"How much a light touch of the pen thins the tube. 0: the same thickness all along.",
     "Cuánto adelgaza el tubo un toque ligero del lápiz. 0: el mismo grosor en toda su longitud."},
    {"How much the stroke's wobble is smoothed out before the tube is made.",
     "Cuánto se suaviza el temblor del trazo antes de crear el tubo."},
    {"How thick new tubes are, as a radius on screen at the depth you draw at.",
     "El grosor de los tubos nuevos, como radio en pantalla a la profundidad en que dibujas."},
    {"Make the active Tube Part a plain mesh to sculpt. Its points can't be edited after this (undo brings them back).",
     "Convierte la pieza Tube activa en una malla normal para esculpir. Después ya no se pueden editar sus puntos (deshacer los recupera)."},
    {"Round ends (for new tubes, and the active Tube Part).",
     "Extremos redondos (para los tubos nuevos y la pieza Tube activa)."},
    {"Smoothing", "Suavizado"},
    {"Strokes are drawn at a fixed distance from the camera (Depth below); orbit to draw at another angle.",
     "Los trazos se dibujan a una distancia fija de la cámara (Distancia, abajo); orbita para dibujar desde otro ángulo."},
    {"Strokes are drawn at the depth of the surface under the pen where they start; started off the model, at the depth where the last stroke ended (or the pivot).",
     "Los trazos se dibujan a la profundidad de la superficie bajo el lápiz donde empiezan; si empiezan fuera del modelo, a la profundidad donde terminó el último trazo (o la del pivote)."},
    {"Strokes are drawn on the plane through the view's pivot, facing you.",
     "Los trazos se dibujan en el plano que pasa por el pivote de la vista, de cara a ti."},
    {"Where the pen is over a surface, the tube lies on it (half sunk in), and off it the stroke carries on in the air from where it left the surface. Off: every point lies on the drawing plane.",
     "Donde el lápiz está sobre una superficie, el tubo se apoya en ella (medio hundido), y fuera de ella el trazo sigue en el aire desde donde dejó la superficie. Desactivado: todos los puntos quedan en el plano de dibujo."},
    {"%s, %s: %s render profile",
     "%s, %s: perfil de render %s"},
    {"Change it any time in",
     "Cámbialo cuando quieras en"},
    {"Compute",
     "Cálculo"},
    {"Everything this computer can safely give to sculpting speed, in one click: antialiasing off and a lighter view while you sculpt and orbit (full quality the moment you stop), and the top budgets of the tier this machine carries. It never goes past what the memory holds. Off puts everything back as you set it. Remembered on this computer.",
     "Todo lo que este equipo puede dar con seguridad a la velocidad de esculpido, con un clic: sin antialiasing y una vista más ligera mientras esculpes y orbitas (calidad completa en cuanto paras), y los presupuestos máximos del nivel que soporta esta máquina. Nunca pasa de lo que cabe en memoria. Al apagarlo todo vuelve a como lo tenías. Se recuerda en este equipo."},
    {"Graphics card",
     "Tarjeta gráfica"},
    {"Performance tier",
     "Nivel de rendimiento"},
    {"Put this card away. It doesn't come back on this computer; the tier is in Preferences > Performance.",
     "Guarda esta tarjeta. No vuelve a aparecer en este ordenador; el nivel está en Preferencias > Rendimiento."},
    {"Preferences > Performance",
     "Preferencias > Rendimiento"},
    {"Render profile",
     "Perfil de render"},
    {"Render profile: automatic",
     "Perfil de render: automático"},
    {"The hardware tier SculptForge runs at on this computer. Click for what it turns on and to change it (Preferences > Performance).",
     "El nivel de hardware con el que SculptForge funciona en este equipo. Haz clic para ver qué activa y cambiarlo (Preferencias > Rendimiento)."},
    {"The hardware tier this computer runs at, why, and what it turns on (Preferences > Performance).",
     "El nivel de hardware de este equipo, por qué y qué activa (Preferencias > Rendimiento)."},
    {"Tuned for your machine",
     "Ajustado para tu equipo"},
    {"Turned on:",
     "Activado:"},
    {"What this computer can carry beyond the essentials: bigger Focus Detail caches, 16K textures and the levels to come switch on by themselves where they fit, never where they don't. Every tier sculpts smoothly; nothing is sent anywhere.",
     "Lo que este equipo puede llevar más allá de lo esencial: cachés de Focus Detail más grandes, texturas de 16K y los niveles que vendrán se activan solos donde caben, nunca donde no. Todos los niveles esculpen con fluidez; no se envía nada a ningún sitio."},
    {"What this turns on:",
     "Qué activa:"},
    {"Where the heavy work runs: on the processor (CPU) or the graphics card (GPU). Auto takes the GPU only where it measured faster on this computer, and never on the Essential tier. If a GPU path fails, that task switches to the CPU and says so once. Tasks without a GPU path yet stay on the CPU.",
     "Dónde se hace el trabajo pesado: en el procesador (CPU) o en la tarjeta gráfica (GPU). Auto usa la GPU solo donde midió que es más rápida en este equipo, y nunca en el nivel Essential. Si una ruta de GPU falla, esa tarea pasa a la CPU y lo dice una vez. Las tareas que aún no tienen ruta de GPU se quedan en la CPU."},
    {"Which graphics card draws SculptForge, on computers with two (a laptop's integrated and discrete GPU, or two cards). Auto takes the fastest discrete one. It applies the next time SculptForge starts, through the system's own setting (Windows' graphics preference; on Linux, Mesa's DRI_PRIME or NVIDIA's render offload). If that card can't start, SculptForge falls back to the system's choice and says so.",
     "Qué tarjeta gráfica dibuja SculptForge en equipos con dos (la GPU integrada y la dedicada de un portátil, o dos tarjetas). Auto elige la dedicada más rápida. Se aplica la próxima vez que SculptForge arranca, mediante el ajuste del propio sistema (la preferencia de gráficos de Windows; en Linux, DRI_PRIME de Mesa o el render offload de NVIDIA). Si esa tarjeta no puede arrancar, SculptForge vuelve a la elección del sistema y lo dice."},
    {"free",
     "libre"},
    {"RAM used by SculptForge",
     "RAM que usa SculptForge"},
    {"RAM free",
     "RAM libre"},
    {"Graphics card memory in use (all programs)",
     "Memoria de la tarjeta gráfica en uso (todos los programas)"},
    {"Graphics card memory free",
     "Memoria libre de la tarjeta gráfica"},
    {"This graphics card doesn't report its memory use",
     "Esta tarjeta gráfica no informa de su uso de memoria"},
    {"Updated every second",
     "Se actualiza cada segundo"},
    // Sandboxed plugins (PanelPlugins.cpp, PanelPacks.cpp)
    {"%s %s by %s, from %s",
     "%s %s de %s, del paquete %s"},
    {"%s plugin, %s KB",
     "Complemento %s, %s KB"},
    {"Add Part",
     "Añadir pieza"},
    {"Defaults",
     "Valores predeterminados"},
    {"Nothing in this pack runs on install or when a project opens. Its plugins run sandboxed, only when you run them, with time and memory limits; macros list their commands and ask before they first run.",
     "Nada de este paquete se ejecuta al instalarlo ni al abrir un proyecto. Sus complementos se ejecutan aislados, solo cuando tú los ejecutas, con límites de tiempo y memoria; las macros muestran sus comandos y preguntan antes de ejecutarse por primera vez."},
    {"Permissions: none. It sees only the mesh it's given and returns a mesh: no files, network, clock or system access.",
     "Permisos: ninguno. Solo ve la malla que recibe y devuelve una malla: sin acceso a archivos, red, reloj ni sistema."},
    {"Plugins",
     "Complementos"},
    {"Running... %.1f s",
     "Ejecutando... %.1f s"},
    {"Sandboxed: no files, network or system access; time and memory limits.",
     "Aislado: sin acceso a archivos, red ni sistema; límites de tiempo y memoria."},
    {"Settings:",
     "Ajustes:"},
    {"Turn on again",
     "Volver a activar"},
    {"Turned off:",
     "Desactivado:"},
    {"Deformer",
     "Deformador"},
    {"Generator",
     "Generador"},
    // Generate 3D: candidates and the sculpting base (PanelGenerate3D.cpp)
    {"%d of %d ready - %s",
     "%d de %d listos - %s"},
    {"%zu tris",
     "%zu triángulos"},
    {"Candidates",
     "Candidatos"},
    {"Discard all",
     "Descartar todos"},
    {"Each candidate is one generation on your key: %d are billed.",
     "Cada candidato es una generación con tu clave: se cobran %d."},
    {"For experts: the model as generated stays as a hidden Part right below the sculpting base. Divide the base, then Project Detail from it to bring fine detail back.",
     "Para expertos: el modelo tal como se generó queda como una pieza oculta justo debajo de la base de esculpido. Divide la base y luego usa Project Detail desde ella para recuperar el detalle fino."},
    {"Generate a model and pick one first.",
     "Primero genera un modelo y elige uno."},
    {"How many shapes to make at once, shown side by side so you can pick the strongest form before you spend time sculpting it.",
     "Cuántas formas crear a la vez, mostradas una junto a otra para que elijas la forma más sólida antes de dedicar tiempo a esculpirla."},
    {"Judge the outline (the black shapes), not the detail: a strong silhouette sculpts well. The one you pick becomes a Part.",
     "Juzga el contorno (las formas negras), no el detalle: una silueta fuerte se esculpe bien. El que elijas se convierte en una pieza."},
    {"Keep a high-detail copy (hidden)",
     "Conservar una copia con todo el detalle (oculta)"},
    {"Keep the others as hidden Parts",
     "Conservar los demás como piezas ocultas"},
    {"Moves the sculpting base onto the generated surface. Best after a Divide or two.",
     "Lleva la base de esculpido sobre la superficie generada. Mejor después de dividir una o dos veces."},
    {"Off: the ones you don't pick are discarded. On: they stay in the Parts list, hidden, to compare or use later.",
     "Desactivado: los que no elijas se descartan. Activado: quedan ocultos en la lista de piezas, para compararlos o usarlos después."},
    {"Pick the strongest form",
     "Elige la forma más sólida"},
    {"Pick %s",
     "Elegir %s"},
    {"Project Detail from the high-detail copy",
     "Project Detail desde la copia con todo el detalle"},
    {"Same shape as %s",
     "Misma forma que %s"},
    {"Sculpting base",
     "Base de esculpido"},
    {"The AI gives the form; SculptForge gives the topology. Use as sculpting base rebuilds the picked model as a clean, even mesh to sculpt on.",
     "La IA da la forma; SculptForge da la topología. Usar como base de esculpido reconstruye el modelo elegido como una malla limpia y uniforme sobre la que esculpir."},
    {"Then Quad Remesh",
     "Después Quad Remesh"},
    {"Use as sculpting base",
     "Usar como base de esculpido"},
    {"Voxel Remesh first makes one even, watertight surface. Quad Remesh then turns it into clean quads with one subdivision level holding the detail, ready to divide and sculpt. Off: the even triangle surface.",
     "Remallado vóxel crea primero una superficie uniforme y cerrada. Luego Quad Remesh la convierte en cuadriláteros limpios con un nivel de subdivisión que guarda el detalle, lista para dividir y esculpir. Desactivado: la superficie uniforme de triángulos."},
    {"Ready",
     "Listo"},
    {"Failed",
     "Falló"},
    {"Reading the model...",
     "Leyendo el modelo..."},
    {"Sculpting base: Voxel Remesh, then Quad Remesh...",
     "Base de esculpido: remallado vóxel y después Quad Remesh..."},
    {"Sculpting base: Voxel Remesh...",
     "Base de esculpido: remallado vóxel..."},
    // --- Tool > Geometry > Edges (PanelEdges.cpp, #108) ---
    {"Crease", "Pliegue"},
    {"Crease %.2f", "Pliegue %.2f"},
    {"Crease the selected edges by the Crease value (1 = fully sharp), for subdivision. Creased edges show tinted on the base level.", "Pliega las aristas seleccionadas con el valor de Pliegue (1 = totalmente afilado), para la subdivisión. Las aristas plegadas se ven teñidas en el nivel base."},
    {"How sharp Crease makes the selected edges: 1 is fully sharp, lower values are softer.", "Cuánto afila Pliegue las aristas seleccionadas: 1 es totalmente afilado; los valores menores son más suaves."},
    {"Remove the crease from the selected edges.", "Quita el pliegue de las aristas seleccionadas."},
    {"Uncrease", "Quitar pliegue"},
    {"creased", "con pliegue"},
    {"Add every edge that touches a selected one.", "Añade cada arista que toca una seleccionada."},
    {"Add the open (border) edges: the rims of holes and of open surfaces.", "Añade las aristas abiertas (de borde): los bordes de los agujeros y de las superficies abiertas."},
    {"Deselect All", "Deseleccionar todo"},
    {"Deselect every edge.", "Deselecciona todas las aristas."},
    {"Drop the selected edges at the loose ends of the selection (Shrink takes back what Grow added; closed loops keep every edge).", "Quita las aristas seleccionadas en los extremos sueltos de la selección (Contraer deshace lo que Expandir añadió; los bucles cerrados conservan todas sus aristas)."},
    {"Edges", "Aristas"},
    {"Edges mode", "Modo aristas"},
    {"Edges mode selects edges of the base level", "El modo aristas selecciona aristas del nivel base"},
    {"Only within selection", "Solo dentro de la selección"},
    {"Open Edges", "Aristas abiertas"},
    {"Select by Angle", "Seleccionar por ángulo"},
    {"Select by Angle keeps only the edges among those selected now (e.g. a loop's sharp stretch).", "Seleccionar por ángulo conserva solo las aristas entre las seleccionadas ahora (p. ej., el tramo afilado de un bucle)."},
    {"Select by Angle takes the edges where the faces on either side meet at more than this many degrees (0 = flat). 30 finds the hard edges of most models; lower finds softer ones too.", "Seleccionar por ángulo toma las aristas donde las caras de cada lado forman más de estos grados (0 = plano). 30 encuentra las aristas duras de la mayoría de los modelos; menos encuentra también las más suaves."},
    {"Select edges of the base level on the model, for edge creases: click an edge, Shift+click adds, Ctrl+click removes, double-click takes its edge loop. A Part with subdivision levels steps down to Level 1 (its base) first. Click the button again to sculpt.", "Selecciona aristas del nivel base sobre el modelo, para pliegues de aristas: clic en una arista, Mayús+clic añade, Ctrl+clic quita, doble clic toma su bucle de aristas. Una Pieza con niveles de subdivisión baja primero al nivel 1 (su base). Pulsa el botón otra vez para esculpir."},
    {"Select every edge where the faces on either side meet at more than the Angle (30 degrees by default): the hard edges of a model. With Only within selection it keeps just those among the edges selected now.", "Selecciona cada arista donde las caras de cada lado forman más que el Ángulo (30 grados por defecto): las aristas duras de un modelo. Con Solo dentro de la selección conserva solo esas entre las aristas seleccionadas ahora."},
    {"Select the edges that aren't selected, and drop those that are.", "Selecciona las aristas no seleccionadas y quita las que lo están."},
    // --- AI: the first-use notice for online services (PanelAiConsent.cpp, ai/Consent.h) ---
    {"Send to %s?", "¿Enviar a %s?"},
    {"What", "Qué"},
    {"To", "A"},
    {"Pays", "Paga"},
    {"your brush description and the current brush settings", "la descripción del pincel y sus ajustes actuales"},
    {"this image (framed, up to 1024 px)", "esta imagen (encuadrada, hasta 1024 px)"},
    {"your description of the model", "tu descripción del modelo"},
    {"a check that the server answers, then your image or description", "una comprobación de que el servidor responde y después tu imagen o descripción"},
    {"Free: Pollinations.ai pays (rate limited), no account", "Gratis: lo paga Pollinations.ai (con límite de uso), sin cuenta"},
    {"Free: Hugging Face's shared GPUs (a daily allowance on your account or connection)", "Gratis: las GPU compartidas de Hugging Face (un cupo diario por cuenta o conexión)"},
    {"You: billed per generation to your fal.ai key", "Tú: se cobra cada generación a tu clave de fal.ai"},
    {"You: billed per generation to your Meshy key", "Tú: se cobra cada generación a tu clave de Meshy"},
    {"You: billed per generation to your Hyper3D key", "Tú: se cobra cada generación a tu clave de Hyper3D"},
    {"You: billed to your Anthropic (Claude) key", "Tú: se cobra a tu clave de Anthropic (Claude)"},
    {"You: billed to your OpenAI key", "Tú: se cobra a tu clave de OpenAI"},
    {"You: billed to your OpenRouter key", "Tú: se cobra a tu clave de OpenRouter"},
    {"You: billed to your API key", "Tú: se cobra a tu clave de API"},
    {"Whoever runs that server (your key, if you added one)", "Quien gestiona ese servidor (tu clave, si añadiste una)"},
    {"Whoever runs that ComfyUI server", "Quien gestiona ese servidor de ComfyUI"},
    {"Your sculpt stays on this computer. Asked once per service; Preferences > AI can revoke it.", "Tu escultura se queda en este equipo. Se pregunta una vez por servicio; Preferencias > IA puede revocarlo."},
    {"Allow", "Permitir"},
    {"Use local instead", "Usar local"},
    {"Set up local", "Configurar local"},
    {"Looking for a model on this computer...", "Buscando un modelo en este equipo..."},
    {"Runs on this computer: nothing leaves it", "Se ejecuta en este equipo: nada sale de él"},
    {"No local model found: how to run one (User Guide)", "No hay un modelo local: cómo ejecutar uno (Guía del usuario)"},
    {"Online AI services", "Servicios de IA en línea"},
    {"Before the first request to a service on someone else's computer, SculptForge says what is sent and asks. Models on this computer never ask.", "Antes de la primera petición a un servicio en el equipo de otra persona, SculptForge dice qué se envía y pregunta. Los modelos en este equipo nunca preguntan."},
    {"Blocked by your studio", "Bloqueado por tu estudio"},
    {"Allowed by your studio", "Permitido por tu estudio"},
    {"Allowed", "Permitido"},
    {"Asks first", "Pregunta antes"},
    {"Revoke", "Revocar"},
    // Voxel Remesh up to 4096 (PanelRetopo.cpp)
    {"Voxels across the model, 32 to 4096: higher keeps more detail and makes more triangles (about 4x for each doubling). The line under it estimates the result. Above 512 a remesh can make tens of millions of triangles and take minutes; it asks once first, and won't start if it can't fit in the free memory. Ctrl+click to type a value.",
     "Vóxeles a lo ancho del modelo, de 32 a 4096: más alto conserva más detalle y crea más triángulos (unas 4 veces más cada vez que se duplica). La línea de debajo estima el resultado. Por encima de 512 un remallado puede crear decenas de millones de triángulos y tardar minutos; pregunta una vez antes y no empieza si no cabe en la memoria libre. Ctrl+clic para escribir un valor."},
    {"Estimate",
     "Estimación"},
    {"About how many triangles Voxel Remesh makes at this Resolution, from the Part's surface area (fine detail smaller than a voxel counts, so a dense scan comes out lower). Above 512 also the memory the remesh needs; in red when that is more than is free now.",
     "Cuántos triángulos crea aproximadamente el Remallado vóxel con esta Resolución, a partir del área de la superficie de la Pieza (el detalle más fino que un vóxel cuenta, así que un escaneo denso sale por debajo). Por encima de 512 también la memoria que necesita; en rojo cuando es más de la que hay libre ahora."},
    {"About %s triangles",
     "Unos %s triángulos"},
    {"About %s triangles, ~%s of memory",
     "Unos %s triángulos, ~%s de memoria"},
    {"About %s triangles, needs ~%s, %s free",
     "Unos %s triángulos, necesita ~%s, %s libres"},
    {"High resolution remesh",
     "Remallado de alta resolución"},
    {"Voxel Remesh at resolution %d makes about %s triangles and needs about %s of memory (%s free).",
     "El Remallado vóxel a resolución %d crea unos %s triángulos y necesita unos %s de memoria (%s libres)."},
    {"It can take a few minutes; Cancel in Tool > Remesh stops it and the Part stays as it is. This won't ask again until the Resolution goes back to 512 or less.",
     "Puede tardar unos minutos; Cancelar en Herramienta > Remallado lo detiene y la Pieza queda como está. No volverá a preguntar hasta que la Resolución vuelva a 512 o menos."},
    {"Sculpting that many triangles needs a fast computer: Decimate or a lower Resolution makes it lighter.",
     "Esculpir tantos triángulos requiere un ordenador rápido: Diezmar o una Resolución más baja lo aligera."},
    // Panels in windows of their own (PanelWindows.cpp)
    {"Back in the main window", "De vuelta en la ventana principal"},
    {"Controls in its own window", "Controles en su propia ventana"},
    {"In a window of its own", "En una ventana propia"},
    {"Show it", "Mostrarla"},
    {"Move Controls to a window of its own, to keep on another monitor. It opens there next time too.", "Mueve Controles a una ventana propia, para tenerla en otro monitor. La próxima vez también se abre allí."},
    {"Move the guide to a window of its own, to keep on another monitor. It opens there next time too.", "Mueve la guía a una ventana propia, para tenerla en otro monitor. La próxima vez también se abre allí."},
    {"Move this palette to a window of its own, to keep on another monitor or a pen display. Close that window to bring it back. It opens there next time too.", "Mueve esta paleta a una ventana propia, para tenerla en otro monitor o en una pantalla de lápiz. Cierra esa ventana para devolverla. La próxima vez también se abre allí."},
    {"Open Controls (every shortcut and gesture) in a window of its own, to keep on another monitor.", "Abre Controles (todos los atajos y gestos) en una ventana propia, para tenerla en otro monitor."},
    {"Open in its own window", "Abrir en su propia ventana"},
    {"Open the User Guide in a window of its own, to keep on another monitor. It opens there next time too.", "Abre la Guía del usuario en una ventana propia, para tenerla en otro monitor. La próxima vez también se abre allí."},
    {"Put every panel that is in a window of its own back in the main window, and forget where those windows were.", "Devuelve a la ventana principal cada panel que está en una ventana propia y olvida dónde estaban esas ventanas."},
    {"Put this panel back in the main window. Preferences > Saving & Layouts > Reset panel windows puts all of them back.", "Devuelve este panel a la ventana principal. Preferencias > Guardado y diseños > Restablecer ventanas de paneles los devuelve todos."},
    {"Reset panel windows", "Restablecer ventanas de paneles"},
    {"User Guide in its own window", "Guía del usuario en su propia ventana"},
};

} // namespace

std::span<const Entry> spanishTable() { return {kSpanish, std::size(kSpanish)}; }

} // namespace sf::i18n
