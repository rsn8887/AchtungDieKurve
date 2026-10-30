// Achtung, die Kurve!  -  a small clone in C++ with SDL2
//
// Needs SDL2 and SDL2_ttf (text is drawn with SDL2_ttf).
// Build (Linux / macOS):
//   g++ -std=c++11 -O2 achtung.cpp -o achtung `sdl2-config --cflags --libs` -lSDL2_ttf
// Build (MSYS2 / MinGW on Windows):
//   g++ -std=c++11 -O2 achtung.cpp -o achtung.exe -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf
//   (or: g++ ... $(pkg-config --cflags --libs sdl2 SDL2_ttf))
// Font: put a TrueType font named font.ttf next to the program (a copy is delivered with
// the game). If there is none, the game falls back to a system font (Switch: the system
// font; desktop: DejaVu Sans / Arial). Nintendo Switch: see the Makefile (romfs/font.ttf).
//
// Controls - everyone shares one keyboard, and up to six game controllers work as well
// (D-pad left / right; controller 1 steers player 1, controller 2 player 2, ... in the
// order the controllers were connected):
//   Player 1 (red)    : Left / Right arrow     or controller 1 D-pad
//   Player 2 (green)  : A / D                  or controller 2 D-pad
//   Player 3 (blue)   : J / L                  or controller 3 D-pad
//   Player 4 (yellow) : Keypad 4 / Keypad 6    or controller 4 D-pad
//   Player 5 (magenta): Z / X                  or controller 5 D-pad
//   Player 6 (cyan)   : 9 / 0 (number row)     or controller 6 D-pad
//
// Title screen: every player's score is shown on the right as two grey digits. Press LEFT
// on your controls to colour your left digit and RIGHT to colour your right digit (press
// again to undo). A player with both digits coloured is in the next game. SPACE - or a
// fire button (A, B, X, Y, Start) on any controller - starts the game (2+ players needed).
// In the game, SPACE / a fire button also starts every round ("get ready" -> go).
//   F11 or Alt+Enter = toggle fullscreen (the window can also be resized / maximized)
//   ESC = back to the title screen (or quit from the title screen)
//
// Every player starts aimed at the centre of the field.
// Like in the Amiga original, the speed depends on the background picture: each
// round gets a new grayscale picture. Here DARK = FAST and LIGHT = SLOW
// (flip DARK_IS_SLOW below to reverse it).
// Rules: steer your curve, never touch a wall, a trail or another player's square.
// Every 2.5 seconds ALL curves leave a gap in their trails at the same moment, so other
// players can slip through - but every square stays visible and solid. Each time
// another player crashes, all players still alive get 1 point.
// First to (players - 1) * 10 points wins.

#include <SDL.h>
#include <SDL_ttf.h>

#ifdef __SWITCH__
#include <switch.h>   // romfs (bundled font) and the system font service
#endif

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------- settings
// The game's own resolution is 1280x720: a 960x720 (4:3) playfield on the left - full window
// height, so its top and bottom edges touch the edges of the screen - plus a 320 px wide
// score column on the right.
// Any window size / fullscreen is scaled to fit this (16:9 screens fill completely).
static const int FIELD_W = 960;
static const int FIELD_H = 720;
static const int SIDEBAR_W = 320;
static const int WIN_W = FIELD_W + SIDEBAR_W;   // 1280
static const int WIN_H = FIELD_H;               // 720
// The background picture and the drawn trails use BG_SCALE x BG_SCALE pixels per game
// pixel, so the grayscale picture is much finer than the game grid (2 = 1920x1440).
static const int BG_SCALE = 2;
static const int BG_W = FIELD_W * BG_SCALE;
static const int BG_H = FIELD_H * BG_SCALE;
// Brightness range of the background picture (0 = black, 255 = white). Keeping it dark
// makes the bright player trails stand out.
static const int BG_MIN_GRAY = 8;
static const int BG_MAX_GRAY = 110;

static const int MAX_PLAYERS = 6;
static const int BORDER_W = 16;         // thickness of the solid frame around the arena (deadly)
static const int ROW_Y0 = 80;           // y of player 1's row (score on the right, controls on the title screen)
static const int ROW_STEP = 100;        // distance between two player rows
static const int SCORE_SCALE = 8;       // size of the score numbers (cap height = 7 * 8 = 56 px)
static const int PANEL_W = 128;         // grey score panel: width ...
static const int PANEL_PAD = 14;        // ... and padding above / below the number
static const float RADIUS = 3.0f;       // half-size of the square head (side = 2*RADIUS+1)
static const float BASE_SPEED = 1.7f;   // pixels per tick on a medium-gray background
static const float SPEED_SLOW_FACTOR = 0.85f;  // speed multiplier on the slow end (-15%)
static const float SPEED_FAST_FACTOR = 1.15f;  // speed multiplier on the fast end (+15%)
static const bool DARK_IS_SLOW = false; // false = dark is fast, light is slow
static const float DETAIL = 0.80f;      // 0.5 = soft picture ... 0.9 = very grainy/detailed
static const int GRAY_LEVELS = 32;      // posterize the picture into this many gray steps
                                        // (crisp Amiga-style edges); 0 = smooth gradients
static const float TURN = 0.045f;       // radians per tick
static const int OWN_GRACE = 18;        // ticks during which own trail is harmless
static const int TICKS_PER_SEC = 60;
static const int GAP_INTERVAL_TICKS = 150;  // solid trail between two gaps (2.5 s at 60 ticks/s)
static const int GAP_LENGTH_TICKS = 16;     // length of a gap (~23-31 px, wider than the 7px square)

enum State { MENU, READY, PLAYING, ROUND_OVER, MATCH_OVER };

struct Player {
    float x = 0, y = 0, angle = 0;
    bool alive = false;
    bool gap = false;
    bool joinLeft = false, joinRight = false;   // title screen: left / right score digit activated
    bool inGame = false;                        // takes part in the current match
    int score = 0;
    int wins = 0;                               // rounds won as the last survivor (this match)
    SDL_Scancode left, right;
    SDL_Color color;
};

