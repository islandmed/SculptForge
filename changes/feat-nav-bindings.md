---
whats_new: **Navigation bindings**: **Edit > Preferences > Navigation** gives orbit, pan and zoom a hold key each (hold it
  and drag with any button or the pen), a mouse-button-plus-modifier gesture, and the pen's side buttons, with warnings
  for a binding that is already taken and a Navigation defaults button. Nothing changes until you set it.
highlight: Hold any key and drag to orbit, pan or zoom
added: **Navigation bindings** (Edit > Preferences > Navigation): one place for the camera's bindings, for left-handed and
  tablet users who navigate from the hand on the keyboard. **Hold to orbit**, **Hold to pan** and **Hold to zoom** are
  actions with no key by default: give one a key (there, in Hotkeys, or Ctrl+Alt+click) and, while it is held, a drag
  anywhere on the canvas with any mouse button or the pen's tip is that camera move and never a stroke; the drag goes on
  to the button's release. Each move can also take a drag gesture of its own (Left, Middle or Right with Ctrl, Shift or
  Alt), checked before the key style's gestures; the left button needs a modifier. The panel lists the view keys (Frame,
  Reset view, the orbit, pan and zoom step keys) and the pen's side buttons, where **Zoom view** joins Rotate view and Pan
  view (Preferences > Tablet has it too). A hold key another action also has, two moves with the same gesture, or a
  gesture the key style already uses show a warning naming the other meaning; **Navigation defaults** puts all of it
  back. While a hold key is down a label (HOLD TO ORBIT, PAN or ZOOM) shows in the status bar where Camera locked does,
  and the warning lines and gesture names are translated. Everything saves itself; every key style's defaults are unchanged. Requested by Fenix and other members on
  Discord.
testers:
  - Any OS: Edit > Preferences > Navigation; click the key button beside Pan, press J. Hold J and drag on the model with
    the left button: the view pans, nothing is sculpted, and HOLD TO PAN shows in the status bar until you let go of J; let go of J mid-drag: the pan continues to the release. Do the
    same with the right and middle buttons and with a pen's tip.
  - Windows or Linux with a tablet: set the pen's third side button (or the upper one) to Zoom view; hold it and move
    the pen: the view zooms.
  - Any OS: under Zoom pick Middle and turn on Shift: a warning says it takes over the middle-drag pan; Shift+middle-drag
    now zooms, a plain middle-drag still pans. Set the same for Pan: both rows warn. Navigation defaults clears it all
    and leaves other shortcuts alone. Restart: the hold keys and gestures are kept.
  - Any OS: press F3, type j into the command palette with Hold to pan on J: it types, the view does not move.
member_request: true
member_credit: Fenix
---
Reviewer notes: NavBindings.h holds the pure logic (tests/test_nav_bindings.cpp); App.cpp onMouseDown checks a held
hold key or a custom gesture first, before Hold Space and the key style's gestures; Config.cpp drawNavigationSection
reuses the Hotkeys shortcut cell (drawShortcutCell, extracted) and the Tablet pen-button rows (drawPenButtonRow,
extracted). tests/ui/nav_bindings.sfui runs at 1440x900 and 2880x1800 as well. The second requester did not consent
to a public credit and is not named.
