// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
package org.sculptforge.app;

import org.libsdl.app.SDLActivity;

// SDL's activity runs the app (libmain.so's SDL_main, the same main() as on the desktop).
public class SculptForgeActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }
}