// ---------------------------------------------------------------- globals
static std::vector<uint32_t> g_stamp(FIELD_W * FIELD_H, 0);  // tick a pixel was painted
static std::vector<uint8_t> g_owner(FIELD_W * FIELD_H, 0);   // 0 = empty, else player+1
static uint32_t g_tick = 1;

static std::vector<uint8_t> g_bg(BG_W * BG_H, 128);          // gray value (0-255) of each picture pixel
static SDL_Texture* g_bgTex = nullptr;                         // the same picture as a texture

static Player g_players[MAX_PLAYERS];
static State g_state = MENU;
static int g_winner = -1;            // index of the match winner (valid in MATCH_OVER)
static bool g_resetAccum = false;    // tells the main loop to restart its tick timer

// Game controllers: slot k steers player k. Slots are filled in connection order.
struct Pad {
    SDL_GameController* ctrl = nullptr;
    SDL_JoystickID id = -1;
};
static Pad g_pads[MAX_PLAYERS];

// ---------------------------------------------------------------- text (SDL2_ttf)
// All text is drawn with SDL2_ttf. The font file is read into memory once, and one TTF_Font
// is opened per size step ("scale"): the cap height is 7 * scale pixels whatever font file is
// used, so the layout stays the same if you swap font.ttf for another font. Every string is
// rendered once into an anti-aliased white texture, cached, and tinted with the current draw
// colour whenever it is drawn.
static std::vector<unsigned char> g_fontBytes;   // the font file
static float g_capRatio = 0.72f;                 // cap height / point size of the loaded font
static bool g_fontReady = false;

struct SizedFont {
    TTF_Font* font = nullptr;
    int ascent = 0;    // pixels from the top of a rendered line to the baseline
    int capTop = 0;    // pixels from the baseline up to the top of capital letters
};
static std::map<int, SizedFont> g_fonts;         // by size step

struct TextTex {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};
static std::map<std::string, TextTex> g_textCache;

static bool readFileBytes(const std::string& path, std::vector<unsigned char>& out) {
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) return false;
    Sint64 size = SDL_RWsize(rw);
    bool ok = size > 0 && size < 64 * 1024 * 1024;
    if (ok) {
        out.resize((size_t)size);
        ok = SDL_RWread(rw, out.data(), 1, (size_t)size) == (size_t)size;
    }
    SDL_RWclose(rw);
    return ok;
}

// Can SDL_ttf open these bytes as a font? Also measures the font's cap height.
static bool acceptFontBytes(const std::vector<unsigned char>& bytes) {
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), (int)bytes.size());
    TTF_Font* f = rw ? TTF_OpenFontRW(rw, 1, 100) : nullptr;   // closes rw by itself
    if (!f) return false;
    int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
    if (TTF_GlyphMetrics(f, 'H', &minx, &maxx, &miny, &maxy, &adv) == 0 && maxy > 0)
        g_capRatio = maxy / 100.0f;
    TTF_CloseFont(f);
    return true;
}

#ifdef __SWITCH__
// The Switch's built-in system font (used when no font.ttf is bundled in the .nro).
static bool loadSwitchSystemFont(std::vector<unsigned char>& out) {
    if (R_FAILED(plInitialize(PlServiceType_User))) return false;
    PlFontData fd;
    bool ok = R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_Standard)) && fd.address && fd.size > 0;
    if (ok) out.assign((const unsigned char*)fd.address, (const unsigned char*)fd.address + fd.size);
    plExit();
    return ok;
}
#endif

// Starts SDL_ttf and loads the first usable font. Returns false (no text) if there is none.
static bool initText() {
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }
    std::vector<std::string> paths;
    paths.push_back("romfs:/font.ttf");            // Switch: bundled inside the .nro (romfs/font.ttf)
    if (char* base = SDL_GetBasePath()) {          // next to the program
        paths.push_back(std::string(base) + "font.ttf");
        SDL_free(base);
    }
    paths.push_back("font.ttf");                   // current directory
    paths.push_back("romfs/font.ttf");
    paths.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");   // system fonts
    paths.push_back("/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf");
    paths.push_back("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf");
    paths.push_back("/System/Library/Fonts/Supplemental/Arial Bold.ttf");
    paths.push_back("/Library/Fonts/Arial Bold.ttf");
    paths.push_back("C:/Windows/Fonts/arialbd.ttf");

    for (size_t i = 0; i < paths.size(); ++i) {
        std::vector<unsigned char> bytes;
        if (readFileBytes(paths[i], bytes) && acceptFontBytes(bytes)) {
            g_fontBytes.swap(bytes);
            g_fontReady = true;
            std::fprintf(stderr, "Font: %s\n", paths[i].c_str());
            return true;
        }
    }
#ifdef __SWITCH__
    {
        std::vector<unsigned char> bytes;
        if (loadSwitchSystemFont(bytes) && acceptFontBytes(bytes)) {
            g_fontBytes.swap(bytes);
            g_fontReady = true;
            std::fprintf(stderr, "Font: Switch system font\n");
            return true;
        }
    }
#endif
    std::fprintf(stderr, "No usable font found - put a TrueType font named font.ttf next to the game.\n");
    return false;
}

static SizedFont* fontFor(int scale) {
    if (!g_fontReady) return nullptr;
    std::map<int, SizedFont>::iterator it = g_fonts.find(scale);
    if (it != g_fonts.end()) return &it->second;

    SizedFont sf;
    int pt = std::max(6, (int)std::lround(7.0f * scale / g_capRatio));   // cap height = 7 * scale px
    SDL_RWops* rw = SDL_RWFromConstMem(g_fontBytes.data(), (int)g_fontBytes.size());
    sf.font = rw ? TTF_OpenFontRW(rw, 1, pt) : nullptr;
    if (sf.font) {
        TTF_SetFontHinting(sf.font, TTF_HINTING_LIGHT);
        sf.ascent = TTF_FontAscent(sf.font);
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        sf.capTop = (TTF_GlyphMetrics(sf.font, 'H', &minx, &maxx, &miny, &maxy, &adv) == 0 && maxy > 0)
                        ? maxy : 7 * scale;
    }
    return &g_fonts.insert(std::make_pair(scale, sf)).first->second;
}

