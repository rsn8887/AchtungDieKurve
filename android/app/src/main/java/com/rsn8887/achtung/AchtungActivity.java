package com.rsn8887.achtung;

import org.libsdl.app.SDLActivity;

// SDL's activity runs the game: it loads libSDL2.so and libmain.so (the game) and calls its main().
public class AchtungActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