static std::string upperText(const std::string& s) {
    std::string u = s;
    for (size_t i = 0; i < u.size(); ++i) u[i] = (char)std::toupper((unsigned char)u[i]);
    return u;
}

static void clearTextCache() {
    for (std::map<std::string, TextTex>::iterator it = g_textCache.begin(); it != g_textCache.end(); ++it)
        if (it->second.tex) SDL_DestroyTexture(it->second.tex);
    g_textCache.clear();
}

// The white, anti-aliased texture of a (capitalised) string at a size step; cached.
static TextTex* textTexture(SDL_Renderer* r, const std::string& text, int scale) {
    SizedFont* sf = fontFor(scale);
    if (!sf || !sf->font || text.empty()) return nullptr;
    std::string key = std::to_string(scale) + ":" + text;
    std::map<std::string, TextTex>::iterator it = g_textCache.find(key);
    if (it != g_textCache.end()) return it->second.tex ? &it->second : nullptr;

    if (g_textCache.size() > 400) clearTextCache();      // only a few dozen strings are ever used
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(sf->font, text.c_str(), white);
    TextTex tt;
    if (surf) {
        tt.w = surf->w;
        tt.h = surf->h;
        tt.tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
        if (tt.tex) SDL_SetTextureBlendMode(tt.tex, SDL_BLENDMODE_BLEND);
    }
    TextTex& slot = g_textCache[key];
    slot = tt;
    return slot.tex ? &slot : nullptr;
}

static void freeText() {
    clearTextCache();
    for (std::map<int, SizedFont>::iterator it = g_fonts.begin(); it != g_fonts.end(); ++it)
        if (it->second.font) TTF_CloseFont(it->second.font);
    g_fonts.clear();
    g_fontReady = false;
    if (TTF_WasInit()) TTF_Quit();
}

// Draws text with the top of its capital letters at y and its left edge at x, in the current
// draw colour. "scale" is the size step: capital letters are 7 * scale pixels tall.
static void drawText(SDL_Renderer* r, const std::string& text, int x, int y, int scale) {
    TextTex* t = textTexture(r, upperText(text), scale);
    SizedFont* sf = fontFor(scale);
    if (!t || !sf) return;
    Uint8 cr = 255, cg = 255, cb = 255, ca = 255;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_SetTextureColorMod(t->tex, cr, cg, cb);
    SDL_Rect dst = {x, y - (sf->ascent - sf->capTop), t->w, t->h};
    SDL_RenderCopy(r, t->tex, nullptr, &dst);
}

static int textWidth(const std::string& text, int scale) {
    SizedFont* sf = fontFor(scale);
    if (!sf || !sf->font || text.empty()) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(sf->font, upperText(text).c_str(), &w, &h);
    return w;
}

static int glyphAdvancePx(char c, int scale) {
    SizedFont* sf = fontFor(scale);
    if (!sf || !sf->font) return 0;
    int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
    TTF_GlyphMetrics(sf->font, (Uint16)std::toupper((unsigned char)c), &minx, &maxx, &miny, &maxy, &adv);
    return adv;
}

// Colour for a hue in degrees (0 red, 60 yellow, 120 green, 180 cyan, 240 blue).
static SDL_Color hueColor(float hueDeg) {
    const float sat = 0.80f, val = 1.0f;
    float h = hueDeg / 60.0f;
    int i = (int)h;
    float f = h - i;
    float p = val * (1.0f - sat), q = val * (1.0f - sat * f), t = val * (1.0f - sat * (1.0f - f));
    float rr = val, gg = t, bb = p;
    switch (i % 6) {
        case 0: rr = val; gg = t;   bb = p;   break;
        case 1: rr = q;   gg = val; bb = p;   break;
        case 2: rr = p;   gg = val; bb = t;   break;
        case 3: rr = p;   gg = q;   bb = val; break;
        case 4: rr = t;   gg = p;   bb = val; break;
        default: rr = val; gg = p;  bb = q;   break;
    }
    SDL_Color c = {(Uint8)(rr * 255.0f + 0.5f), (Uint8)(gg * 255.0f + 0.5f), (Uint8)(bb * 255.0f + 0.5f), 255};
    return c;
}

// Text with a vertical rainbow gradient, like the sky gradients of old Amiga games: red at
// the top of the letters through yellow, green and cyan to blue at the bottom. The text
// texture is drawn in thin horizontal strips, each tinted with its own colour.
static void drawTextRainbow(SDL_Renderer* r, const std::string& text, int x, int y, int scale) {
    TextTex* t = textTexture(r, upperText(text), scale);
    SizedFont* sf = fontFor(scale);
    if (!t || !sf) return;
    const int capRow = sf->ascent - sf->capTop;           // row of the capital tops inside the texture
    const float cap = (float)std::max(1, sf->capTop);
    for (int yy = 0; yy < t->h; yy += 2) {
        int bh = std::min(2, t->h - yy);
        float u = ((yy + bh * 0.5f) - capRow) / cap;      // 0 = top of the letters, 1 = baseline
        u = std::min(1.0f, std::max(0.0f, u));
        SDL_Color col = hueColor(u * 250.0f);
        SDL_SetTextureColorMod(t->tex, col.r, col.g, col.b);
        SDL_Rect src = {0, yy, t->w, bh};
        SDL_Rect dst = {x, y - capRow + yy, t->w, bh};
        SDL_RenderCopy(r, t->tex, &src, &dst);
    }
}

// ---------------------------------------------------------------- drawing helpers
static void fillSquare(SDL_Renderer* r, int cx, int cy, int half) {
    SDL_Rect sq = {cx - half, cy - half, 2 * half + 1, 2 * half + 1};
    SDL_RenderFillRect(r, &sq);
}

// same square, drawn into the high-resolution field texture
static void fillSquareHi(SDL_Renderer* r, int cx, int cy, int half) {
    SDL_Rect sq = {(cx - half) * BG_SCALE, (cy - half) * BG_SCALE,
                   (2 * half + 1) * BG_SCALE, (2 * half + 1) * BG_SCALE};
    SDL_RenderFillRect(r, &sq);
}

// mark the pixels of a square as occupied in the collision grid
static void paintGrid(int cx, int cy, int radius, int owner) {
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = cx + dx, py = cy + dy;
            if (px < 0 || py < 0 || px >= FIELD_W || py >= FIELD_H) continue;
            g_owner[py * FIELD_W + px] = (uint8_t)owner;
            g_stamp[py * FIELD_W + px] = g_tick;
        }
}

// ---------------------------------------------------------------- game logic
static float frand(float a, float b) { return a + (b - a) * (std::rand() / (float)RAND_MAX); }

// ---- grayscale background -------------------------------------------------
// A grid of random values that is smoothly interpolated ("value noise").
struct Lattice {
    int cell, w, h;
    std::vector<float> v;
    explicit Lattice(int c) : cell(c), w(BG_W / c + 2), h(BG_H / c + 2), v(w * h) {
        for (size_t i = 0; i < v.size(); ++i) v[i] = frand(0.0f, 1.0f);
    }
    static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
    float at(int x, int y) const {
        int gx = x / cell, gy = y / cell;
        float tx = smooth((x % cell) / (float)cell), ty = smooth((y % cell) / (float)cell);
        float a = v[gy * w + gx], b = v[gy * w + gx + 1];
        float c = v[(gy + 1) * w + gx], d = v[(gy + 1) * w + gx + 1];
        return (a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty;
    }
};

// Builds a new random gray picture (BG_W x BG_H pixels) and uploads it.
// Eight layers of noise, from coarse blobs down to features of only a few pixels,
// give a lot of detail. Histogram equalisation spreads the values evenly over
// BG_MIN_GRAY..BG_MAX_GRAY (strong contrast), and optional posterising makes crisp steps.
static void generateBackground() {
    const int LAYERS = 8;
    std::vector<float> val(BG_W * BG_H, 0.0f);
    float weight = 1.0f;
    for (int layer = 0; layer < LAYERS; ++layer) {
        int cell = std::max(3, (200 * BG_SCALE) >> layer);   // 400, 200, 100, ... 3 (at BG_SCALE 2)
        Lattice lat(cell);
        for (int y = 0; y < BG_H; ++y)
            for (int x = 0; x < BG_W; ++x)
                val[y * BG_W + x] += weight * lat.at(x, y);
        weight *= DETAIL;                                     // how strongly finer layers count
    }

    // histogram equalisation: every value becomes its rank (0..1) among all pixels
    float lo = val[0], hi = val[0];
    for (size_t i = 1; i < val.size(); ++i) {
        lo = std::min(lo, val[i]);
        hi = std::max(hi, val[i]);
    }
    const int BINS = 8192;
    std::vector<float> cdf(BINS, 0.0f);
    std::vector<int> binOf(val.size());
    for (size_t i = 0; i < val.size(); ++i) {
        binOf[i] = (int)((val[i] - lo) / (hi - lo) * (BINS - 1) + 0.5f);
        cdf[binOf[i]] += 1.0f;
    }
    float run = 0.0f;
    for (int b = 0; b < BINS; ++b) {
        run += cdf[b];
        cdf[b] = run / (float)val.size();
    }
    const float first = cdf[0];

    std::vector<uint32_t> pixels(val.size());
    for (size_t i = 0; i < val.size(); ++i) {
        float n = (cdf[binOf[i]] - first) / (1.0f - first);   // 0..1, evenly distributed
        if (GRAY_LEVELS > 1) n = std::floor(n * (GRAY_LEVELS - 1) + 0.5f) / (GRAY_LEVELS - 1);
        uint32_t gray = BG_MIN_GRAY + (uint32_t)(n * (BG_MAX_GRAY - BG_MIN_GRAY) + 0.5f);
        g_bg[i] = (uint8_t)gray;
        pixels[i] = (gray << 24) | (gray << 16) | (gray << 8) | 0xFFu;  // RGBA8888
    }
    SDL_UpdateTexture(g_bgTex, nullptr, pixels.data(), BG_W * (int)sizeof(uint32_t));
}

// Speed (pixels per tick) at a position, depending on the background brightness.
static float speedAt(float x, float y) {
    int ix = std::min(std::max((int)(x * BG_SCALE), 0), BG_W - 1);
    int iy = std::min(std::max((int)(y * BG_SCALE), 0), BG_H - 1);
    float t = (g_bg[iy * BG_W + ix] - BG_MIN_GRAY) / (float)(BG_MAX_GRAY - BG_MIN_GRAY);  // 0 = darkest, 1 = lightest
    if (!DARK_IS_SLOW) t = 1.0f - t;
    return BASE_SPEED * (SPEED_SLOW_FACTOR + (SPEED_FAST_FACTOR - SPEED_SLOW_FACTOR) * t);
}

// Paints the background picture into the playfield texture (this wipes all trails).
static void resetField(SDL_Renderer* r, SDL_Texture* field) {
    SDL_SetRenderTarget(r, field);
    SDL_RenderCopy(r, g_bgTex, nullptr, nullptr);
    SDL_SetRenderTarget(r, nullptr);
}

static void setupPlayers() {
    const SDL_Scancode lefts[MAX_PLAYERS] = {SDL_SCANCODE_LEFT, SDL_SCANCODE_A, SDL_SCANCODE_J,
                                             SDL_SCANCODE_KP_4, SDL_SCANCODE_Z, SDL_SCANCODE_9};
    const SDL_Scancode rights[MAX_PLAYERS] = {SDL_SCANCODE_RIGHT, SDL_SCANCODE_D, SDL_SCANCODE_L,
                                              SDL_SCANCODE_KP_6, SDL_SCANCODE_X, SDL_SCANCODE_0};
    const SDL_Color colors[MAX_PLAYERS] = {
        {255, 70, 70, 255},  {70, 230, 90, 255},  {80, 140, 255, 255},
        {255, 220, 60, 255}, {230, 90, 230, 255}, {60, 220, 220, 255}};
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        g_players[i].left = lefts[i];
        g_players[i].right = rights[i];
        g_players[i].color = colors[i];
        g_players[i].score = 0;
        g_players[i].wins = 0;
    }
}

// ---- who takes part ---------------------------------------------------------
static bool isJoined(const Player& p) { return p.joinLeft && p.joinRight; }

static int joinedCount() {
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) n += isJoined(g_players[i]) ? 1 : 0;
    return n;
}

static int playingCount() {
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) n += g_players[i].inGame ? 1 : 0;
    return n;
}

static int targetScore() {
    int n = playingCount();
    return (n > 1 ? n - 1 : 1) * 10;
}

static void resetScores() {
    for (int i = 0; i < MAX_PLAYERS; ++i) { g_players[i].score = 0; g_players[i].wins = 0; }
}

// Title screen: pressing LEFT / RIGHT of a player's controls (keyboard or D-pad)
// activates / deactivates that player's left / right score digit.
static void togglePlayerJoin(int player, bool right) {
    if (player < 0 || player >= MAX_PLAYERS) return;
    bool& flag = right ? g_players[player].joinRight : g_players[player].joinLeft;
    flag = !flag;
}

// ---- game controllers ---------------------------------------------------------
static int padSlotOf(SDL_JoystickID id) {
    for (int k = 0; k < MAX_PLAYERS; ++k)
        if (g_pads[k].ctrl && g_pads[k].id == id) return k;
    return -1;
}

static void openPad(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;
    SDL_GameController* c = SDL_GameControllerOpen(deviceIndex);
    if (!c) return;
    SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
    if (padSlotOf(id) >= 0) {            // already known (startup scan + "added" event)
        SDL_GameControllerClose(c);
        return;
    }
    for (int k = 0; k < MAX_PLAYERS; ++k)
        if (!g_pads[k].ctrl) {
            g_pads[k].ctrl = c;
            g_pads[k].id = id;
            return;
        }
    SDL_GameControllerClose(c);          // more than six controllers: ignore the rest
}

static void closePad(SDL_JoystickID id) {
    int k = padSlotOf(id);
    if (k < 0) return;
    SDL_GameControllerClose(g_pads[k].ctrl);
    g_pads[k].ctrl = nullptr;
    g_pads[k].id = -1;
}

static bool padHeld(int slot, SDL_GameControllerButton button) {
    return g_pads[slot].ctrl && SDL_GameControllerGetButton(g_pads[slot].ctrl, button) != 0;
}

static void newRound(SDL_Renderer* r, SDL_Texture* field) {
    std::fill(g_stamp.begin(), g_stamp.end(), 0);
    std::fill(g_owner.begin(), g_owner.end(), 0);
    g_tick = 1;

    generateBackground();       // new gray picture every round
    resetField(r, field);

    const float cx = FIELD_W / 2.0f, cy = FIELD_H / 2.0f;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player& p = g_players[i];
        p.alive = false;
        p.gap = false;
        if (!p.inGame) continue;
        // random start position, away from walls, the centre and the other players
        for (int attempt = 0; attempt < 200; ++attempt) {
            p.x = frand(80, FIELD_W - 80);
            p.y = frand(80, FIELD_H - 80);
            bool ok = std::hypot(p.x - cx, p.y - cy) > 150.0f;
            for (int j = 0; j < i && ok; ++j)
                if (g_players[j].inGame && std::hypot(p.x - g_players[j].x, p.y - g_players[j].y) < 110.0f)
                    ok = false;
            if (ok) break;
        }
        p.angle = std::atan2(cy - p.y, cx - p.x);   // aim at the centre of the screen
        p.alive = true;
    }
}

// Title screen -> first round. Needs at least two players with both digits activated.
static bool startMatch(SDL_Renderer* r, SDL_Texture* field) {
    if (joinedCount() < 2) return false;
    for (int i = 0; i < MAX_PLAYERS; ++i) g_players[i].inGame = isJoined(g_players[i]);
    resetScores();
    newRound(r, field);
    g_state = READY;
    return true;
}

static bool hitsSomething(int i) {
    const Player& p = g_players[i];
    float dx = std::cos(p.angle), dy = std::sin(p.angle);
    const float reach = RADIUS * 1.45f + 1.0f;  // squares stick out ~1.41x at their corners
    float fx = p.x + dx * reach;                // point just in front of the head
    float fy = p.y + dy * reach;
    for (int k = -3; k <= 3; ++k) {          // sample across the width of the head
        float off = RADIUS * k / 3.0f;
        int px = (int)std::lround(fx - dy * off);
        int py = (int)std::lround(fy + dx * off);
        // the thick frame around the arena is deadly
        if (px < BORDER_W || py < BORDER_W || px >= FIELD_W - BORDER_W || py >= FIELD_H - BORDER_W) return true;

        // The square of every other player is always solid, even while that player is
        // in a gap and leaves no trail behind.
        for (int j = 0; j < MAX_PLAYERS; ++j) {
            if (j == i || !g_players[j].alive) continue;
            if (std::abs(px - (int)std::lround(g_players[j].x)) <= (int)RADIUS &&
                std::abs(py - (int)std::lround(g_players[j].y)) <= (int)RADIUS)
                return true;
        }

        int idx = py * FIELD_W + px;
        int owner = g_owner[idx];
        if (owner == 0) continue;
        if (owner - 1 == i && g_stamp[idx] + OWN_GRACE > g_tick) continue;   // own fresh trail
        return true;
    }
    return false;
}

static int aliveCount() {
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) n += g_players[i].alive ? 1 : 0;
    return n;
}

static void updateTick(SDL_Renderer* r, SDL_Texture* field) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Gaps follow one global, fixed schedule shared by all players: GAP_INTERVAL_TICKS of
    // solid trail, then GAP_LENGTH_TICKS of gap, again and again (counted from round start).
    const bool gapNow = ((g_tick - 1) % (uint32_t)(GAP_INTERVAL_TICKS + GAP_LENGTH_TICKS)) >=
                        (uint32_t)GAP_INTERVAL_TICKS;

    // 1) steering + movement
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player& p = g_players[i];
        if (!p.alive) continue;
        float spd = speedAt(p.x, p.y);          // the gray value under the head sets the speed
        // keyboard or the D-pad of this player's controller
        if (keys[p.left] || padHeld(i, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) p.angle -= TURN;
        if (keys[p.right] || padHeld(i, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p.angle += TURN;
        p.x += std::cos(p.angle) * spd;
        p.y += std::sin(p.angle) * spd;

        p.gap = gapNow;                         // every player gaps at the same moments
    }

    // 2) collisions (all players checked before anyone paints -> fair head-on crashes)
    bool died[MAX_PLAYERS] = {false};
    int deaths = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g_players[i].alive && hitsSomething(i)) {
            died[i] = true;
            ++deaths;
        }
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (died[i]) {
            g_players[i].alive = false;
            // the square stays where the player crashed - and stays solid for the others
            paintGrid((int)std::lround(g_players[i].x), (int)std::lround(g_players[i].y), (int)RADIUS, i + 1);
        }

    // survivors score one point per crash this tick
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g_players[i].alive) g_players[i].score += deaths;

    // 3) paint trails of players still alive
    SDL_SetRenderTarget(r, field);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player& p = g_players[i];
        if (!p.alive || p.gap) continue;
        SDL_SetRenderDrawColor(r, p.color.r, p.color.g, p.color.b, 255);
        fillSquareHi(r, (int)std::lround(p.x), (int)std::lround(p.y), (int)RADIUS);
        paintGrid((int)std::lround(p.x), (int)std::lround(p.y), (int)RADIUS, i + 1);
    }
    SDL_SetRenderTarget(r, nullptr);

    ++g_tick;
}

// After every tick: is the round over (one or nobody left)? Does somebody win the match?
static void finishRoundIfOver() {
    if (aliveCount() > 1) return;
    for (int i = 0; i < MAX_PLAYERS; ++i)             // the last survivor wins the round
        if (g_players[i].alive && g_players[i].inGame) ++g_players[i].wins;
    int best = -1, bestScore = -1;
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g_players[i].inGame && g_players[i].score > bestScore) {
            bestScore = g_players[i].score;
            best = i;
        }
    if (bestScore >= targetScore()) {
        g_winner = best;
        g_state = MATCH_OVER;
    } else {
        g_state = ROUND_OVER;
    }
}

// SPACE / Return (title screen) / a controller's fire button: go to the next step.
static void advance(SDL_Renderer* r, SDL_Texture* field) {
    switch (g_state) {
        case MENU:       startMatch(r, field); break;
        case READY:      g_state = PLAYING; g_resetAccum = true; break;
        case ROUND_OVER: newRound(r, field); g_state = READY; break;
        case MATCH_OVER: g_state = MENU; resetScores(); break;
        case PLAYING:    break;
    }
}

// ---------------------------------------------------------------- rendering
static void drawMenu(SDL_Renderer* r) {
    // dim the background picture a bit so the text stays readable
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 10, 10, 20, 205);
    SDL_Rect all = {0, 0, FIELD_W, FIELD_H};
    SDL_RenderFillRect(r, &all);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    std::string title = "ACHTUNG, DIE KURVE!";
    int titleX = (FIELD_W - textWidth(title, 6)) / 2;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);                 // drop shadow
    drawText(r, title, titleX + 3, 25, 6);
    drawTextRainbow(r, title, titleX, 22, 6);                // rainbow-gradient title

    // The controls of every player, written to the left of that player's score
    // (the scores themselves are drawn in the sidebar by drawSidebar).
    static const char* KEYS[MAX_PLAYERS] = {"LEFT / RIGHT ARROW", "A / D", "J / L",
                                            "KEYPAD 4 / KEYPAD 6", "Z / X", "9 / 0"};
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g_players[i];
        int y = ROW_Y0 + i * ROW_STEP;
        std::string l1 = "P" + std::to_string(i + 1) + "  " + KEYS[i];
        std::string l2 = "OR CONTROLLER " + std::to_string(i + 1) + " D-PAD";
        SDL_SetRenderDrawColor(r, p.color.r, p.color.g, p.color.b, 255);   // never dimmed
        drawText(r, l1, FIELD_W - 32 - textWidth(l1, 3), y + 22, 3);
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        drawText(r, l2, FIELD_W - 32 - textWidth(l2, 2), y + 50, 2);
    }

    // short explanation on the left
    int ty = 100;
    auto put = [&](const std::string& text, int scale, Uint8 c) {
        SDL_SetRenderDrawColor(r, c, c, c, 255);
        drawText(r, text, 30, ty, scale);
        ty += scale * 10;
    };
    put("HOW TO JOIN", 3, 255);
    ty += 4;
    put("PRESS LEFT OR RIGHT", 2, 210);
    put("ON YOUR CONTROLS TO", 2, 210);
    put("COLOR YOUR NUMBERS.", 2, 210);
    put("BOTH COLORED = YOU", 2, 210);
    put("ARE IN THE GAME.", 2, 210);
    put("PRESS AGAIN TO UNDO.", 2, 210);
    ty += 20;
    put("SPACE OR ANY FIRE", 2, 255);
    put("BUTTON STARTS THE GAME", 2, 255);
    put("WITH AT LEAST 2 PLAYERS.", 2, 255);
    ty += 20;
    put("F11 = FULLSCREEN", 2, 180);
    put("ESC = QUIT", 2, 180);
    ty += 20;
    put("BACKGROUND SPEED:", 2, 180);
    put(DARK_IS_SLOW ? "DARK = SLOW, LIGHT = FAST" : "DARK = FAST, LIGHT = SLOW", 2, 180);
    for (int i = 0; i < 28; ++i) {                  // legend bar from dark to light
        Uint8 g = (Uint8)(BG_MIN_GRAY + i * (BG_MAX_GRAY - BG_MIN_GRAY) / 27);
        SDL_SetRenderDrawColor(r, g, g, g, 255);
        SDL_Rect seg = {30 + i * 10, ty + 4, 10, 12};
        SDL_RenderFillRect(r, &seg);
    }

    int n = joinedCount();
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    drawText(r, "PLAYERS IN: " + std::to_string(n), 30, FIELD_H - 110, 3);
    if (n < 2) {
        SDL_SetRenderDrawColor(r, 255, 170, 60, 255);
        drawText(r, "NEED AT LEAST 2 PLAYERS.", 30, FIELD_H - 76, 2);
    }
}

// The frame around the arena: BORDER_W one-pixel rings from the outer edge inwards, in
// several greys - a bright bevelled outer edge fading to a dark inner edge.
static void drawArenaBorder(SDL_Renderer* r) {
    static const Uint8 RING[BORDER_W] = {39, 63, 166, 180, 161, 146, 134, 122,
                                         111, 102, 92, 83, 73, 62, 50, 32};
    for (int d = 0; d < BORDER_W; ++d) {
        SDL_SetRenderDrawColor(r, RING[d], RING[d], RING[d], 255);
        SDL_Rect ring = {d, d, FIELD_W - 2 * d, FIELD_H - 2 * d};
        SDL_RenderDrawRect(r, &ring);
    }
}

// Thick vertical bar at the right edge: the share of rounds each player has won as the last
// survivor, one colour band per player, the biggest share on top (like the Amiga original).
static void drawWinBar(SDL_Renderer* r) {
    const int bx = WIN_W - 16 - 40, by = 16, bw = 40, bh = FIELD_H - 32;
    int total = 0;
    int order[MAX_PLAYERS], n = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g_players[i].inGame && g_players[i].wins > 0) { order[n++] = i; total += g_players[i].wins; }
    // largest on top; equal shares keep player order
    for (int a = 1; a < n; ++a)
        for (int b = a; b > 0 && g_players[order[b]].wins > g_players[order[b - 1]].wins; --b)
            std::swap(order[b], order[b - 1]);

    SDL_Rect frame = {bx - 4, by - 4, bw + 8, bh + 8};
    SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
    SDL_RenderFillRect(r, &frame);
    SDL_Rect inner = {bx - 2, by - 2, bw + 4, bh + 4};
    SDL_SetRenderDrawColor(r, 96, 96, 96, 255);
    SDL_RenderFillRect(r, &inner);
    SDL_Rect back = {bx, by, bw, bh};
    SDL_SetRenderDrawColor(r, 48, 48, 50, 255);
    SDL_RenderFillRect(r, &back);

    int acc = 0;
    for (int k = 0; k < n; ++k) {
        acc += g_players[order[k]].wins;
        int y0 = by + (k == 0 ? 0 : (int)((long long)bh * (acc - g_players[order[k]].wins) / total));
        int y1 = by + (int)((long long)bh * acc / total);
        const SDL_Color& c = g_players[order[k]].color;
        SDL_Rect seg = {bx, y0, bw, y1 - y0};
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(r, &seg);
    }
}

// Score column on the right. Every player has a grey panel with the score inside.
// Title screen: two grey digits per player that turn player-coloured when the player
// activates them. In the game: the real score, drawn in the player's colour.
static void drawSidebar(SDL_Renderer* r) {
    const bool menu = (g_state == MENU);
    const SDL_Color grey = {115, 115, 115, 255};
    const int panelH = 7 * SCORE_SCALE + 2 * PANEL_PAD;
    const int px = FIELD_W + (SIDEBAR_W - PANEL_W) / 2;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g_players[i];
        if (!menu && !p.inGame) continue;
        int y = ROW_Y0 + i * ROW_STEP;

        // grey panel: light outer edge, mid-grey inner edge, dark fill
        SDL_Rect outer = {px, y, PANEL_W, panelH};
        SDL_Rect mid = {px + 2, y + 2, PANEL_W - 4, panelH - 4};
        SDL_Rect fill = {px + 4, y + 4, PANEL_W - 8, panelH - 8};
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        SDL_RenderFillRect(r, &outer);
        SDL_SetRenderDrawColor(r, 96, 96, 96, 255);
        SDL_RenderFillRect(r, &mid);
        SDL_SetRenderDrawColor(r, 48, 48, 50, 255);
        SDL_RenderFillRect(r, &fill);

        int ty = y + PANEL_PAD;
        if (menu) {
            int tx = px + (PANEL_W - textWidth("00", SCORE_SCALE)) / 2;
            SDL_Color cl = p.joinLeft ? p.color : grey;
            SDL_Color cr = p.joinRight ? p.color : grey;
            SDL_SetRenderDrawColor(r, cl.r, cl.g, cl.b, 255);
            drawText(r, "0", tx, ty, SCORE_SCALE);
            SDL_SetRenderDrawColor(r, cr.r, cr.g, cr.b, 255);
            drawText(r, "0", tx + glyphAdvancePx('0', SCORE_SCALE), ty, SCORE_SCALE);
        } else {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d", std::min(p.score, 99));
            int tx = px + (PANEL_W - textWidth(buf, SCORE_SCALE)) / 2;
            SDL_SetRenderDrawColor(r, p.color.r, p.color.g, p.color.b, 255);
            drawText(r, buf, tx, ty, SCORE_SCALE);
        }
    }

    if (!menu) drawWinBar(r);

    // target score at the top of the column
    if (!menu) {
        std::string num = std::to_string(targetScore());
        SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
        drawText(r, "TARGET", px + (PANEL_W - textWidth("TARGET", 2)) / 2, 12, 2);
        drawText(r, num, px + (PANEL_W - textWidth(num, 5)) / 2, 32, 5);
    }
}

static void render(SDL_Renderer* r, SDL_Texture* field) {
    SDL_SetRenderDrawColor(r, 30, 30, 40, 255);
    SDL_RenderClear(r);

    SDL_Rect dst = {0, 0, FIELD_W, FIELD_H};
    SDL_RenderCopy(r, field, nullptr, &dst);

    if (g_state == MENU) {
        drawMenu(r);
    } else {
        // squares (same colour as their trails) of ALL players in the game: crashed players
        // keep their square where they died until the next round; in READY state also an arrow line
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            const Player& p = g_players[i];
            if (!p.inGame) continue;
            SDL_SetRenderDrawColor(r, p.color.r, p.color.g, p.color.b, 255);  // same colour as the trail
            fillSquare(r, (int)std::lround(p.x), (int)std::lround(p.y), (int)RADIUS);
            if (g_state == READY) {
                SDL_RenderDrawLine(r, (int)p.x, (int)p.y, (int)(p.x + std::cos(p.angle) * 30),
                                   (int)(p.y + std::sin(p.angle) * 30));
            }
        }
    }

    drawArenaBorder(r);
    drawSidebar(r);
    SDL_RenderPresent(r);
}

static void updateTitle(SDL_Window* w) {
    std::string t = "Achtung, die Kurve!  -  ";
    switch (g_state) {
        case MENU: t += "Press LEFT and RIGHT of your controls to join, SPACE / fire button to start, F11 = fullscreen"; break;
        case READY: t += "Get ready - SPACE / fire button to go (ESC = title screen)"; break;
        case PLAYING: t += "Go!"; break;
        case ROUND_OVER: t += "Round over - SPACE / fire button for next round"; break;
        case MATCH_OVER: t += "Player " + std::to_string(g_winner + 1) + " wins the match! SPACE / fire button = title screen"; break;
    }
    SDL_SetWindowTitle(w, t.c_str());
}

static void toggleFullscreen(SDL_Window* w) {
    bool isFull = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(w, isFull ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_ShowCursor(isFull ? SDL_ENABLE : SDL_DISABLE);
}

// ---------------------------------------------------------------- main
int main(int, char**) {
    std::srand((unsigned)std::time(nullptr));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
#ifdef __SWITCH__
    romfsInit();                  // lets the game read romfs:/font.ttf (bundled in the .nro)
#endif
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        std::fprintf(stderr, "Game controllers unavailable (keyboard still works): %s\n", SDL_GetError());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");  // smooth scaling when resized
    SDL_Window* window = SDL_CreateWindow("Achtung, die Kurve!", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!window || !renderer) {
        std::fprintf(stderr, "Window/renderer creation failed: %s\n", SDL_GetError());
        return 1;
    }
    // Draw everything in a fixed 1280x720 coordinate system; SDL scales it (with black
    // bars if needed) to whatever size the window / screen has.
    SDL_RenderSetLogicalSize(renderer, WIN_W, WIN_H);
    SDL_Texture* field = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, BG_W, BG_H);
    if (!field) {
        std::fprintf(stderr, "Could not create field texture: %s\n", SDL_GetError());
        return 1;
    }

    g_bgTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_STATIC, BG_W, BG_H);
    if (!g_bgTex) {
        std::fprintf(stderr, "Could not create background texture: %s\n", SDL_GetError());
        return 1;
    }

    initText();                   // SDL_ttf + font (the game still runs without, but with no text)

    setupPlayers();
    generateBackground();         // a picture for the title screen; every round gets a fresh one
    resetField(renderer, field);

    for (int i = 0; i < SDL_NumJoysticks(); ++i) openPad(i);   // controllers already plugged in

    bool running = true;
    updateTitle(window);

    Uint32 lastTime = SDL_GetTicks();
    double accumulator = 0.0;
    const double tickMs = 1000.0 / TICKS_PER_SEC;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                SDL_Scancode sc = e.key.keysym.scancode;

                if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    toggleFullscreen(window);
                } else if (k == SDLK_ESCAPE) {
                    if (g_state == MENU) running = false;
                    else { g_state = MENU; resetScores(); }
                } else if (k == SDLK_SPACE || (k == SDLK_RETURN && g_state == MENU)) {
                    advance(renderer, field);
                } else if (g_state == MENU) {
                    // title screen: a player's own LEFT / RIGHT key (de)activates a score digit
                    for (int i = 0; i < MAX_PLAYERS; ++i) {
                        if (sc == g_players[i].left) togglePlayerJoin(i, false);
                        else if (sc == g_players[i].right) togglePlayerJoin(i, true);
                    }
                }
                updateTitle(window);
            } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                openPad(e.cdevice.which);
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                closePad(e.cdevice.which);
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                int slot = padSlotOf(e.cbutton.which);
                Uint8 b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                    if (g_state == MENU && slot >= 0) togglePlayerJoin(slot, b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                } else if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_B ||
                           b == SDL_CONTROLLER_BUTTON_X || b == SDL_CONTROLLER_BUTTON_Y ||
                           b == SDL_CONTROLLER_BUTTON_START) {
                    advance(renderer, field);             // any fire button on any controller
                }
                updateTitle(window);
            }
        }
        if (g_resetAccum) { accumulator = 0.0; g_resetAccum = false; }

        Uint32 now = SDL_GetTicks();
        accumulator += now - lastTime;
        lastTime = now;
        if (accumulator > 250.0) accumulator = 250.0;  // avoid spiral after a stall

        while (accumulator >= tickMs) {
            accumulator -= tickMs;
            if (g_state == PLAYING) {
                updateTick(renderer, field);
                finishRoundIfOver();
                if (g_state != PLAYING) updateTitle(window);
            }
        }

        render(renderer, field);
        SDL_Delay(1);
    }

    for (int k = 0; k < MAX_PLAYERS; ++k)
        if (g_pads[k].ctrl) SDL_GameControllerClose(g_pads[k].ctrl);
    freeText();
    SDL_DestroyTexture(g_bgTex);
    SDL_DestroyTexture(field);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
#ifdef __SWITCH__
    romfsExit();
#endif
    SDL_Quit();
    return 0;
}
