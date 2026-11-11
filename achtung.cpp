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
//   Title screen: UP / DOWN arrow (or controller D-pad up / down) removes / adds a CPU
//   opponent in the first free player slot; a player takes a CPU slot over by pressing
//   their own LEFT / RIGHT.
//
// Sound: a fire-explosion sound (embedded in this file) plays whenever a player crashes,
// and a "Ta-da-da-da-da-DAAA!" fanfare (synthesized at start-up) when somebody wins the match - while a
// golden coin with an embossed "1" flies in next to the winner's score.
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
static const int BORDER_W = 14;         // thickness of the solid frame around the arena (deadly)
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
    bool cpu = false;                           // controlled by the computer (set on the title screen)
    float skill = 0.8f, aggr = 0.5f;            // CPU: how well it sees / reacts, how much it hunts opponents
    int cmd = 0, thinkIn = 0, wobble = 0, wobbleDir = 0;   // CPU: current steering, think timer, random swerve
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

static void audioPlayExplosion();   // sound section further down
static void audioPlayTada();
static Uint32 g_winTime = 0;         // when the match was won (for the coin animation)

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
static bool isParticipant(const Player& p) { return isJoined(p) || p.cpu; }   // human or CPU

static int joinedCount() {                      // everybody who would play: humans and CPUs
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) n += isParticipant(g_players[i]) ? 1 : 0;
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
    Player& p = g_players[player];
    if (p.cpu) {                                // a human takes over this slot: no more CPU
        p.cpu = false;
        p.joinLeft = p.joinRight = false;
    }
    bool& flag = right ? p.joinRight : p.joinLeft;
    flag = !flag;
}

// Title screen, DOWN: the first free slot becomes a CPU opponent (slots a human has touched
// are skipped). UP: the highest CPU slot is freed again.
static void addCpu() {
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player& p = g_players[i];
        if (!p.cpu && !p.joinLeft && !p.joinRight) { p.cpu = true; return; }
    }
}

static void removeCpu() {
    for (int i = MAX_PLAYERS - 1; i >= 0; --i)
        if (g_players[i].cpu) { g_players[i].cpu = false; return; }
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
        p.cmd = 0; p.thinkIn = 0; p.wobble = 0;
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
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        Player& p = g_players[i];
        p.inGame = isParticipant(p);
        if (p.cpu) {                            // every CPU gets its own character for this match
            p.skill = frand(0.55f, 1.0f);
            p.aggr = frand(0.0f, 1.0f);
        }
    }
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

// ---------------------------------------------------------------- CPU opponents
// A CPU player "looks ahead": every few ticks it plays out a handful of possible manoeuvres
// (go straight, or turn left / right for a while and then go straight) on the real playfield,
// using the same turn rate and speed as everybody else, and checks how far each one gets before
// hitting a wall, a trail or another player. It steers along the manoeuvre that survives
// longest, and among equally safe ones prefers open space - or, for aggressive CPUs, heading
// for the opponents to cut them off. Every CPU gets its own skill (how far it sees and how
// quickly it reacts) and its own aggression, and sometimes wobbles for no reason, so the CPUs
// are different from each other, beatable, and not boring.

// Would a head at (x, y) pointing along `angle` crash `ahead` ticks from now? `opp` holds, for
// every other player, the stretch (x0, y0, x1, y1) it is expected to have covered by then:
// its head sits at the end, and everything along the stretch is its fresh trail.
static bool cpuBlocked(int self, float x, float y, float angle, int ahead,
                       const float (*opp)[4], int oppCount) {
    const float dx = std::cos(angle), dy = std::sin(angle);
    const float reach = RADIUS * 1.45f + 1.0f;
    const float fx = x + dx * reach, fy = y + dy * reach;
    for (int k = -3; k <= 3; k += 3) {
        const float off = RADIUS * k / 3.0f;
        const int px = (int)std::lround(fx - dy * off), py = (int)std::lround(fy + dx * off);
        if (px < BORDER_W || py < BORDER_W || px >= FIELD_W - BORDER_W || py >= FIELD_H - BORDER_W) return true;
        for (int j = 0; j < oppCount; ++j) {
            const float sx = opp[j][2] - opp[j][0], sy = opp[j][3] - opp[j][1];
            const float len2 = sx * sx + sy * sy;
            float t = len2 > 1e-6f ? ((px - opp[j][0]) * sx + (py - opp[j][1]) * sy) / len2 : 0.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            const float qx = opp[j][0] + sx * t - px, qy = opp[j][1] + sy * t - py;
            if (qx * qx + qy * qy <= (RADIUS + 2.5f) * (RADIUS + 2.5f)) return true;
        }
        const int idx = py * FIELD_W + px;
        const int owner = g_owner[idx];
        if (owner == 0) continue;
        if (owner - 1 == self && g_stamp[idx] + OWN_GRACE > g_tick + (uint32_t)ahead) continue;   // own fresh trail
        return true;
    }
    return false;
}

struct CpuView {                       // what a CPU knows about the other players
    float pos[MAX_PLAYERS][2], vel[MAX_PLAYERS][2];
    int n = 0;
};

// Plays out one manoeuvre: turn `dir` (-1 / +1) for `turnTicks` ticks, then straight on.
// Returns the number of ticks survived (up to `horizon`) and where the head ends up.
static int cpuRollout(int self, const CpuView& v, float x, float y, float a, float spd,
                      int dir, int turnTicks, int horizon, float* ex, float* ey, float* ea) {
    float opp[MAX_PLAYERS][4];
    int s = 1;
    for (; s <= horizon; ++s) {
        if (s <= turnTicks) a += dir * TURN;
        x += std::cos(a) * spd;
        y += std::sin(a) * spd;
        int cnt = 0;
        if (s <= 45)                                   // the others are expected to keep going straight
            for (int j = 0; j < v.n; ++j) {
                opp[cnt][0] = v.pos[j][0]; opp[cnt][1] = v.pos[j][1];
                opp[cnt][2] = v.pos[j][0] + v.vel[j][0] * s; opp[cnt][3] = v.pos[j][1] + v.vel[j][1] * s;
                ++cnt;
            }
        if (cpuBlocked(self, x, y, a, s, opp, cnt)) break;
    }
    *ex = x; *ey = y; *ea = a;
    return s - 1;
}

// How much free room lies around a position (0 = boxed in ... 1 = wide open).
static float cpuOpenness(int self, float x, float y, float a, int horizon) {
    static const float rel[5] = {-1.2f, -0.6f, 0.0f, 0.6f, 1.2f};
    int free = 0, total = 0;
    for (int k = 0; k < 5; ++k) {
        for (int s = 1; s <= 14; ++s) {
            float px = x + std::cos(a + rel[k]) * s * 4.0f, py = y + std::sin(a + rel[k]) * s * 4.0f;
            ++total;
            if (cpuBlocked(self, px, py, a + rel[k], horizon, nullptr, 0)) break;
            ++free;
        }
    }
    return free / (float)total;
}

// Steering command for CPU player i this tick: -1 = left, 0 = straight, +1 = right.
static int cpuSteer(int i) {
    Player& p = g_players[i];
    const float spd = speedAt(p.x, p.y);

    CpuView v;
    for (int j = 0; j < MAX_PLAYERS; ++j) {
        if (j == i || !g_players[j].alive) continue;
        const float sj = speedAt(g_players[j].x, g_players[j].y);
        v.pos[v.n][0] = g_players[j].x; v.pos[v.n][1] = g_players[j].y;
        v.vel[v.n][0] = std::cos(g_players[j].angle) * sj; v.vel[v.n][1] = std::sin(g_players[j].angle) * sj;
        ++v.n;
    }

    // Reaction time: think only every few ticks - unless the way ahead is about to be blocked.
    float ex, ey, ea;
    const int dir0 = p.wobble > 0 ? p.wobbleDir : p.cmd;
    const bool urgent = cpuRollout(i, v, p.x, p.y, p.angle, spd, dir0, 18, 18, &ex, &ey, &ea) < 18;
    if (--p.thinkIn > 0 && !urgent) return dir0;
    p.thinkIn = 2 + (int)std::lround((1.0f - p.skill) * 4.0f);       // skilled CPUs think nearly every tick
    if (urgent) p.wobble = 0;

    const int horizon = 45 + (int)(55 * p.skill);
    static const int DURATIONS[] = {3, 8, 14, 22, 32, 45, 60};

    // the opponent closest to us (aggressive CPUs like to head for it)
    int target = -1;
    float bestD = 1e9f;
    for (int k = 0; k < v.n; ++k) {
        float d = std::hypot(v.pos[k][0] - p.x, v.pos[k][1] - p.y);
        if (d < bestD) { bestD = d; target = k; }
    }

    float bestScore = -1e9f;
    int bestDir = 0, bestSteps = 0;
    for (int c = -1; c < 2 * 7; ++c) {            // c == -1: straight on; else direction + duration
        const int dir = (c < 0) ? 0 : (c < 7 ? -1 : +1);
        const int dur = (c < 0) ? 0 : DURATIONS[c % 7];
        int steps = cpuRollout(i, v, p.x, p.y, p.angle, spd, dir, dur, horizon, &ex, &ey, &ea);
        float score = (float)steps;
        if (steps >= horizon) {                    // safe all the way: prefer open space (and, if bold, the opponent)
            score += 40.0f * cpuOpenness(i, ex, ey, ea, horizon);
            if (target >= 0) {
                float d = std::hypot(v.pos[target][0] - ex, v.pos[target][1] - ey);
                score += p.aggr * 22.0f * std::max(0.0f, std::min(1.0f, (260.0f - d) / 260.0f));
            }
        }
        score += frand(-1.0f, 1.0f) * (0.5f + 3.0f * (1.0f - p.skill));   // a little human imperfection
        if (dir == p.cmd) score += 2.5f;           // don't dither between equal choices
        if (score > bestScore) { bestScore = score; bestDir = dir; bestSteps = steps; }
    }
    p.cmd = bestDir;

    // now and then, when everything is fine, swerve a bit for no good reason
    if (p.wobble <= 0 && bestSteps >= horizon && frand(0.0f, 1.0f) < 0.012f + 0.03f * (1.0f - p.skill)) {
        p.wobble = 6 + std::rand() % 14;
        p.wobbleDir = (std::rand() & 1) ? 1 : -1;
    }
    if (p.wobble > 0) { --p.wobble; return p.wobbleDir; }
    return p.cmd;
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
        if (p.cpu) {
            p.angle += cpuSteer(i) * TURN;
        } else {
            if (keys[p.left] || padHeld(i, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) p.angle -= TURN;
            if (keys[p.right] || padHeld(i, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p.angle += TURN;
        }
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
    if (deaths > 0) audioPlayExplosion();
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
        g_winTime = SDL_GetTicks();
        audioPlayTada();
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
    ty += 14;
    put("UP / DOWN (OR D-PAD):", 2, 255);
    put("ADD / REMOVE CPU", 2, 255);
    put("OPPONENTS. PRESS YOUR", 2, 210);
    put("OWN KEYS TO TAKE OVER.", 2, 210);
    ty += 14;
    put("SPACE OR ANY FIRE", 2, 255);
    put("BUTTON STARTS THE GAME", 2, 255);
    put("WITH AT LEAST 2 PLAYERS.", 2, 255);
    ty += 14;
    put("F11 = FULLSCREEN", 2, 180);
    put("ESC = QUIT", 2, 180);
    ty += 14;
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
// several greys - a dark outer edge rising to a bright inner edge (a raised pedestal).
static void drawArenaBorder(SDL_Renderer* r) {
    static const Uint8 RING[BORDER_W] = {32, 50, 62, 73, 83, 92, 102, 111,
                                         122, 134, 146, 161, 180, 166};
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
    SDL_SetRenderDrawColor(r, 96, 96, 96, 255);
    SDL_RenderFillRect(r, &frame);
    SDL_Rect inner = {bx - 2, by - 2, bw + 4, bh + 4};
    SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
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
    // centred between the arena and the left edge of the win bar's frame (WIN_W - 60)
    const int px = (FIELD_W + (WIN_W - 60) - PANEL_W) / 2;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g_players[i];
        if (!menu && !p.inGame) continue;
        int y = ROW_Y0 + i * ROW_STEP;

        // grey panel: light outer edge, mid-grey inner edge, dark fill
        SDL_Rect outer = {px, y, PANEL_W, panelH};
        SDL_Rect mid = {px + 2, y + 2, PANEL_W - 4, panelH - 4};
        SDL_Rect fill = {px + 4, y + 4, PANEL_W - 8, panelH - 8};
        SDL_SetRenderDrawColor(r, 96, 96, 96, 255);
        SDL_RenderFillRect(r, &outer);
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        SDL_RenderFillRect(r, &mid);
        SDL_SetRenderDrawColor(r, 48, 48, 50, 255);
        SDL_RenderFillRect(r, &fill);

        if (p.cpu) {                            // "CPU" next to the numbers of a computer player
            SDL_SetRenderDrawColor(r, p.color.r, p.color.g, p.color.b, 255);
            drawText(r, "CPU", px + PANEL_W + 8, y + (panelH - 14) / 2, 2);
        }

        int ty = y + PANEL_PAD;
        if (menu) {
            int tx = px + (PANEL_W - textWidth("00", SCORE_SCALE)) / 2;
            SDL_Color cl = (p.joinLeft || p.cpu) ? p.color : grey;
            SDL_Color cr = (p.joinRight || p.cpu) ? p.color : grey;
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

// ---------------------------------------------------------------- winner's coin
// When somebody wins the match, a round golden coin with an embossed "1" flies in from the
// bottom of the screen and stops left of the winner's score panel. The coin only moves - it
// never rotates or changes size - so the "1" always stays readable. It is drawn once into a
// texture (larger than it is shown, so it comes out smooth) from gradient-filled shapes.
static SDL_Texture* g_coinTex = nullptr;
static const int COIN_TEX = 256;           // texture size
static const float COIN_SIZE = 96;         // size on screen

static SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t) {
    auto m = [t](int x, int y) { return (Uint8)std::max(0, std::min(255, (int)(x + (y - x) * t))); };
    SDL_Color o = {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
    return o;
}

// filled ellipse, centre colour -> edge colour
static void geoEllipse(SDL_Renderer* r, float cx, float cy, float rx, float ry, SDL_Color cc, SDL_Color ce) {
    const int N = 64;
    SDL_Vertex v[N + 2];
    int idx[N * 3];
    v[0].position.x = cx; v[0].position.y = cy; v[0].color = cc; v[0].tex_coord.x = v[0].tex_coord.y = 0;
    for (int i = 0; i <= N; ++i) {
        float a = 6.2831853f * i / N;
        v[i + 1].position.x = cx + std::cos(a) * rx;
        v[i + 1].position.y = cy + std::sin(a) * ry;
        v[i + 1].color = ce;
        v[i + 1].tex_coord.x = v[i + 1].tex_coord.y = 0;
    }
    for (int i = 0; i < N; ++i) { idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = i + 2; }
    SDL_RenderGeometry(r, nullptr, v, N + 2, idx, N * 3);
}

// A ring between two radii, lit from the upper left. bevel = +1: a slope facing outwards
// (bright where it faces the light); -1: a slope facing inwards (bright on the far side).
static void geoRing(SDL_Renderer* r, float cx, float cy, float rin, float rout,
                    SDL_Color lightC, SDL_Color darkC, float bevel) {
    const int N = 96;
    SDL_Vertex v[(N + 1) * 2];
    int idx[N * 6];
    for (int i = 0; i <= N; ++i) {
        float a = 6.2831853f * i / N, nx = std::cos(a), ny = std::sin(a);
        float lit = 0.5f + 0.5f * bevel * (-0.7071f * nx - 0.7071f * ny);
        SDL_Color c = lerpColor(darkC, lightC, lit);
        v[i * 2].position.x = cx + nx * rout; v[i * 2].position.y = cy + ny * rout;
        v[i * 2 + 1].position.x = cx + nx * rin; v[i * 2 + 1].position.y = cy + ny * rin;
        v[i * 2].color = v[i * 2 + 1].color = c;
        v[i * 2].tex_coord.x = v[i * 2].tex_coord.y = v[i * 2 + 1].tex_coord.x = v[i * 2 + 1].tex_coord.y = 0;
    }
    for (int i = 0; i < N; ++i) {
        int a = i * 2, b = i * 2 + 1, c = i * 2 + 2, d = i * 2 + 3;
        int* p = idx + i * 6;
        p[0] = a; p[1] = b; p[2] = c; p[3] = b; p[4] = d; p[5] = c;
    }
    SDL_RenderGeometry(r, nullptr, v, (N + 1) * 2, idx, N * 6);
}

static void createCoinTexture(SDL_Renderer* r) {
    g_coinTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, COIN_TEX, COIN_TEX);
    if (!g_coinTex) return;
    SDL_SetTextureBlendMode(g_coinTex, SDL_BLENDMODE_BLEND);
    SDL_Texture* prevTarget = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, g_coinTex);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);

    const float R = COIN_TEX / 2.0f, u = R / 100.0f, cx = R, cy = R;     // u: one "percent" of the radius
    const SDL_Color hiGold = {255, 228, 125, 255}, midGold = {196, 146, 30, 255},
                    darkGold = {112, 70, 8, 255};

    geoEllipse(r, cx, cy, 98 * u, 98 * u, darkGold, darkGold);                           // outer edge
    geoRing(r, cx, cy, 90 * u, 98 * u, hiGold, darkGold, +1);                            // raised rim: outer slope
    geoRing(r, cx, cy, 82 * u, 90.3f * u, SDL_Color{236, 188, 62, 255}, midGold, +1);    // rim top
    // fine ridges around the rim
    {
        const int TICKS = 90;
        for (int i = 0; i < TICKS; ++i) {
            float a = 6.2831853f * i / TICKS, da = 6.2831853f / TICKS * 0.28f;
            float r0 = 83.5f * u, r1 = 88.5f * u;
            SDL_FPoint q[4] = {{cx + std::cos(a - da) * r0, cy + std::sin(a - da) * r0},
                               {cx + std::cos(a + da) * r0, cy + std::sin(a + da) * r0},
                               {cx + std::cos(a + da) * r1, cy + std::sin(a + da) * r1},
                               {cx + std::cos(a - da) * r1, cy + std::sin(a - da) * r1}};
            float lit = 0.5f + 0.5f * (-0.7071f * std::cos(a) - 0.7071f * std::sin(a));
            SDL_Color c = lerpColor(SDL_Color{120, 78, 10, 255}, SDL_Color{176, 128, 28, 255}, lit);
            SDL_Vertex v[4];
            for (int k = 0; k < 4; ++k) { v[k].position = q[k]; v[k].color = c; v[k].tex_coord.x = v[k].tex_coord.y = 0; }
            static const int qi[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(r, nullptr, v, 4, qi, 6);
        }
    }
    geoRing(r, cx, cy, 75 * u, 82.3f * u, SDL_Color{250, 214, 100, 255}, darkGold, -1);  // inner slope down to the face
    geoEllipse(r, cx, cy, 75.3f * u, 75.3f * u, SDL_Color{238, 192, 64, 255}, SDL_Color{208, 158, 38, 255});   // the face

    // soft highlight on the upper left of the face
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    geoEllipse(r, cx - 24 * u, cy - 28 * u, 40 * u, 28 * u, SDL_Color{255, 244, 190, 80}, SDL_Color{255, 244, 190, 0});

    // the embossed "1": a raised digit - bright edge towards the light, dark edge away from it
    const int sc = std::max(2, (int)std::lround(0.82f * R / 7.0f));
    const int tw = textWidth("1", sc);
    const int tx = (int)(cx - tw / 2.0f), ty = (int)(cy - 7 * sc / 2.0f);
    const int d = std::max(2, (int)(2.2f * u));
    SDL_SetRenderDrawColor(r, 78, 46, 4, 255);                                           // shadow side
    drawText(r, "1", tx + d * 2, ty + d * 2, sc);
    drawText(r, "1", tx + d, ty + d, sc);
    SDL_SetRenderDrawColor(r, 255, 244, 176, 255);                                       // lit side
    drawText(r, "1", tx - d, ty - d, sc);
    SDL_SetRenderDrawColor(r, 240, 194, 66, 255);                                        // the raised top
    drawText(r, "1", tx, ty, sc);

    SDL_SetRenderTarget(r, prevTarget);
}

static void drawCoin(SDL_Renderer* r) {
    if (g_winner < 0) return;
    if (!g_coinTex) createCoinTexture(r);
    if (!g_coinTex) return;

    const float tSec = (SDL_GetTicks() - g_winTime) / 1000.0f;
    const float t = std::min(1.0f, tSec / 1.6f);
    const float e = 1.0f - std::pow(1.0f - t, 3.0f);                // smooth: fast at first, gently settling

    const int panelH = 7 * SCORE_SCALE + 2 * PANEL_PAD;
    const int px = (FIELD_W + (WIN_W - 60) - PANEL_W) / 2;          // same as the score panels
    float endX = px - 10 - COIN_SIZE / 2;                           // stops to the left of the winner's panel
    float endY = ROW_Y0 + g_winner * ROW_STEP + panelH / 2.0f;
    endY = std::max(16 + COIN_SIZE / 2, std::min((float)FIELD_H - 16 - COIN_SIZE / 2, endY));
    const float startX = endX - 150, startY = FIELD_H + COIN_SIZE;  // below the bottom edge of the screen

    // pure translation: only the position changes - no rotation, no scaling
    const float cx = startX + (endX - startX) * e;
    const float cy = startY + (endY - startY) * e;
    SDL_Rect dst = {(int)std::lround(cx - COIN_SIZE / 2), (int)std::lround(cy - COIN_SIZE / 2),
                    (int)COIN_SIZE, (int)COIN_SIZE};
    SDL_RenderCopy(r, g_coinTex, nullptr, &dst);

    if (t >= 1.0f) {                                                // twinkling sparkles once it has landed
        static const float off[6][2] = {{-44, -46}, {46, -50}, {-52, 6}, {50, 22}, {-26, 56}, {34, -14}};
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        for (int k = 0; k < 6; ++k) {
            float a = std::sin(tSec * 4.0f + k * 1.9f);
            if (a <= 0) continue;
            int sx = (int)(cx + off[k][0]), sy = (int)(cy + off[k][1]), len = (int)(3 + 11 * a);
            SDL_SetRenderDrawColor(r, 255, 240, 160, (Uint8)(255 * a));
            SDL_RenderDrawLine(r, sx - len, sy, sx + len, sy);
            SDL_RenderDrawLine(r, sx, sy - len, sx, sy + len);
            SDL_RenderDrawLine(r, sx - len / 2, sy - len / 2, sx + len / 2, sy + len / 2);
            SDL_RenderDrawLine(r, sx - len / 2, sy + len / 2, sx + len / 2, sy - len / 2);
        }
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
    if (g_state == MATCH_OVER) drawCoin(r);
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

// Fire explosion, 1.2 seconds, 16000 Hz, mono 16-bit (synthesized with numpy/scipy).
static const int EXPLOSION_RATE = 16000;
static const short g_explosion[19200] = {
0, 22, 128, 370, 749, 1238, 1745, 2175, 2700, 3546, 4514, 5359, 6021, 6330, 6467, 7204, 9207, 12052, 14056, 14116, 13070, 12690, 13513, 14799,
15772, 15844, 15407, 15720, 16189, 15527, 15466, 17944, 20701, 21150, 18828, 14777, 11342, 10204, 10688, 11750, 14784, 18977, 20960, 20172, 16646, 10657, 7851, 12152,
17428, 19549, 20278, 21181, 21202, 19164, 14282, 4424, -6543, -9035, -4102, 286, 1165, 2661, 6056, 7118, 3728, -1707, -3924, 627, 6208, 4351, -4754, -14011,
-18262, -18180, -16380, -15945, -17507, -18712, -17879, -15760, -14566, -14686, -15982, -17785, -16662, -12318, -11937, -16466, -17080, -9205, 2938, 7602, 1562, -11244, -18885, -18266,
-11182, -4400, -8944, -20904, -26478, -27181, -25571, -21295, -13278, -2668, 5096, 3252, -8640, -18381, -20393, -16920, -9435, -1001, 7517, 17340, 24444, 26676, 25354, 18762,
5033, -6738, -8715, -3710, -754, -3136, -2634, 5140, 13587, 18243, 21696, 24857, 25836, 23897, 20453, 19354, 19813, 20794, 23799, 26630, 27138, 25374, 22271, 20787,
21692, 22068, 18561, 8505, -3026, -6597, -567, 12340, 22692, 26563, 26915, 24959, 21105, 17954, 18122, 19763, 19612, 17521, 15823, 16011, 16510, 14334, 9120, 5240,
5589, 8339, 9532, 5916, -74, -1337, 4238, 12358, 19048, 23667, 25415, 24014, 20616, 18876, 18165, 16816, 17530, 20293, 21750, 20613, 15984, 7689, 305, -452,
7821, 18632, 22359, 18824, 9840, 1935, -5555, -14421, -18619, -16195, -10542, -9467, -14353, -19684, -22341, -21272, -13750, -1387, 3913, 1977, -1351, -5809, -13329, -21901,
-26576, -27793, -28103, -28703, -28799, -27375, -23854, -19696, -14550, -10789, -14578, -20913, -24414, -26416, -28111, -29402, -30066, -30299, -30285, -29939, -28962, -27493, -26205, -26048,
-26703, -26847, -26911, -27402, -27166, -25554, -23313, -20109, -13227, -6580, -12973, -25980, -29754, -30200, -30035, -29368, -26175, -7960, 18215, 24383, 21679, 9697, -7606, -16113,
-15845, -9594, -1619, 856, -3020, -5213, 2345, 16592, 24526, 26046, 24672, 22755, 21237, 17904, 12520, 6768, -1575, -12195, -20112, -23864, -23506, -17375, -6530, -1600,
-6155, -14845, -19764, -17089, -4276, 9261, 15247, 19783, 23683, 24998, 24978, 25695, 26985, 27683, 27637, 26685, 24064, 18967, 12226, 7565, 8120, 12551, 19035, 25273,
28250, 28844, 27958, 24883, 19100, 12046, 6176, 5158, 7741, 8229, 7491, 11476, 18394, 22790, 24737, 25227, 24663, 23512, 20662, 13573, 4745, -1505, -10296, -20939,
-25929, -26905, -26454, -25580, -24194, -22111, -20705, -20637, -19181, -13426, -4267, 3645, 8062, 8664, 9515, 16152, 23330, 25970, 25127, 19280, 3827, -14691, -24531, -27941,
-28834, -28523, -26715, -22089, -14457, -6907, -2298, -1365, -4362, -12390, -21628, -25818, -25954, -23027, -14972, 598, 14338, 19704, 20042, 15810, 6280, -642, 374, 3183,
4763, 7120, 8192, 6108, 3659, 2062, -2557, -12527, -22794, -28043, -29580, -29746, -29268, -28664, -28517, -28298, -27098, -22923, -12547, 1137, 10996, 14477, 11314, 2398,
-6793, -11574, -14378, -17601, -19658, -18931, -14579, -7456, -2128, -1967, -4640, -4133, 588, 3917, 4289, 6898, 14042, 20090, 22006, 20556, 15882, 10569, 9569, 11051,
10009, 7066, 7471, 11462, 14091, 15870, 21426, 27037, 29091, 29593, 29775, 30042, 30328, 30461, 30473, 30429, 30344, 30141, 29746, 29297, 29053, 29004, 28879, 28334,
26813, 23042, 15561, 6282, -284, -1191, 3223, 7706, 10002, 13345, 17198, 17592, 12089, 721, -10266, -16308, -18430, -18327, -16665, -14074, -13548, -17491, -23516, -27318,
-28022, -25440, -15799, -807, 10361, 18161, 24263, 27152, 27091, 23445, 14456, 7097, 10170, 19091, 24249, 25288, 24088, 20723, 15667, 11948, 9211, 3418, -5672, -13286,
-17884, -20483, -20350, -17862, -17488, -21054, -23911, -23841, -21018, -16652, -12769, -11749, -14763, -17631, -17297, -16587, -19645, -24921, -28397, -29740, -30176, -30289, -30200, -29762,
-28552, -26453, -23252, -15974, -6081, -4957, -12331, -19625, -24506, -27536, -28932, -29277, -29141, -29116, -29454, -29815, -30051, -30171, -30121, -29770, -28906, -27249, -24317, -19115,
-10102, 1726, 11022, 13396, 5233, -13636, -25454, -28535, -29187, -29238, -29098, -28713, -27176, -21893, -10939, 841, 8488, 11148, 9524, 3405, -7267, -16534, -18464, -12608,
-2772, 2269, 297, -6760, -13334, -14279, -10710, -9971, -16647, -23507, -25895, -24692, -17550, -335, 15177, 22017, 25726, 28144, 29065, 28742, 26998, 24907, 25376, 27603,
29240, 29791, 29516, 27887, 24756, 23849, 26002, 28125, 29337, 29873, 29863, 28911, 25326, 19807, 18960, 23023, 26949, 28645, 28725, 27426, 25200, 24518, 25358, 25087,
21940, 15210, 7500, 3438, 7031, 16179, 22887, 25491, 25668, 24212, 22323, 21824, 23021, 24300, 23181, 17483, 9429, 6405, 10206, 18389, 25942, 29298, 30113, 30207,
30119, 30006, 29761, 29357, 29248, 29410, 29331, 29000, 28806, 28753, 28640, 28869, 29412, 29632, 29204, 26799, 17196, 1827, -3167, 2936, 12314, 17000, 16251, 13120,
11008, 10185, 9560, 8381, 5610, 2567, 2844, 4936, 1546, -9795, -19856, -24203, -26161, -27414, -27486, -25121, -18023, -7521, -211, -816, -9811, -19298, -22928, -21649,
-15441, -6258, -3076, -9475, -17971, -23299, -26579, -28367, -28759, -27547, -22595, -11167, 1179, 4251, -3589, -13623, -17467, -15314, -8318, 465, 6217, 5940, -1651, -12770,
-19774, -21939, -21881, -22216, -23960, -25799, -27179, -28289, -29079, -29549, -29710, -29527, -28939, -28026, -27083, -25869, -22780, -15398, -5286, 239, -3243, -14189, -22922, -25742,
-25603, -24784, -24542, -24069, -21943, -17728, -12835, -8799, -6622, -7867, -11135, -12086, -9170, -6156, -8843, -16261, -22287, -25565, -27405, -28219, -27946, -26261, -22785, -17464,
-10267, -1759, 6692, 12415, 13388, 10363, 5014, -3722, -15758, -24348, -27613, -28342, -28044, -27157, -25018, -19778, -12567, -9828, -12111, -14062, -12042, -3476, 11776, 23614,
27921, 29051, 29262, 29330, 29557, 29732, 29680, 29428, 28917, 27804, 25173, 19095, 10025, 2996, -1612, -6179, -10325, -12370, -11223, -5211, 6565, 17743, 22670, 22734,
19095, 12801, 6294, 2331, 3768, 11084, 20079, 25687, 28070, 29187, 29827, 30080, 29953, 29125, 26684, 22889, 20522, 19488, 17410, 13506, 7842, 962, -4149, -4370,
-53, 4737, 6811, 6388, 3246, -3623, -12187, -18272, -20142, -18020, -12469, -5533, -390, 2409, 4814, 5855, 2691, -2843, -5281, -2921, 2635, 10632, 19769, 26059,
28754, 29593, 29579, 28890, 27608, 25661, 21825, 15618, 8674, 1592, -4510, -9624, -14762, -17570, -16041, -10839, -5315, -1521, 1764, 3660, 1285, -4671, -9430, -9791,
-4886, 6021, 18685, 25740, 28071, 28527, 28277, 27733, 26300, 20477, 363, -22607, -29029, -30110, -30278, -30163, -29781, -29194, -28548, -27637, -25736, -21060, -11259, -360,
2853, -2451, -10834, -17454, -21690, -23516, -22449, -17711, -9925, -2381, 1454, -1391, -10700, -19845, -24773, -26874, -27503, -27180, -25897, -23029, -17433, -9496, -4338, -6816,
-14949, -21790, -24751, -25495, -25332, -24325, -21261, -14237, -4035, 4470, 8525, 8296, 4112, -4781, -16371, -23804, -26217, -25645, -21273, -11873, -6191, -12309, -22713, -27609,
-28698, -28378, -27445, -26557, -26249, -26364, -26082, -24952, -22861, -19012, -12537, -6826, -6754, -10643, -14995, -19248, -22795, -24643, -24776, -23321, -20059, -14864, -7509, 1942,
9231, 8687, -655, -10114, -10854, -3178, 6530, 12157, 14727, 16598, 18728, 20936, 22890, 24274, 24411, 23186, 21854, 21993, 23655, 25820, 27419, 28086, 27854, 26531,
23658, 19031, 12598, 5767, 4481, 12261, 21297, 24979, 25148, 23776, 21996, 19659, 16133, 12282, 10806, 14316, 21174, 26091, 27879, 27882, 26708, 25138, 24750, 25672,
26674, 26904, 25983, 23722, 19688, 13641, 9688, 11359, 13686, 12190, 7443, 985, -6920, -15038, -21131, -23961, -23139, -16906, -5568, 2161, 3334, 1870, 2247, 6843,
13561, 18137, 18556, 14558, 9007, 7688, 10961, 15171, 18336, 19838, 19160, 16166, 11472, 6464, 2575, 23, -331, 4962, 16379, 25429, 28689, 29437, 29368, 28836,
27387, 23389, 16037, 8820, 3576, -1442, -6873, -10205, -8785, -3835, 600, 4874, 11076, 15802, 15991, 12178, 5545, -3826, -12841, -16571, -14108, -5519, 5228, 10335,
7658, 415, -4859, -4852, -396, 5178, 8254, 7173, 1177, -7379, -13185, -15458, -17662, -21983, -25806, -27016, -25538, -20702, -14539, -10813, -8593, -4961, 855, 6158,
9072, 10500, 9849, 3884, -7549, -16435, -18679, -15105, -5732, 5359, 11089, 9260, -151, -11971, -18522, -19451, -17311, -17324, -22618, -27520, -29429, -29901, -29712, -28670,
-26402, -23357, -19305, -12905, -4540, 2816, 6752, 6847, 3754, -1550, -7819, -13539, -18063, -21361, -22721, -21623, -19322, -18987, -20732, -21997, -21677, -19464, -14826, -7554,
1742, 11625, 19127, 23059, 24865, 25683, 25531, 23323, 15944, 1037, -13280, -21053, -24528, -25812, -25884, -25406, -25147, -25415, -25616, -24826, -21960, -16397, -10538, -8139,
-9121, -10230, -8713, -3946, 3075, 10889, 17885, 22369, 23783, 21380, 11151, -7390, -20090, -24773, -26002, -25577, -24285, -23693, -24936, -26604, -27319, -26936, -25213, -20702,
-11282, -1028, 1449, -4708, -12946, -16335, -12750, -1626, 10107, 14487, 12275, 7603, 7494, 14242, 21204, 24901, 27042, 28354, 28772, 28455, 27363, 24829, 20761, 19496,
23359, 27205, 28974, 29610, 29651, 29018, 27444, 26290, 27113, 28401, 29167, 29465, 29269, 27980, 23104, 9174, -10052, -20354, -22476, -19944, -14451, -9575, -7352, -6204,
-3486, 2016, 7553, 10238, 11741, 15145, 20517, 25502, 28354, 29389, 29489, 28769, 26371, 21943, 19713, 21735, 23076, 19661, 7739, -6753, -11957, -8099, 325, 4923,
906, -9010, -16521, -18622, -16740, -11299, -917, 12725, 21548, 24107, 23012, 19102, 12881, 6298, 1904, 506, 1858, 6798, 14706, 20093, 19612, 12175, 454, -7606,
-11771, -16372, -21783, -24833, -24466, -19234, -6702, 6945, 14387, 17160, 17487, 15402, 10159, 2915, -4022, -10769, -17302, -21710, -23112, -21567, -16710, -8245, 3069, 14056,
21150, 24156, 24551, 23440, 21272, 17959, 13995, 9245, 1394, -9029, -17279, -21283, -21434, -17932, -11573, -3253, 8514, 20379, 26648, 28815, 29246, 28408, 24947, 18075,
13310, 14496, 17746, 18007, 11280, -5100, -19654, -25406, -27189, -27693, -27672, -27387, -26903, -25967, -24081, -21152, -18635, -18079, -18446, -18468, -18583, -19533, -20932, -21846,
-22185, -22408, -22297, -21487, -20610, -20457, -20228, -18400, -14931, -12134, -12413, -16214, -21794, -25868, -27369, -26924, -24552, -21108, -19772, -21045, -22531, -23615, -25090, -26708,
-27552, -27488, -26813, -25897, -25292, -25756, -27000, -27858, -27897, -27043, -24554, -17923, -4583, 9292, 16354, 18220, 17607, 15484, 12192, 7761, 278, -10378, -18452, -21457,
-20778, -16824, -9914, -3838, -4460, -12295, -19508, -21304, -18584, -13643, -10644, -11812, -15582, -18284, -17649, -13676, -8430, -4843, -3755, -3435, -1998, -200, -1566, -7780,
-14787, -19155, -21598, -22292, -19702, -11081, 2711, 12647, 14882, 9794, -2545, -16017, -23743, -26733, -27558, -27492, -27118, -26303, -24066, -20320, -19084, -22657, -26259, -27441,
-26822, -24903, -22501, -20309, -17787, -13321, -5763, 2655, 8596, 13151, 18495, 23238, 25712, 26219, 24984, 21182, 13631, 3776, -3990, -7330, -6289, -1510, 4732, 8895,
9349, 6084, -1022, -10526, -17922, -21231, -21853, -21194, -19825, -17580, -13724, -7285, 2719, 14598, 22598, 25596, 25843, 24339, 21141, 16817, 13473, 13251, 16505, 21376,
25071, 26594, 26228, 23811, 19215, 14165, 11311, 11625, 13339, 13574, 11202, 7295, 5153, 8231, 14751, 19198, 19851, 17485, 12874, 6182, -1191, -4659, -2028, 3879,
9779, 15151, 19393, 22048, 23746, 24904, 25264, 24637, 23409, 22275, 21611, 21491, 21514, 21273, 21156, 21046, 18901, 11796, 646, -6943, -7447, -681, 11683, 21504,
25240, 25411, 23554, 21378, 21405, 23153, 24420, 24653, 23005, 16781, 7270, 3701, 4353, 663, -7415, -12920, -13488, -9516, -1821, 7107, 15408, 21105, 22979, 21799,
19787, 20049, 22530, 25431, 27489, 28127, 27230, 24220, 18956, 13648, 11246, 11227, 11976, 13149, 13487, 12543, 12331, 13688, 15403, 17653, 20874, 23446, 23645, 19543,
7437, -8309, -17025, -19302, -18134, -16758, -18930, -22706, -23916, -21391, -14922, -7428, -2515, -841, -2288, -4678, -4221, -1520, -1414, -4988, -9356, -13810, -18514, -21872,
-23346, -24026, -24770, -25304, -24980, -22985, -17957, -8360, 5112, 16322, 21696, 23233, 21898, 16267, 5485, -6254, -14193, -17347, -16797, -14199, -12084, -12968, -17264, -22593,
-26155, -27636, -27910, -27528, -26647, -25144, -22550, -18009, -11100, -3104, 3160, 4338, -2227, -14544, -23952, -27874, -29113, -29318, -28804, -27071, -23327, -18301, -13900, -10209,
-6022, -1040, 3721, 7378, 9590, 9638, 5597, -4090, -14948, -21032, -22746, -21308, -16418, -8397, 20, 6032, 8378, 6847, 2033, -4149, -9398, -12898, -14854, -15954,
-17516, -20374, -23402, -25033, -24941, -23005, -18425, -11224, -5292, -3981, -5716, -6799, -4261, 1432, 5606, 5041, 327, -6622, -13813, -19323, -22359, -22904, -20193, -12195,
729, 12161, 18275, 20374, 19785, 16495, 11148, 7615, 8880, 12974, 16647, 18466, 18248, 16195, 12368, 6570, -538, -6986, -11359, -13666, -14929, -17013, -20705, -24018,
-25221, -23601, -16391, -401, 14744, 21563, 23789, 23916, 22363, 18990, 13456, 6029, -636, -4130, -4622, -2815, 788, 4903, 7836, 9165, 9837, 11032, 12613, 12705,
9728, 4476, 873, 2646, 8686, 13914, 15735, 14357, 10424, 6388, 5820, 8545, 11302, 12563, 13459, 15349, 17235, 16570, 11925, 5849, 4693, 10239, 17350, 21627,
23214, 23153, 22148, 20480, 17286, 11702, 5816, 2490, 682, -2491, -7345, -10475, -8873, -3041, 2979, 6615, 9036, 10752, 10544, 7991, 4085, 506, -1014, 1043,
7504, 15998, 22207, 25284, 26361, 26003, 24105, 20522, 14964, 5498, -8007, -18242, -21288, -17315, -2933, 15432, 24467, 27152, 27338, 25405, 18925, 5867, -5462, -8987,
-7375, -2864, 3938, 10976, 15597, 17395, 17382, 17382, 19167, 22238, 24649, 25640, 25477, 24752, 24299, 24314, 24178, 23509, 21816, 17809, 10484, 1401, -5881, -8872,
-6712, 768, 10614, 17219, 18958, 16581, 10987, 4077, -2806, -8767, -12332, -12658, -9331, -3060, 2812, 5598, 5713, 4500, 2797, 1789, 3977, 10787, 18638, 23099,
24237, 23276, 20912, 17824, 14798, 12343, 11220, 12713, 16588, 20311, 21634, 19937, 15896, 11760, 9337, 9287, 11233, 13557, 15334, 16409, 15477, 10202, 274, -9195,
-14343, -16582, -18527, -20842, -22140, -21478, -18761, -14217, -7955, -36, 8012, 13879, 17274, 18900, 18772, 16735, 13279, 9750, 8211, 10494, 15679, 19782, 20436, 17192,
9838, 45, -8920, -15594, -20203, -22944, -24045, -24140, -24259, -25143, -26402, -27447, -28216, -28674, -28575, -27443, -24537, -19923, -14935, -10818, -8069, -7367, -9156, -11446,
-13908, -17574, -19570, -17529, -13228, -12161, -15416, -17844, -16039, -11609, -9952, -11484, -12644, -12972, -13813, -15314, -16489, -17183, -18113, -20009, -23090, -25587, -26262, -25571,
-24819, -25046, -25302, -24217, -20537, -12571, -2008, 5195, 7888, 8152, 6062, 824, -5688, -10844, -13956, -15036, -14985, -15849, -17751, -18356, -16625, -12480, -5153, 3326,
8241, 9253, 7868, 4180, -720, -4915, -9196, -15055, -20960, -24442, -24902, -21497, -11753, 528, 5344, 165, -10812, -18661, -20850, -18411, -11223, -1730, 4575, 4866,
-760, -8895, -14769, -17324, -18062, -18275, -17796, -15655, -10843, -2998, 5128, 9728, 11053, 11394, 11341, 9793, 6735, 4670, 6471, 11577, 16347, 18544, 17810, 13787,
6511, -1996, -8476, -11337, -10897, -7614, -2284, 2685, 5243, 6118, 7869, 12259, 17905, 21991, 23821, 24389, 25222, 26960, 28399, 28924, 28719, 27456, 23741, 17248,
11627, 9278, 10588, 14003, 16812, 18621, 20090, 20558, 18835, 15097, 11601, 10089, 10555, 12992, 16450, 18038, 14951, 5430, -6765, -14653, -17480, -17200, -16071, -16206,
-16814, -14086, -4028, 10403, 19867, 23852, 24715, 23163, 19919, 17643, 17106, 16461, 16404, 18860, 22530, 25253, 26393, 25726, 22931, 19367, 17212, 15157, 12501, 12158,
15966, 21060, 23866, 23720, 21127, 18404, 18991, 21751, 23470, 23313, 21400, 17623, 12750, 9538, 9809, 12096, 13028, 10513, 6366, 4640, 5131, 4278, 1424, 439,
4992, 13134, 19298, 21833, 21887, 20726, 19740, 19660, 19786, 19468, 19148, 18992, 18050, 15750, 13139, 12202, 13537, 15791, 17639, 18662, 18712, 17663, 15778, 14269,
13987, 13558, 11593, 9058, 7288, 5581, 3219, 1381, 2485, 8749, 18498, 25596, 28494, 29309, 29176, 28037, 24989, 18816, 9805, 1525, -2504, -2124, 809, 4075,
5670, 4429, 920, -3072, -5945, -6409, -3942, 339, 4530, 7272, 7853, 5859, 955, -5968, -12191, -15617, -15679, -12306, -6647, -1800, 323, 627, 480, 52,
-701, -691, 1518, 6130, 11264, 13950, 12669, 8026, 2392, -2125, -4768, -5082, -2746, 1796, 6659, 9469, 9302, 6958, 4090, 1731, -406, -3064, -6099, -8520,
-10171, -12224, -15315, -18174, -19221, -17927, -14282, -8417, -1376, 4043, 5290, 1582, -6082, -14144, -19186, -20769, -19432, -15284, -9059, -3551, -2238, -6338, -12668, -16425,
-16495, -14133, -11290, -9616, -9440, -10527, -12400, -13488, -12105, -8160, -3873, -1940, -2686, -4568, -5986, -5874, -4488, -3826, -5022, -7068, -9267, -11400, -12222, -10657,
-7056, -2553, 1541, 3431, 2237, -521, -2574, -3404, -3438, -2853, -2509, -3697, -6564, -10302, -13868, -15931, -15757, -13960, -12242, -12047, -13462, -15460, -16414, -14727,
-9549, -2208, 4365, 9009, 11659, 11844, 9752, 7602, 8266, 11878, 15780, 17620, 16783, 13573, 8732, 3235, -2328, -7908, -12897, -15939, -16146, -13906, -11830, -13334,
-17907, -21858, -23418, -22980, -21726, -21048, -20839, -19576, -15777, -9163, -2634, 139, -664, -2421, -2990, -1464, 1736, 5264, 7615, 7102, 2883, -3321, -8004, -8875,
-5229, 2316, 10772, 17126, 21262, 23979, 25558, 26253, 26272, 25492, 23549, 20184, 16029, 12884, 11674, 10910, 8386, 3689, -1569, -6298, -10714, -14906, -18280, -19830,
-18804, -15167, -10374, -6536, -4138, -2453, -662, 1990, 6134, 11602, 16723, 19702, 20143, 18431, 15090, 10897, 6999, 3925, 1493, 98, 338, 2300, 5687, 9689,
13254, 16052, 18339, 20072, 21067, 21220, 20464, 19400, 19302, 20372, 21564, 22246, 22546, 22582, 22080, 20873, 19275, 17825, 17088, 17440, 18832, 20779, 22365, 22992,
22974, 22889, 22389, 20467, 16969, 13554, 11898, 11640, 11506, 11097, 11227, 12959, 16399, 20065, 22252, 22513, 21177, 18863, 16203, 13767, 12224, 11662, 11234, 9629,
5975, 1264, -2005, -2589, -1292, 991, 4935, 10894, 16488, 19401, 19470, 16827, 11179, 3974, -199, 1671, 7457, 11529, 10588, 5012, -1630, -5252, -4845, -1313,
3923, 9449, 14295, 17824, 19702, 20107, 19450, 18061, 16310, 14548, 12913, 11831, 11658, 11465, 9878, 7122, 5499, 6937, 10415, 13253, 14190, 13452, 11031, 6407,
87, -6451, -12406, -17205, -20336, -22025, -22804, -22798, -21862, -20399, -19275, -18364, -16685, -13887, -10412, -6621, -3335, -2349, -4252, -7293, -9280, -9483, -8832, -8806,
-9560, -9936, -9246, -8057, -7791, -9571, -13396, -18028, -21486, -22567, -20679, -14470, -3288, 8394, 14909, 15591, 11686, 5295, -715, -4963, -7807, -10600, -14456, -18824,
-22128, -23529, -22935, -20494, -17102, -13564, -9198, -4659, -2872, -4295, -6115, -6258, -5516, -5847, -6982, -6850, -5338, -5130, -8179, -12456, -14496, -13242, -10017, -7570,
-7588, -9023, -9918, -9925, -9857, -9739, -8654, -6027, -3231, -3624, -8824, -15963, -21331, -24221, -25263, -24896, -23177, -20122, -16116, -11767, -8497, -8200, -10664, -14354,
-18311, -21379, -22781, -22972, -22892, -22875, -22160, -18995, -10925, 1006, 9337, 10997, 8116, 3575, -399, -2321, -1200, 2438, 5566, 4991, -344, -9033, -16570, -20006,
-20173, -18874, -18158, -19428, -22063, -24245, -25052, -24530, -23015, -21186, -19970, -19772, -19721, -18113, -13949, -8252, -3237, 41, 1867, 2738, 2217, -93, -2505, -2945,
-2103, -1947, -2040, -278, 3161, 5431, 4663, 1718, -1055, -2403, -2752, -2225, -348, 2020, 2654, 737, -1857, -3208, -3992, -6079, -9789, -13371, -14813, -12730,
-5958, 3958, 11191, 12372, 7735, 300, -4426, -3917, 854, 6729, 10646, 12235, 12842, 13953, 16248, 19251, 21885, 23426, 23792, 23362, 22832, 22654, 22636, 22552,
22396, 21523, 18499, 12805, 7125, 4361, 3734, 3035, 1646, 624, 238, -1046, -4569, -10013, -15734, -19646, -20638, -18235, -11865, -3238, 3158, 5004, 2955, -232,
-590, 4168, 12135, 17976, 18975, 15473, 10248, 6570, 3671, 1932, 2972, 6468, 11546, 15641, 17040, 16253, 13105, 7391, 1029, -4771, -8901, -7473, 1324, 12418,
20212, 24020, 24911, 23752, 20376, 14733, 10068, 10067, 14380, 20393, 24940, 27145, 27827, 27623, 27006, 26269, 24781, 21488, 16246, 9517, 2614, -1578, -2445, -1394,
465, 3374, 7842, 12344, 15636, 17509, 17523, 16117, 14706, 14671, 16184, 17724, 18426, 18563, 17947, 16125, 13578, 12165, 13654, 17304, 20424, 21465, 20277, 17057,
13089, 11282, 13111, 16639, 19613, 21662, 22959, 23292, 22463, 20575, 17975, 15242, 12681, 9460, 5077, 1009, -1721, -4217, -7301, -10512, -13226, -14676, -14143, -11879,
-8918, -5640, -1558, 3272, 7946, 11681, 14254, 16001, 17344, 17863, 16512, 12958, 9036, 7709, 9282, 11389, 12412, 12739, 13292, 14305, 15563, 16711, 17258, 16735,
14832, 11466, 6806, 1420, -3378, -5770, -4762, -1582, 885, 1096, -387, -2768, -5853, -9440, -12704, -14115, -12185, -6527, 949, 6923, 9278, 7875, 4422, 1738,
2234, 6684, 12513, 15799, 15424, 12191, 7582, 3109, -917, -4925, -7898, -7948, -5043, -1687, -813, -3323, -7618, -11194, -13141, -14357, -15934, -17585, -17948, -15956,
-11526, -6386, -3356, -3594, -5503, -6208, -4426, -1535, 528, 1949, 4086, 6730, 8407, 8247, 5858, 1060, -5374, -11780, -16995, -20788, -23184, -24343, -24553, -24138,
-23703, -23808, -24110, -23918, -22924, -21187, -19699, -20207, -22507, -24549, -25279, -24656, -22909, -20721, -18804, -16597, -12408, -5486, 1894, 6392, 6383, 1538, -5703, -10383,
-10581, -7810, -4717, -3073, -2945, -3535, -3985, -3833, -3584, -4504, -7216, -10857, -13967, -15813, -16315, -15442, -13154, -9628, -5507, -1229, 3668, 8805, 12313, 13340,
12538, 10881, 9250, 8040, 6531, 3437, -2108, -9667, -16771, -21165, -22781, -22366, -20497, -17567, -13667, -9155, -5771, -5310, -7537, -11032, -14703, -17685, -19006, -17927,
-14135, -8413, -3031, 50, 506, -480, -865, 658, 3811, 7726, 11821, 15191, 16543, 15483, 12987, 10381, 7880, 4864, 1029, -2623, -4323, -3550, -1501, 650,
2507, 3152, 1818, -419, -1768, -1477, 143, 1676, 1433, -814, -4145, -7454, -9599, -9880, -8463, -6898, -7467, -10242, -12459, -11409, -5999, 1929, 7887, 9901,
9312, 8386, 8668, 10458, 13109, 15529, 17100, 17745, 17132, 14554, 9810, 4404, 721, -583, -659, -504, 106, 2090, 5498, 8794, 10515, 10384, 8654, 6162,
4937, 7065, 12454, 18287, 21876, 22688, 20968, 17271, 13188, 9818, 6673, 3584, 1647, 1533, 2291, 2532, 2010, 1878, 3353, 6417, 10699, 15997, 20790, 23511,
24290, 23795, 22490, 20760, 19131, 18158, 17830, 17420, 16348, 14765, 13240, 12345, 12313, 12576, 12655, 13280, 14978, 16677, 16992, 15076, 10658, 5303, 2117, 1929,
2679, 2399, 955, -600, -1134, 366, 4663, 11024, 16951, 20615, 21901, 21337, 19419, 16461, 13027, 10693, 10772, 12440, 13772, 13925, 13396, 12487, 10231, 5750,
392, -2865, -2179, 2110, 7773, 12922, 17278, 20602, 22259, 21836, 19241, 15436, 12723, 12150, 12463, 12217, 11018, 9440, 8663, 9409, 10844, 11067, 8940, 5466,
3199, 3282, 4411, 4704, 3710, 2974, 4258, 7309, 10118, 10890, 9313, 6253, 2630, -1404, -5757, -9084, -10112, -9426, -8158, -6413, -4380, -3074, -3157, -4471,
-6443, -8172, -8455, -6317, -1763, 3766, 8053, 9896, 10062, 10210, 10627, 10427, 9366, 7601, 5829, 6152, 9378, 12197, 10929, 4907, -4112, -13383, -19832, -22395,
-22205, -20691, -18784, -15955, -11224, -5285, -213, 2986, 4352, 3519, 317, -3757, -6484, -7221, -7328, -8885, -12351, -16274, -19749, -22533, -23911, -23188, -19468, -11682,
-2100, 2945, 965, -4683, -8208, -6906, -1835, 3466, 5303, 1982, -4625, -8772, -6335, 2507, 11865, 16776, 18219, 18362, 18244, 17136, 13119, 5182, -3702, -8820,
-8673, -4034, 2504, 7005, 7652, 6320, 6809, 10337, 13609, 13023, 7278, -1856, -9119, -11090, -7622, -639, 5629, 8144, 6522, 1706, -4382, -9899, -14192, -17205,
-18738, -18630, -17228, -16174, -17495, -20384, -22236, -21914, -19733, -17193, -15637, -14686, -13608, -12538, -12269, -13578, -16349, -19543, -22336, -24402, -25537, -25645, -24388, -20447,
-11650, 271, 7941, 7984, 803, -9998, -17576, -19951, -18864, -15884, -11130, -3098, 7536, 15579, 17932, 14627, 6530, -1805, -6743, -9540, -11673, -12351, -9709, -2252,
8000, 15370, 18205, 18392, 17845, 17390, 16532, 14534, 11121, 6022, -665, -7797, -13764, -17023, -16833, -13091, -5898, 3457, 11011, 13168, 9025, 9, -8831, -13935,
-14753, -10097, 1376, 14261, 21391, 23669, 23399, 21612, 19187, 17335, 16509, 15672, 13447, 8999, 2208, -4738, -8118, -6926, -2382, 3846, 9030, 10827, 9365, 5748,
1081, -2610, -3853, -3444, -2527, -1535, -1169, -1617, -1716, -228, 2996, 7222, 11374, 13859, 12949, 7861, 219, -6092, -8510, -6882, -2835, 214, -1083, -6871,
-13284, -16635, -15844, -10598, -2651, 2977, 3804, 1431, -1290, -1878, 462, 3689, 5372, 6154, 8105, 11095, 13091, 12998, 11327, 9539, 9501, 12319, 16668, 19910,
21199, 21266, 20678, 19427, 17968, 17406, 18158, 19389, 19611, 17853, 14744, 12173, 10604, 8990, 7572, 8306, 11775, 15608, 17508, 17237, 15924, 15346, 16539, 18842,
20979, 22160, 21917, 19640, 14957, 9849, 8091, 9988, 12398, 13349, 12959, 11442, 8853, 6216, 5175, 6522, 9287, 11001, 9518, 4497, -1866, -6570, -8846, -9198,
-7445, -3420, 1520, 5708, 8805, 10699, 10928, 9763, 8548, 8117, 8105, 8388, 8814, 8409, 7112, 6989, 9818, 14194, 17621, 19658, 20843, 21204, 20478, 18726,
16467, 14426, 13086, 11973, 9876, 6632, 3822, 2582, 2226, 1731, 958, 353, 250, 521, 819, 1115, 1896, 4062, 8251, 13215, 16402, 16588, 14013, 10264,
7967, 8065, 9273, 10254, 10543, 10354, 10407, 11083, 11713, 11320, 9501, 6965, 5732, 7304, 10549, 12685, 11982, 8638, 4291, 634, -1648, -2864, -3960, -5402,
-6471, -6241, -4882, -3688, -3543, -4137, -5383, -7867, -11148, -13577, -13857, -11256, -5525, 1712, 7218, 9374, 8362, 4854, 542, -2063, -1963, -188, 1810, 3266,
4137, 4653, 4637, 3936, 3461, 4112, 5118, 5134, 3669, 701, -3588, -8457, -12348, -13754, -12460, -9703, -7671, -8001, -10402, -13300, -15549, -16959, -17694, -17375,
-14861, -9324, -2392, 2727, 5211, 5750, 4323, 680, -4364, -8632, -9842, -7197, -2102, 2369, 4023, 2468, -1574, -6430, -9747, -9574, -5090, 2291, 8609, 11074,
9102, 3151, -3465, -5951, -3201, 2146, 6859, 9665, 10352, 9176, 7105, 5050, 3189, 1117, -1952, -5739, -8358, -8893, -8334, -8009, -8770, -11150, -14664, -17610,
-18470, -16250, -10068, -1109, 6029, 7763, 3126, -6203, -14568, -18467, -18648, -16944, -15811, -16574, -17885, -17957, -16472, -14691, -14591, -16867, -20104, -22698, -24265, -24869,
-24527, -23427, -22062, -20774, -19216, -16827, -13811, -11015, -8722, -6722, -5378, -5444, -7353, -10967, -15158, -18285, -19341, -17629, -12066, -3136, 4490, 7617, 7462, 5601,
2275, -1675, -4288, -4699, -3923, -4029, -6190, -9310, -12115, -14630, -16563, -17214, -16813, -16470, -16783, -16752, -14664, -9554, -2526, 3472, 6678, 7012, 4394, -1026,
-7561, -12690, -15140, -15261, -14248, -13108, -11949, -10337, -7687, -3129, 4001, 12163, 18025, 20386, 19619, 15266, 6400, -4604, -12233, -14575, -12807, -8563, -3527, 829,
3140, 2604, -58, -3106, -5640, -7457, -7643, -5159, -30, 6407, 11809, 13952, 11583, 4673, -3355, -7780, -7322, -2586, 4746, 11834, 16821, 19664, 21081, 21695,
21721, 21030, 19349, 16580, 13364, 11180, 11033, 12126, 12490, 10786, 7371, 4378, 4019, 6223, 8833, 10136, 10231, 10171, 10242, 9463, 7353, 5533, 6364, 9777,
13291, 15313, 16164, 16638, 16919, 16527, 14797, 11511, 7595, 4551, 2882, 2175, 1730, 848, -267, -809, -1008, -1876, -3309, -3610, -1051, 4373, 10360, 14129,
14890, 13640, 12012, 11425, 12397, 14124, 15462, 16007, 15694, 14476, 12315, 9070, 4972, 1113, -943, -255, 2405, 5145, 7060, 8557, 9909, 11106, 12654, 14928,
17115, 17827, 16427, 13738, 11363, 9198, 5729, 1427, -505, 1914, 7028, 11445, 13383, 12871, 10521, 7103, 3206, -685, -3720, -4767, -2938, 1458, 6373, 9359,
9148, 5796, 470, -5035, -9161, -10791, -9093, -4114, 2147, 7105, 9905, 10617, 9286, 6542, 4056, 3698, 5998, 9490, 12354, 14098, 14834, 14062, 10361, 2438,
-7663, -15147, -18709, -19647, -18676, -15384, -8813, 1291, 12267, 19704, 22612, 22234, 19146, 13573, 7102, 3000, 2900, 5396, 8315, 10312, 10624, 9054, 5961, 1766,
-2587, -4947, -3494, 1372, 6751, 9329, 7311, 341, -9231, -16905, -20917, -22265, -21915, -20005, -16269, -11413, -7815, -6568, -5570, -2751, 852, 2661, 1704, -637,
-1548, 776, 5857, 11252, 14133, 14031, 12230, 9164, 4203, -3203, -11242, -14988, -12152, -4238, 4144, 10321, 13574, 14259, 13183, 10580, 7707, 5527, 3029, -1252,
-7736, -14461, -19542, -22326, -22668, -20928, -17816, -14026, -9506, -4225, -85, 1013, -709, -4331, -7860, -8358, -5281, -2017, -1421, -2096, -3151, -6101, -10261, -12578,
-11551, -7691, -2992, 236, 1458, 2391, 4615, 6755, 6756, 3913, -2496, -10453, -15545, -17115, -16394, -13960, -10460, -7156, -5683, -7465, -12056, -17242, -21025, -22611,
-21670, -17195, -9337, -3138, -2774, -6918, -11547, -13789, -13201, -10788, -8435, -7209, -6577, -5749, -4714, -4303, -4927, -5644, -5890, -6698, -8775, -11034, -12116, -11588,
-9289, -5434, -1876, -1047, -2779, -4482, -4688, -4322, -4517, -5398, -6944, -8991, -10863, -12005, -12328, -12315, -12620, -12891, -12236, -10667, -9145, -8622, -9440, -11353,
-13486, -14944, -15880, -17103, -18695, -19862, -19856, -18315, -15161, -10582, -5455, -1702, -548, -1335, -2894, -4029, -3354, -972, 679, -417, -3554, -6450, -7381, -6069,
-3315, -45, 2892, 4281, 3170, 442, -1097, 370, 3898, 7450, 9886, 10643, 9049, 5085, 673, -989, 1916, 8541, 15805, 20987, 23545, 23930, 22084, 16937,
7552, -3647, -12007, -15480, -13826, -6973, 1982, 7098, 6497, 2549, -466, -85, 2674, 5057, 5029, 1947, -3768, -9531, -11925, -9933, -4863, 478, 3406, 2683,
-988, -5206, -7994, -9419, -10805, -12583, -13431, -12103, -8673, -3711, 2738, 9968, 15755, 18542, 18235, 15076, 9695, 3971, 426, 281, 3091, 7915, 13907, 19724,
23897, 26081, 26778, 26364, 24668, 20984, 14939, 8007, 3064, 1985, 4102, 6692, 7519, 6638, 6135, 8274, 12924, 17604, 20528, 21664, 21423, 20075, 18117, 16405,
15537, 15607, 16484, 17842, 19149, 19965, 20019, 19059, 17029, 14470, 12498, 11699, 11185, 9700, 7246, 5082, 4489, 5933, 8643, 11076, 12477, 12998, 12730, 11559,
9744, 8109, 7037, 5898, 4270, 2725, 2042, 2206, 2627, 3253, 4356, 5389, 5414, 4189, 1922, -1239, -4793, -7586, -8631, -7617, -4691, -802, 2569, 5046,
7355, 9737, 11577, 12116, 10852, 7818, 3540, -1602, -6843, -10348, -10802, -8276, -4038, -369, 935, -322, -3267, -6360, -8100, -8082, -6825, -4828, -2076, 1494,
5524, 9752, 13949, 17141, 18187, 16498, 12065, 6248, 1944, 1575, 5513, 11753, 17279, 20581, 21460, 19562, 14120, 6148, -322, -3124, -3691, -4005, -4357, -4030,
-2762, -688, 1590, 2910, 2610, 861, -2005, -5606, -9076, -11308, -12014, -12024, -12443, -13542, -14389, -13633, -10638, -6306, -2586, -419, 243, -888, -3783, -7220,
-9896, -11417, -11930, -11728, -11270, -11228, -12223, -14170, -16130, -17177, -16969, -15669, -14073, -13548, -14627, -16116, -16637, -16109, -15543, -15629, -15666, -14714, -13013, -11654,
-11478, -12583, -14365, -16148, -17837, -19536, -20906, -21200, -19606, -15663, -10420, -6483, -5116, -5099, -4568, -2787, -310, 1677, 1851, -415, -4096, -7539, -10318, -12513,
-13661, -13609, -13030, -12207, -10325, -7134, -4306, -3672, -5101, -6939, -7516, -6285, -4044, -2426, -2724, -4790, -7265, -8851, -9102, -8174, -6448, -4851, -4707, -6037,
-7164, -6778, -5425, -5053, -7225, -11277, -14546, -15037, -12564, -8396, -4451, -1578, 579, 2310, 3405, 3835, 4284, 5580, 7399, 7468, 5192, 3592, 1404, -2549,
-2770, -1606, -3235, -1829, 3002, 5147, 3611, 581, -1546, -3797, -7546, -10007, -11410, -14452, -17146, -17429, -17251, -17236, -15499, -11504, -6226, -1886, 1416, 4632,
7225, 8573, 8080, 7090, 6702, 5714, 3599, 1729, 2744, 6030, 7404, 7397, 9257, 11459, 11227, 8313, 5318, 4331, 3491, 2791, 4482, 7705, 9814, 9071,
5669, 1810, -1139, -1747, 1152, 5024, 7596, 8094, 6183, 3909, 3034, 3377, 4375, 4909, 4395, 4005, 4578, 5387, 6042, 6975, 8183, 8681, 7578, 5626,
4499, 5174, 7888, 11257, 13110, 13100, 11898, 10004, 8477, 8461, 10169, 13251, 16538, 18345, 18174, 16223, 12246, 7100, 3020, 1193, 2301, 6078, 10418, 12922,
12251, 8173, 2043, -2821, -4267, -3286, -1690, 34, 2366, 5614, 9668, 14012, 17543, 19436, 19955, 20035, 20493, 21289, 21652, 21066, 19899, 19122, 19202, 19844,
20631, 21020, 20425, 18664, 16094, 13012, 9014, 4087, 29, -1004, 1237, 5069, 8547, 10816, 11684, 11267, 10281, 9664, 9608, 9505, 8759, 7064, 4255, 961,
-1094, -702, 1567, 3971, 5154, 4624, 2147, -1529, -4381, -5679, -6798, -8798, -10966, -11807, -10590, -8236, -6967, -8225, -11156, -13755, -15104, -15470, -14898, -12853,
-9437, -6220, -4925, -5635, -6619, -5939, -3372, -449, 1187, 1041, -29, -145, 2199, 6581, 10912, 13434, 13709, 11794, 8426, 7046, 9756, 12356, 11848, 10556,
10923, 10548, 5647, -2660, -8033, -7631, -4511, -2819, -3452, -5488, -6036, -3813, -1763, -1783, -2592, -2257, -672, -342, -2897, -4982, -3452, -1099, -357, -271,
-1941, -5925, -9081, -8947, -5156, 311, 4241, 6012, 7397, 9670, 11065, 8889, 3475, -1139, -3009, -4095, -5710, -6970, -7097, -6300, -4274, -1591, -189, -569,
-1377, -1717, -2030, -2410, -3387, -5458, -6578, -5692, -4947, -5863, -7213, -6875, -3983, 454, 4275, 5923, 5692, 4735, 3305, 637, -3456, -7252, -8581, -6862,
-3132, 335, 1725, 1395, 546, -931, -3867, -7888, -11322, -12611, -11236, -7981, -4694, -2985, -3405, -5274, -6902, -7311, -7470, -9047, -12133, -15100, -16575, -16304,
-14753, -12870, -11579, -11127, -11310, -11895, -12617, -13111, -12819, -11260, -8794, -6893, -7163, -10080, -14475, -18284, -20353, -20740, -19754, -17459, -13768, -9134, -5155, -3194,
-2869, -3216, -4132, -5740, -7322, -7728, -6717, -5340, -4578, -4257, -3689, -2507, -1092, -407, -1059, -2602, -3820, -3534, -1511, 919, 1907, 1218, 147, -560,
-1331, -2536, -4153, -6356, -9108, -11431, -12066, -10655, -8306, -6790, -6666, -7129, -7261, -6554, -4829, -2361, -252, 246, -1055, -3512, -6222, -8119, -8521, -7270,
-4215, 668, 6461, 11448, 14448, 15200, 13740, 10219, 5434, 1020, -1655, -2319, -1360, 346, 1551, 1338, -523, -3442, -6170, -7895, -8643, -8751, -9083, -10801,
-13722, -16160, -16749, -15094, -11566, -7531, -4776, -3818, -3791, -3913, -3683, -2141, 1395, 6327, 11098, 14030, 14298, 12492, 10408, 9427, 9035, 7842, 5048, 986,
-2732, -4526, -4141, -2099, 953, 4088, 5910, 5286, 2320, -1474, -3877, -3124, 1083, 6753, 10961, 12537, 11671, 8742, 4350, -445, -4391, -6486, -6068, -2737,
3099, 9291, 12992, 13248, 11350, 9807, 10298, 12420, 14425, 14561, 12025, 7950, 5395, 5999, 8516, 10848, 11590, 10398, 7926, 5300, 3066, 866, -1476, -3564,
-5087, -5698, -4959, -2623, 1567, 7001, 11247, 11962, 8499, 2191, -3802, -7246, -8267, -8115, -7595, -6716, -5478, -3953, -2196, -1033, -630, 1477, 6384, 10656,
11165, 8090, 4824, 4759, 6897, 9031, 10618, 11159, 10202, 8059, 5762, 5689, 9240, 13066, 12774, 8660, 6425, 8782, 12235, 14678, 14755, 11351, 6889, 4259,
3784, 6458, 10784, 12447, 10077, 5097, -643, -5191, -6348, -4060, -2182, -3645, -5170, -2576, 4143, 10766, 12648, 9125, 3142, -1342, -2950, -1629, 1961, 4907,
4937, 3570, 2176, -391, -3542, -4933, -4596, -3853, -2332, 154, 1988, 2779, 2836, 1285, -979, -1382, 590, 4173, 9013, 13906, 17244, 18157, 16373, 12593,
8367, 4776, 2439, 1130, 137, -544, -1538, -4016, -7204, -9581, -11101, -12178, -12833, -12508, -10389, -6897, -3695, -2024, -2262, -3549, -3877, -1953, 1437, 4018,
4416, 3442, 2486, 1884, 1481, 1544, 2819, 5720, 8968, 10138, 7846, 2830, -2288, -5403, -5993, -3935, 429, 5236, 7953, 7478, 4387, 419, -2176, -1665,
2216, 7393, 10633, 10528, 7861, 4253, 867, -1405, -1756, -236, 2306, 4799, 6274, 5983, 3437, -759, -4530, -6491, -7206, -7742, -8629, -10017, -11522, -12266,
-11826, -10697, -9509, -8182, -6314, -3873, -1180, 1300, 3213, 4495, 5166, 4968, 3494, 1198, -514, -1111, -1789, -3938, -7351, -10308, -11269, -10071, -8184, -7891,
-9993, -12775, -13983, -12954, -10447, -7499, -4974, -3813, -4245, -5152, -5374, -5348, -6670, -9824, -13477, -16201, -17391, -16994, -15421, -13305, -11048, -9015, -7746, -7319,
-6753, -4856, -2139, -723, -1671, -3959, -5926, -6823, -6764, -5875, -4555, -3831, -4473, -6411, -8958, -11146, -12218, -11914, -10100, -6636, -2263, 1473, 4075, 6162,
7897, 9057, 9835, 10238, 9652, 7689, 4791, 2066, 710, 916, 1478, 1395, 1279, 2338, 4725, 7489, 9571, 10183, 8655, 4980, 694, -2199, -3551, -4943,
-7435, -10134, -12376, -14181, -13561, -10003, -8163, -10735, -13658, -13596, -12418, -11537, -7997, -3971, -4779, -7303, -7701, -7630, -8597, -8326, -5233, -1538, 1063, 3344,
5416, 7164, 9096, 10044, 8076, 4212, 2090, 1607, 76, -1710, -2494, -2388, -1774, -1232, -964, -1524, -2153, -2694, -3650, -2839, -1001, -1310, -3004, -4447,
-5109, -3491, 1275, 5469, 5259, 2876, 1465, 340, -1548, -2394, 611, 6448, 11197, 13295, 13051, 11118, 8320, 5438, 3352, 2190, 1167, 164, -404, -492,
-359, 329, 2761, 6827, 10659, 12443, 11134, 7468, 3570, -310, -5015, -9438, -11114, -8444, -1997, 5769, 12263, 15772, 16468, 15662, 14452, 13338, 12574, 12066,
11006, 8414, 4136, -326, -2334, -299, 5081, 11212, 15524, 16878, 15148, 11111, 7009, 4957, 5473, 8031, 11035, 12727, 12709, 11373, 8567, 3888, -1515, -5213,
-6303, -5606, -4156, -2882, -2402, -2217, -1319, 373, 2729, 6265, 10429, 13369, 14222, 13390, 11543, 9350, 7615, 6851, 6525, 5454, 3126, 146, -2406, -3323,
-1622, 1951, 4781, 5592, 5825, 6856, 7975, 7944, 6374, 3407, -148, -2841, -3805, -3658, -3903, -4757, -4596, -2810, -1300, -1998, -4544, -7029, -7775, -6314,
-3526, -976, 214, -43, -611, 159, 2585, 5487, 7831, 9205, 9704, 9969, 10257, 9954, 8358, 5110, 471, -4119, -6434, -4749, 400, 4959, 5061, 933,
-3190, -3468, -675, 1867, 2359, 897, -1829, -4884, -7512, -8851, -8193, -6437, -5230, -4681, -4442, -5182, -7162, -9334, -10547, -10662, -10348, -10126, -10002, -9706,
-8526, -5765, -2148, 557, 2053, 3715, 5906, 6941, 5628, 3313, 2384, 2798, 2104, -963, -4898, -7226, -6470, -2851, 1491, 4021, 3790, 1248, -2421, -5777,
-8044, -9236, -9352, -8137, -5667, -3163, -2477, -4349, -7912, -11533, -13994, -14869, -14058, -11810, -9189, -7387, -6411, -5097, -2163, 2164, 5239, 4356, -400, -6096,
-10104, -11862, -11540, -9763, -7728, -6071, -4475, -2919, -2456, -4370, -8656, -13663, -17392, -18983, -18676, -17074, -14900, -13089, -12662, -14215, -17362, -20541, -22347, -22581,
-21711, -20375, -18967, -17347, -15589, -14534, -14612, -14302, -11939, -9462, -8888, -7442, -3506, 845, 2197, -626, -3829, -5257, -7600, -12053, -16433, -18465, -18075, -16805,
-14621, -9995, -5560, -4258, -3605, -3493, -5529, -7929, -9449, -10700, -12380, -13557, -12108, -8672, -6675, -7661, -10637, -13588, -14432, -12926, -11238, -10846, -11285, -12219,
-12280, -10589, -9256, -10227, -11571, -10367, -6922, -3441, -231, 2858, 4900, 5232, 3540, 55, -3155, -3976, -3223, -2804, -2957, -3632, -5287, -6637, -5349, -1607,
2043, 4729, 6993, 8222, 7905, 6610, 5718, 6568, 8383, 9946, 11620, 12955, 12374, 9104, 3357, -3163, -7760, -9188, -7626, -4055, -479, 1277, 1097, -11,
-1254, -2266, -2706, -2395, -1888, -2251, -3799, -5256, -4840, -2427, -184, -146, -1665, -2592, -2237, -1402, -238, 1810, 4546, 7208, 9313, 10509, 10813, 10908,
11037, 10670, 9976, 9923, 10728, 11610, 11762, 10775, 8715, 5928, 2486, -1674, -6073, -9419, -10202, -7748, -3140, 734, 1612, -430, -3755, -6530, -7930, -8014,
-6783, -4535, -2546, -1602, -651, 1759, 5559, 9182, 11200, 11306, 9707, 6341, 1396, -3497, -6101, -5355, -1642, 3185, 6830, 8195, 7033, 3541, -724, -3307,
-3184, -1122, 1846, 5195, 8304, 10103, 9908, 8296, 6676, 5841, 5534, 5076, 4080, 3155, 3348, 4729, 6384, 7376, 7329, 6773, 6644, 7131, 7801, 8196,
7782, 6385, 4536, 2981, 2463, 3366, 4855, 5512, 5217, 5087, 5502, 5550, 4629, 3633, 3995, 5731, 7305, 7337, 5525, 2518, -267, -1779, -2333, -2306,
-1011, 1902, 5344, 7802, 8583, 8144, 7678, 7868, 8051, 6970, 4180, 625, -2127, -2914, -1368, 1625, 4098, 4547, 2944, 500, -1270, -1479, -245, 1199,
1664, 1510, 1798, 2530, 2871, 2618, 2707, 4043, 6324, 8318, 8684, 6624, 2603, -1432, -3393, -2673, -498, 1213, 1534, 670, -1034, -3618, -6706, -9043,
-9792, -9540, -9621, -10648, -12056, -13070, -13263, -12274, -9646, -5351, -515, 3174, 5090, 5765, 5593, 4205, 1504, -1146, -1890, -196, 3258, 7454, 11216, 13586,
14023, 12064, 7435, 726, -6401, -11840, -14296, -13744, -10858, -6712, -2614, -116, 58, -790, -1138, -1687, -4021, -7558, -10259, -11405, -11509, -11120, -10614, -10266,
-10149, -10026, -9426, -7859, -5222, -2203, 155, 1464, 2022, 2325, 3130, 5191, 8407, 11843, 14571, 15787, 14645, 10450, 3342, -4777, -11069, -13699, -11872, -5439,
2827, 8081, 8822, 6523, 3642, 2054, 1811, 1674, 360, -2441, -5853, -8669, -10269, -10538, -9458, -7606, -6383, -6263, -5661, -3058, 230, 1032, -1874, -7131,
-12467, -15797, -16134, -13711, -9606, -5273, -1531, 1867, 5072, 6754, 4851, -1361, -9386, -15256, -17603, -16913, -14085, -10585, -7805, -6348, -6044, -5974, -5189, -3284,
-609, 1511, 1525, -1056, -4575, -6199, -4880, -1739, 1652, 4194, 5078, 3772, 219, -4806, -9553, -12116, -11584, -8485, -4761, -2221, -938, -51, 1194, 3224,
5417, 6167, 4244, -445, -6175, -10060, -10589, -7982, -3586, 268, 1772, 1055, -1076, -4673, -9233, -12481, -12761, -10572, -7412, -3816, 370, 4246, 6343, 6069,
4250, 2439, 908, -1558, -5088, -7539, -6677, -2396, 2651, 5284, 4839, 2747, 490, -1698, -4039, -6148, -7097, -5914, -2394, 2032, 4865, 4994, 3463, 1427,
-973, -3045, -3318, -1651, 103, 418, -241, -1060, -1974, -2719, -2818, -2233, -1471, -1306, -1902, -2598, -2874, -2637, -1598, 907, 5179, 10097, 13602, 14407,
12607, 9995, 9127, 10122, 10856, 9929, 7204, 3320, -224, -2319, -3405, -4040, -3588, -1248, 2432, 5714, 7285, 7085, 5974, 4828, 3479, 1133, -2175, -5442,
-7766, -8732, -8031, -5759, -3021, -964, 638, 2762, 5371, 7159, 7388, 6608, 5830, 5972, 7127, 8337, 8719, 8336, 7773, 7056, 5921, 4747, 3787, 2643,
1337, 545, 682, 1510, 2638, 3624, 3772, 2942, 2048, 1845, 2087, 2474, 3417, 5170, 6850, 7237, 6097, 4178, 2514, 1712, 1474, 1224, 1287, 2446,
4404, 6127, 7394, 8621, 9584, 9253, 6979, 3754, 1680, 1591, 2445, 2898, 2607, 2398, 3315, 5348, 7464, 8759, 9262, 9900, 11453, 13273, 13784, 12218,
9224, 6177, 4087, 3455, 4364, 5935, 6594, 5610, 3981, 3391, 4403, 6436, 8684, 10134, 9813, 7612, 4371, 1141, -1435, -3211, -4369, -5161, -5830, -6620,
-7583, -8342, -8340, -7492, -6313, -5401, -4874, -4298, -3216, -1655, -104, 1017, 1851, 2532, 2443, 769, -2359, -5522, -7192, -7100, -5783, -3486, -641, 1316,
1210, -513, -2396, -3342, -3106, -1910, -91, 1738, 2590, 1730, -567, -3059, -4598, -5002, -4791, -4426, -3968, -3324, -2636, -2168, -1610, -196, 2079, 4261,
5439, 4911, 2192, -1933, -5201, -5905, -4235, -1951, -1207, -2919, -6304, -9694, -11726, -11831, -9963, -6458, -2505, -20, -148, -2433, -5290, -6917, -6518, -5015,
-4127, -4500, -5432, -6249, -7013, -7980, -8976, -9350, -8705, -7576, -6678, -5883, -4654, -2664, 51, 2848, 4963, 6348, 7232, 7275, 5742, 2313, -2419, -7172,
-10690, -12349, -12303, -11159, -9500, -7672, -6001, -5031, -5115, -5767, -5983, -5198, -3801, -2745, -2311, -1606, 263, 3069, 5629, 6820, 6336, 4521, 1848, -979,
-2861, -3267, -2945, -3188, -4623, -6753, -8593, -9418, -9049, -8060, -7448, -7690, -8652, -9987, -11143, -11781, -12011, -11890, -11294, -10414, -9875, -10029, -10529, -10667,
-9559, -6370, -1301, 3710, 6658, 7238, 6085, 3736, 722, -2099, -3738, -3489, -1528, 819, 2182, 2321, 1810, 1174, 634, 508, 1306, 3086, 5220, 6955,
7778, 7455, 6117, 4172, 2203, 893, 605, 902, 687, -838, -3218, -4935, -4915, -3542, -1991, -983, -697, -1372, -3083, -5188, -6694, -6799, -4951, -1360,
2300, 3798, 2410, -447, -2424, -2144, -53, 2120, 2934, 2324, 1114, -112, -1295, -2339, -2837, -2413, -1462, -1160, -1991, -2938, -2955, -2388, -2066, -1810,
-708, 1634, 4584, 6875, 7394, 5849, 3047, 285, -1754, -2977, -3316, -2843, -2070, -1585, -1212, -263, 1306, 2966, 4319, 4908, 4333, 3110, 2569, 3664,
6296, 9515, 12079, 13183, 12893, 11857, 10572, 9315, 8660, 9010, 9947, 10724, 10637, 9131, 6489, 4207, 3701, 4700, 5650, 5463, 4173, 2283, 287, -1242,
-1710, -976, 708, 3273, 6351, 8845, 9822, 8931, 6171, 2202, -1397, -3025, -2375, -652, 875, 2173, 3530, 4277, 3424, 1000, -1876, -3965, -4513, -3498,
-1752, -376, -21, -690, -1717, -2128, -1549, -274, 1344, 2125, 711, -894, 928, 4528, 4668, 1449, -943, -1586, -1578, -2502, -5687, -7737, -5245, -829,
2355, 5732, 9642, 11385, 9051, 4885, 3856, 6581, 10999, 14164, 13587, 11289, 10653, 9630, 5763, 1994, 1292, 1457, -539, -3416, -4128, -4175, -5934, -6707,
-4055, -568, 832, 234, -2512, -5946, -7851, -9205, -9613, -7507, -4649, -3295, -2670, -1415, 474, 2472, 3705, 4315, 5514, 7106, 8396, 9666, 11684, 14194,
15753, 15807, 14465, 11570, 7815, 4815, 3208, 2645, 2840, 3589, 4369, 4783, 4747, 3902, 2612, 1995, 1433, -828, -4639, -8016, -9680, -9967, -9739, -9037,
-6931, -2587, 2941, 6709, 7376, 5988, 4408, 3922, 3861, 2961, 1425, 109, -281, 298, 588, -297, -1489, -1715, -696, 854, 1957, 1931, 370, -2498,
-5230, -6027, -4640, -2555, -1003, -79, 403, 500, -233, -2066, -3991, -4831, -4468, -3261, -1560, 214, 1662, 2463, 2448, 1705, 243, -2090, -5142, -8516,
-11745, -13992, -14305, -12190, -7834, -2316, 2522, 5362, 6383, 6239, 5074, 3155, 1222, -304, -1482, -2042, -1187, 1400, 4742, 7384, 8777, 9038, 7775, 4034,
-1992, -7788, -10806, -10507, -7780, -4346, -1498, 745, 2556, 3446, 2657, -57, -3925, -7438, -9462, -10065, -10332, -11299, -12874, -13814, -12725, -9124, -4086, 379,
2767, 2428, -196, -3542, -5863, -6126, -4295, -1170, 2332, 5471, 7159, 6417, 3619, 533, -1352, -2074, -2577, -3411, -4259, -4655, -4357, -2887, 253, 4339,
7442, 8487, 8312, 8586, 9911, 11356, 11788, 10854, 8993, 7010, 5117, 2485, -1674, -6648, -10398, -11489, -10007, -7252, -4869, -3651, -3658, -4800, -6501, -7722,
-7790, -6726, -4737, -1874, 1477, 4317, 5732, 5604, 4683, 4003, 4049, 4566, 5084, 5562, 6161, 6478, 5788, 3856, 1062, -1793, -3573, -3560, -2106, 65,
2412, 4277, 4991, 4331, 2864, 1665, 1408, 2274, 4140, 5913, 6064, 4434, 2291, 974, 1328, 3348, 5640, 6485, 5696, 4624, 4169, 3570, 1838, -617,
-2299, -1904, 605, 3653, 5466, 5756, 5198, 4154, 2623, 816, -896, -2282, -3225, -3710, -3841, -3804, -3577, -2617, -485, 2187, 4154, 4760, 3978, 2042,
-252, -1724, -1689, -218, 2005, 3733, 4143, 3892, 4305, 5859, 7769, 8584, 7299, 4074, 98, -3043, -4092, -2480, 1533, 6672, 11013, 12909, 11598, 7635,
3116, 97, -1087, -1198, -630, 698, 2555, 4609, 6523, 7300, 5930, 2825, -495, -3322, -5917, -8177, -9533, -9647, -8480, -6131, -3023, -18, 1749, 1571,
114, -981, -601, 1369, 4307, 7192, 9366, 10967, 12344, 13239, 12917, 11089, 8423, 6207, 5633, 7165, 9807, 11461, 10964, 9066, 7118, 5105, 1974, -2239,
-5648, -6453, -4650, -1754, 225, 288, -612, -519, 1167, 3088, 4010, 3844, 2624, 516, -1465, -2453, -2855, -3411, -3881, -3425, -1862, -60, 1171, 2159,
3466, 4682, 5317, 5719, 6024, 5891, 5771, 6704, 8870, 11329, 12797, 12335, 9735, 5514, 462, -4295, -7150, -7093, -4555, -1320, 703, 1012, 452, 231,
1175, 3294, 5753, 7704, 8882, 9170, 8447, 6844, 4765, 3058, 2595, 3278, 4400, 5484, 6043, 5446, 3538, 859, -2096, -5448, -9243, -12602, -14243, -13444,
-10205, -5586, -1835, -596, -1817, -4723, -8322, -11404, -13144, -13277, -11774, -9002, -5871, -3223, -1152, 690, 1882, 1161, -1802, -5411, -7443, -6690, -3246, 1266,
4241, 4093, 1185, -2449, -4483, -4323, -3398, -3624, -5651, -8443, -10535, -11447, -11341, -10155, -7913, -5395, -3797, -3659, -4223, -4191, -3272, -2368, -2301, -3058,
-4012, -4553, -4570, -4246, -3681, -3006, -2396, -1695, -633, 523, 1020, 232, -1716, -3726, -4781, -5252, -6358, -8241, -9393, -8246, -4620, -254, 2236, 1812,
-503, -3079, -5018, -6285, -7039, -7236, -7055, -7107, -7684, -8217, -7696, -5507, -2039, 1400, 3462, 3476, 1717, -632, -2120, -1955, -554, 876, 1537, 1473,
939, -43, -1530, -3677, -6809, -10635, -13795, -14908, -13360, -9347, -4317, -563, 505, -1041, -4109, -7263, -9421, -10062, -9399, -8373, -7842, -7719, -7139, -5529,
-3417, -1901, -1579, -2260, -3485, -4492, -4414, -4071, -5354, -8066, -10334, -11445, -11412, -10595, -9411, -7196, -4781, -3336, -1382, 2162, 5701, 6539, 5067, 3754,
2405, 611, 91, 1121, 1810, 867, -221, 87, 73, -1094, -1869, -2345, -2723, -2098, -1249, -279, 1825, 3586, 3641, 2293, -521, -4090, -6754, -8535,
-9903, -9741, -7189, -2957, 880, 2850, 3852, 4961, 5548, 4816, 2786, -484, -3881, -5485, -5194, -3966, -2824, -2556, -2740, -2784, -2769, -2603, -2307, -2252,
-1827, -805, -507, -832, -303, 1120, 2477, 3168, 2859, 1249, -1088, -2234, -1329, 235, 1496, 2644, 3302, 2939, 1631, -126, -1439, -1371, 517, 3933,
7773, 10627, 11918, 12106, 11783, 11219, 10583, 9781, 8291, 6015, 3715, 2286, 2234, 3266, 4642, 5545, 5148, 3567, 1890, 1176, 1732, 3095, 4585, 5389,
4546, 1833, -1731, -4726, -6183, -6000, -4941, -4059, -3947, -4305, -4448, -4187, -3997, -4080, -3837, -2866, -1668, -904, -303, 911, 2721, 4505, 5956, 6765,
6430, 4781, 2071, -819, -2474, -2274, -1014, 87, 534, 716, 990, 890, -546, -3700, -7410, -9863, -10400, -9356, -7123, -4231, -1696, -354, 4, 391,
1568, 3218, 3974, 2619, -491, -3463, -4717, -4176, -2456, 71, 2787, 4382, 4112, 3020, 3188, 5319, 7827, 8997, 8647, 7730, 7618, 8884, 10315, 10116,
7647, 3830, 479, -1016, -365, 1672, 3790, 4995, 4988, 4022, 2606, 1200, 147, -378, -573, -733, -905, -901, -303, 1161, 3132, 5130, 7154, 9049,
10154, 10280, 10049, 9500, 7763, 4538, 685, -3350, -7239, -8586, -6933, -3939, 414, 4224, 4907, 2637, -519, -1204, 634, 2006, 2240, 2391, 1703, -262,
-1731, -2293, -2547, -1955, -1017, -1351, -2128, -1263, -72, -1120, -3562, -5547, -6660, -6309, -4595, -2433, 175, 3030, 4782, 4526, 3116, 2175, 2397, 3232,
3248, 1844, 592, 205, -340, -371, 612, 1071, 812, 476, -206, -776, -486, 766, 2644, 3912, 3636, 2393, 1238, 1050, 1948, 2846, 2556, 540,
-2319, -4550, -5735, -6078, -5919, -5780, -6466, -7812, -8587, -8548, -8521, -8948, -9236, -8711, -7384, -5604, -3434, -884, 1033, 1445, 691, -411, -979, -1064,
-1193, -881, 268, 1618, 2239, 1756, 241, -2445, -5674, -7789, -8201, -7566, -6293, -4368, -2125, -231, 1203, 2738, 4656, 6447, 7376, 7125, 5903, 4275,
2798, 1791, 1366, 1453, 1937, 2798, 3780, 4232, 3544, 1569, -1315, -4350, -6331, -6092, -3767, -1027, 726, 1655, 2251, 2334, 1931, 1869, 2614, 3487,
3376, 1701, -829, -2635, -2804, -1574, 144, 1279, 1323, 718, 204, -114, -579, -1195, -1775, -2335, -2670, -2257, -1085, 81, 625, 797, 1282, 2196,
2888, 2819, 2196, 1625, 1170, 201, -1482, -3224, -4112, -3605, -2361, -2115, -3847, -6521, -7922, -6657, -3318, 71, 2172, 3185, 3686, 3734, 2918, 802,
-2398, -5475, -7059, -6581, -4094, -79, 4132, 6859, 7289, 5799, 3928, 3588, 5194, 7287, 8108, 6812, 3726, 132, -2711, -4291, -4480, -3522, -2424, -2192,
-2692, -2677, -858, 2695, 6347, 8775, 10071, 10611, 10277, 8936, 6929, 4979, 3548, 2538, 1890, 1736, 1993, 2358, 2458, 2197, 2010, 2172, 2230, 1648,
457, -762, -1053, 344, 3258, 6548, 8539, 8065, 5585, 2737, 237, -2416, -4858, -5655, -4205, -1547, 433, 340, -2085, -5851, -9169, -10721, -10085, -7370,
-3630, -825, 54, -252, -420, 633, 3093, 5690, 7008, 6904, 5853, 4368, 3255, 2894, 2797, 2575, 2163, 1294, -184, -1731, -2287, -1002, 2076, 5818,
8718, 9757, 8557, 5763, 2979, 921, -1227, -3813, -5745, -5889, -4269, -1946, -224, 163, -656, -1565, -926, 1946, 5787, 8213, 7839, 5380, 3082, 2468,
2897, 2991, 2546, 2213, 2215, 2456, 3037, 4062, 5275, 6063, 5838, 4362, 2201, 1008, 2260, 5494, 8480, 9259, 7421, 4344, 2354, 2340, 3526, 5306,
7191, 8004, 6898, 4607, 2967, 3010, 4232, 5578, 6191, 5388, 2879, -316, -2005, -994, 1664, 3896, 4509, 3503, 1581, -426, -2420, -4747, -7003, -8065,
-7267, -5070, -2596, -594, 879, 1954, 2669, 3048, 2972, 2222, 680, -1739, -4738, -7353, -8608, -7932, -5407, -2304, -214, 541, 549, 420, 781, 1923,
3391, 4541, 5093, 5079, 4661, 4053, 3535, 3289, 3184, 2820, 1913, 557, -1037, -2584, -3680, -4092, -3857, -3506, -4000, -5741, -7896, -9028, -8106, -4983,
-693, 2789, 3931, 2570, -353, -3529, -6006, -7389, -7475, -6054, -3183, 391, 3151, 3602, 1471, -1869, -4520, -5593, -5146, -3784, -2535, -1904, -1323, -25,
1869, 3278, 3357, 2357, 1011, -193, -955, -1087, -854, -1078, -2339, -4263, -6179, -7859, -9196, -9786, -9197, -7648, -6201, -5786, -6289, -6773, -6137, -3885,
-755, 1727, 2666, 2234, 892, -1076, -3244, -4950, -5674, -5168, -3392, -692, 1963, 3515, 3813, 3401, 2557, 1341, 45, -1058, -1686, -1270, 533, 3212,
5534, 6404, 5584, 3468, 592, -2343, -4605, -5850, -6157, -5737, -4699, -3217, -1659, -195, 1304, 2801, 3979, 4455, 3801, 1912, -363, -1685, -1502, -526,
-226, -1787, -5099, -8715, -11202, -12168, -11846, -10663, -9296, -8212, -7167, -5947, -5055, -4910, -4796, -3316, -195, 2834, 4120, 4082, 4089, 4731, 5320, 4364,
1096, -3071, -5385, -4837, -3367, -3107, -3669, -3679, -2317, 610, 3810, 5526, 5756, 5389, 4453, 2839, 1645, 1954, 3390, 4854, 5371, 4454, 2358, 136,
-760, 340, 2947, 6215, 9132, 10343, 8846, 4692, -683, -5080, -6925, -5885, -2980, -321, 447, -421, -1322, -1190, -400, 157, 47, -492, -754, -143,
1559, 4137, 6592, 7565, 6376, 3306, -699, -4121, -5455, -4648, -3169, -2561, -3242, -4152, -3831, -2370, -1278, -1407, -2341, -3315, -3545, -2478, -618, 598,
357, -760, -1270, 239, 3879, 8030, 10684, 11116, 9903, 8129, 6469, 4646, 2071, -1048, -3574, -4344, -2832, 698, 5050, 8566, 10225, 9974, 8450, 6451,
4360, 2315, 687, 154, 1191, 3305, 5395, 6702, 6761, 5616, 4267, 3409, 2460, 949, -772, -2454, -4172, -5851, -7234, -8232, -8919, -9199, -8751, -7124,
-4046, -273, 2491, 3437, 3200, 2682, 2496, 3027, 4266, 5504, 5642, 4209, 1621, -1319, -3451, -3794, -2479, -433, 1342, 2100, 1855, 1555, 2112, 3042,
2808, 982, -922, -1333, -209, 1424, 2480, 2421, 1375, 15, -811, -736, 17, 962, 1718, 2213, 2311, 1793, 899, 30, -977, -2595, -4860, -7162,
-8511, -7972, -5191, -960, 3098, 5839, 6984, 6796, 6019, 5508, 5470, 5153, 3620, 849, -2167, -4257, -4931, -4507, -3461, -2008, -245, 1676, 3422, 4331,
3612, 1061, -2293, -4614, -4840, -3587, -2203, -1431, -1485, -2449, -3866, -4500, -3150, 214, 4155, 6701, 6782, 4833, 1975, -1192, -4262, -6448, -7039, -5774,
-3041, 259, 3194, 4931, 5010, 3568, 1283, -641, -1160, -448, 404, 850, 1279, 2096, 3000, 3281, 2604, 1107, -1071, -3688, -6101, -7768, -8633, -8854,
-8557, -7508, -4963, -649, 4036, 6901, 7089, 5376, 3213, 1972, 2377, 3721, 4223, 2953, 632, -1693, -3635, -5215, -6678, -8016, -8745, -8444, -6904, -4136,
-798, 1714, 2293, 1451, 857, 1067, 961, -526, -3266, -6145, -8112, -8920, -8630, -7207, -4994, -2716, -887, 156, 23, -1446, -3783, -6011, -7301, -7426,
-6587, -5064, -3186, -1302, 141, 497, -675, -3281, -6617, -9536, -11259, -11594, -10404, -7649, -4138, -1458, -248, 306, 927, 1575, 1907, 1617, 890, 385,
330, 218, -270, -732, -625, -59, 185, -403, -1405, -2004, -1852, -1212, -338, 963, 2507, 3177, 2348, 854, -242, -692, -633, -234, 104, -187,
-1039, -1550, -1150, -57, 1059, 1007, -1194, -4767, -7449, -7609, -5248, -1888, 270, -89, -2721, -6075, -8474, -9221, -8453, -6714, -4744, -3104, -1844, -731,
185, 330, -767, -2577, -4106, -5056, -5353, -4361, -1819, 1194, 3447, 4907, 6207, 7620, 8792, 9211, 8890, 8450, 8234, 7613, 5710, 2478, -1089, -3567,
-3848, -1773, 1408, 3565, 3383, 1408, -847, -2562, -3712, -4129, -3202, -528, 3104, 6028, 7150, 6484, 4726, 2519, 230, -1169, -268, 2913, 6439, 8453,
8608, 7680, 6796, 6656, 7035, 6981, 5833, 3913, 1745, -532, -2491, -3301, -2596, -1047, 55, -6, -817, -1750, -3033, -5160, -7677, -9347, -9160, -6921,
-3333, 676, 4750, 8583, 11267, 12006, 10908, 8759, 6497, 4963, 4608, 5194, 5955, 5997, 4653, 1825, -1638, -4406, -5603, -4965, -2712, 518, 3745, 5597,
4967, 2098, -1177, -2845, -2427, -943, 286, 552, -5, -594, -175, 1818, 4550, 6225, 5979, 4487, 2846, 1334, -360, -1877, -1988, 277, 4587, 9149,
12067, 12755, 11748, 9936, 7790, 5303, 2949, 1711, 1801, 2326, 2197, 965, -925, -2216, -1503, 1575, 5703, 8596, 8657, 5969, 2361, -102, -1005, -1059,
-1053, -1748, -3417, -5279, -6296, -6087, -4703, -2737, -1481, -1644, -2575, -3356, -3724, -3798, -3437, -2139, 45, 1959, 2725, 2951, 3653, 4572, 4539, 3301,
1940, 1507, 2019, 2598, 2142, 311, -1938, -3334, -3698, -3755, -3985, -4216, -4132, -3677, -2875, -1928, -1403, -1541, -1712, -1205, -230, 495, 797, 1001,
1236, 1399, 1495, 1486, 934, -678, -3047, -5036, -5963, -6118, -5955, -5630, -5123, -4293, -3080, -1906, -1641, -2815, -5026, -7340, -8999, -9586, -8877, -6878,
-3916, -592, 2199, 3537, 3085, 1298, -943, -2703, -3042, -1477, 1351, 4091, 5970, 7098, 7816, 8346, 8740, 8777, 8038, 6239, 3721, 1340, -456, -1783,
-2527, -2305, -1191, 93, 755, 423, -872, -2751, -4357, -4706, -3498, -1465, 214, 1060, 1452, 1583, 1090, -137, -1552, -2512, -2747, -2385, -1903, -1873,
-2706, -4517, -6882, -8848, -9436, -8155, -5288, -1862, 786, 1835, 1515, 628, -504, -2176, -4278, -6066, -7147, -7714, -7926, -7747, -7156, -6357, -5888, -6183,
-7005, -7500, -6898, -5284, -3503, -2336, -2080, -2624, -3685, -5002, -6332, -7395, -7895, -7568, -6214, -4029, -1889, -663, -463, -849, -1160, -998, -578, -188,
182, 592, 1090, 1706, 2363, 2813, 2574, 1259, -856, -2765, -3252, -1752, 848, 2726, 2963, 2122, 1427, 1880, 3528, 5393, 6411, 6182, 4948, 3339,
2016, 1226, 732, 358, 252, 297, 44, -572, -1313, -2337, -4050, -6342, -8623, -10484, -11727, -12070, -11443, -10329, -9427, -9002, -8777, -8428, -7879, -7038,
-5617, -3425, -728, 1713, 3028, 2938, 2216, 1916, 2045, 1833, 1107, 615, 1071, 2219, 2913, 2188, 117, -2168, -3174, -2077, 546, 2967, 3682, 2515,
569, -918, -1673, -1844, -1171, 517, 2494, 3501, 2966, 1330, -584, -1962, -2119, -965, 726, 1864, 2050, 1794, 1466, 693, -819, -2652, -4177, -5142,
-5455, -4960, -3832, -2720, -2104, -1988, -2204, -2282, -1330, 1147, 4312, 6592, 7338, 6977, 6130, 5138, 4100, 3233, 2973, 3470, 4097, 3751, 2058, 43,
-988, -849, -159, 626, 1198, 1092, 332, -284, -104, 666, 1165, 691, -348, -647, 615, 2909, 4810, 5015, 3056, -151, -2684, -3393, -2685, -1699,
-1284, -1557, -2244, -3231, -4267, -4742, -4635, -4777, -5469, -5732, -4535, -2106, 304, 1613, 1376, -379, -2999, -5329, -6554, -6594, -5665, -4049, -2344, -1117,
-370, -40, -232, -528, -54, 1404, 2827, 2926, 1701, 488, 199, 420, 341, -441, -1787, -3068, -3641, -3203, -1618, 1055, 4147, 6469, 7086, 5973,
4072, 2455, 1207, -287, -2079, -3570, -4318, -4103, -2830, -760, 1376, 2482, 1742, -545, -3072, -4651, -5117, -5149, -5493, -6392, -7455, -7963, -7517, -6354,
-4897, -3194, -1252, 423, 1408, 2047, 2801, 3531, 3850, 3705, 3447, 3268, 2915, 2253, 1555, 977, 165, -1362, -3509, -5456, -6351, -6039, -4891, -3251,
-1549, -521, -565, -1201, -1556, -1212, -421, 267, 450, 211, 135, 858, 2381, 3880, 4678, 5140, 6081, 7519, 8357, 7469, 4702, 854, -2926, -5852,
-7703, -8465, -8151, -6988, -5315, -3177, -439, 2420, 4180, 4138, 2770, 833, -1427, -3894, -6058, -7157, -6785, -5362, -3892, -3255, -3442, -3469, -2458, -537,
1264, 1743, 789, -311, -440, 301, 1152, 1278, 181, -1779, -3648, -4800, -5171, -4862, -4075, -3329, -3242, -3625, -3495, -2504, -1253, -354, -314, -1601,
-3924, -6004, -6522, -5120, -2692, -867, -537, -1113, -1519, -1293, -812, -1004, -2509, -4801, -6556, -6903, -5811, -3606, -648, 2523, 4894, 5398, 3881, 1475,
-131, -106, 1121, 2621, 3633, 3579, 2170, -127, -2391, -4078, -5199, -5980, -6463, -6510, -6079, -5131, -3759, -2644, -2602, -3717, -5276, -6147, -5528, -3802,
-2155, -1268, -993, -778, 5, 1466, 2795, 2853, 1275, -939, -2189, -1738, 5, 2205, 4089, 5157, 5422, 5337, 5375, 5413, 4137, 429, -3802, -4982,
-3588, -4040, -7093, -9099, -8472, -5212, -698, 696, -1763, -3082, -1075, 718, -520, -2902, -3862, -4031, -4790, -5887, -7098, -8071, -7422, -5332, -3741, -3267,
-3182, -2460, -174, 2731, 4301, 4651, 5163, 6267, 7216, 7394, 7253, 7060, 6465, 5484, 4434, 3902, 4130, 4121, 2791, 563, -541, 913, 3413, 4363,
3798, 3453, 3675, 3696, 3061, 1440, -1043, -3388, -4738, -5049, -4704, -4306, -4453, -4685, -3565, -900, 1486, 2437, 2739, 2593, 1043, -1314, -2642, -2567,
-1472, 920, 4362, 7449, 9209, 9653, 8841, 6968, 4654, 2388, 642, 94, 977, 2619, 4036, 4754, 4637, 3749, 2700, 2175, 2096, 1950, 1521, 968,
629, 668, 674, -48, -1481, -2588, -2588, -1931, -1547, -1893, -3062, -4962, -6954, -7773, -6429, -3336, -147, 1936, 2985, 3592, 4332, 5610, 7138, 8051,
7895, 6987, 5979, 5279, 4589, 3297, 1724, 1074, 1625, 2320, 2328, 1886, 1733, 2064, 2239, 1774, 1205, 1414, 2265, 2558, 1464, -516, -2275, -3302,
-3635, -3423, -2936, -2091, -434, 1658, 2967, 3121, 2956, 3009, 2726, 1366, -857, -2627, -2682, -871, 2117, 5384, 7937, 9032, 8470, 6346, 3249, 778,
587, 2725, 5601, 7504, 7733, 6393, 3765, 194, -3526, -6320, -7538, -7015, -4731, -1078, 2432, 3998, 3540, 2643, 2373, 2375, 2013, 963, -825, -2821,
-3890, -3013, -353, 2520, 4282, 5130, 5789, 6165, 5523, 3734, 1844, 971, 1104, 1671, 2514, 3448, 3631, 2418, 470, -808, -747, 147, 785, 240,
-1609, -4014, -5926, -6432, -5143, -2775, -755, 107, 180, 367, 1130, 2343, 3826, 5375, 6602, 7137, 6814, 5577, 3499, 1099, -580, -595, 1212, 3853,
5553, 5037, 2391, -1059, -3460, -3490, -1197, 1980, 4279, 4948, 4453, 3675, 3069, 2574, 1997, 1146, -89, -1617, -3249, -4877, -6497, -8024, -9160, -9405,
-8199, -5561, -2695, -995, -381, 130, 645, 473, -211, -277, 605, 1544, 1977, 2286, 2664, 2619, 1780, 281, -1447, -2587, -2228, -375, 1773, 3104,
3559, 3449, 2810, 1698, 416, -724, -1796, -3190, -5060, -7104, -8688, -8969, -7299, -3886, -59, 2613, 3641, 3701, 3703, 3751, 3054, 882, -2587, -6301,
-9163, -10643, -10549, -8907, -6328, -3750, -1512, 593, 2553, 3862, 3820, 2148, -519, -2875, -3928, -3587, -2357, -1032, -538, -1279, -2537, -3052, -2416, -1503,
-1437, -2434, -3892, -5205, -5944, -5832, -4996, -3765, -2314, -909, -83, -83, -554, -1010, -1139, -733, 250, 1530, 2512, 2822, 2660, 2326, 1809, 1040,
159, -596, -1030, -933, -276, 339, -45, -1742, -4002, -5630, -5794, -4415, -2307, -596, 375, 960, 1190, 690, -548, -2075, -3604, -4959, -5612, -5200,
-4328, -4133, -4949, -5840, -5555, -3716, -983, 1519, 2758, 2376, 1104, 184, 275, 972, 1377, 1240, 1117, 1246, 1107, 447, 14, 687, 2184, 3288,
3348, 2798, 2196, 1638, 989, -17, -1443, -2552, -2435, -1290, -71, 858, 1556, 1644, 770, -780, -2354, -3169, -2691, -1404, -343, 330, 1235, 2664,
4337, 5527, 5384, 3914, 2275, 1441, 1235, 925, 170, -689, -1287, -1787, -2487, -3274, -3651, -3386, -2946, -2988, -3694, -4789, -5919, -6814, -7143, -6645,
-5479, -3960, -2045, 298, 2491, 3740, 3921, 3784, 4050, 4470, 4369, 3755, 3266, 3241, 3440, 3544, 3514, 3535, 3746, 3962, 3644, 2286, -65, -2654,
-4303, -4222, -2585, -180, 2317, 4607, 6617, 8231, 9224, 9348, 8296, 5851, 2519, -219, -1072, -262, 717, 408, -1514, -3878, -4848, -3611, -1059, 1117,
2163, 2806, 3903, 5360, 6516, 6620, 5179, 2658, 318, -1247, -2627, -4360, -6067, -7164, -7796, -8404, -8759, -8193, -6310, -3058, 1078, 4780, 6915, 7622,
7818, 7926, 7450, 6006, 4031, 2098, 495, -552, -1111, -1549, -2144, -2759, -2935, -2415, -1204, 659, 2842, 4668, 5743, 6152, 5940, 4999, 3171, 163,
-3833, -7402, -8884, -7971, -5803, -3909, -3048, -3110, -3559, -3847, -3795, -3484, -2658, -909, 1273, 2716, 2902, 2073, 549, -1174, -2359, -2754, -2955, -3627,
-4719, -5695, -6229, -6251, -5563, -4018, -1862, 528, 2833, 4605, 5440, 5174, 3726, 1259, -1601, -4227, -6366, -7773, -7852, -6056, -2617, 1239, 3957, 4704,
3676, 1728, -27, -837, -826, -684, -847, -1124, -997, -72, 1674, 3813, 5547, 6097, 5193, 3134, 572, -1843, -3930, -5699, -6759, -6734, -5934, -5012,
-4175, -3272, -2492, -2168, -2104, -1898, -1532, -1304, -1565, -2424, -3495, -4127, -4020, -3427, -2861, -2978, -4248, -6191, -7628, -7958, -7561, -7104, -6802, -6344,
-5478, -4408, -3359, -2255, -1099, -252, -181, -889, -1834, -2685, -3510, -4063, -3805, -2703, -1386, -576, -522, -777, -602, 304, 1586, 2612, 2995, 2850,
2717, 3000, 3364, 3140, 2201, 1140, 733, 1216, 1997, 2279, 1970, 1625, 1667, 2061, 2568, 3054, 3511, 3824, 3778, 3181, 1759, -641, -3487, -5865,
-7386, -8213, -8615, -8680, -8136, -6688, -4666, -2907, -1790, -903, 157, 1053, 1045, -27, -1337, -1746, -843, 843, 2455, 3388, 3284, 1858, -778, -3759,
-5871, -6377, -5457, -4104, -3267, -2915, -2333, -1171, 194, 1203, 1701, 1852, 1796, 1665, 1592, 1539, 1362, 1097, 1011, 1336, 2091, 3053, 3678, 3387,
2231, 928, -33, -725, -1201, -1450, -1615, -1667, -1350, -588, 280, 682, 408, -171, -598, -649, -411, -137, 76, 686, 2270, 3890, 4251, 3549,
2758, 2217, 1855, 3275, 6362, 7820, 7034, 5621, 3908, 1555, -480, -1124, -1268, -1753, -2799, -4129, -4061, -2428, -519, 1031, 1287, 359, -39, 271,
393, 1006, 2328, 2825, 1882, 299, -1046, -1827, -2365, -2299, -660, 1512, 2425, 2529, 3013, 3843, 4724, 5742, 6140, 5161, 3156, 579, -1775, -3432,
-4487, -4226, -2339, 425, 3143, 4976, 5225, 3756, 1585, -264, -1419, -1632, -1331, -1181, -1327, -1672, -1970, -2052, -1734, -801, 679, 2687, 4887, 6323,
6236, 4695, 2529, 622, -488, -631, -269, -224, -793, -1590, -2109, -2129, -1816, -1592, -1716, -2146, -2783, -3428, -3617, -3087, -2126, -1092, -119, 502,
315, -716, -1913, -2322, -1442, 280, 1784, 2542, 2956, 3377, 3432, 2695, 1391, 112, -808, -1508, -2204, -2761, -2831, -2261, -1236, -116, 762, 1284,
1689, 2287, 3033, 3646, 3928, 3753, 3002, 1529, -709, -3263, -5243, -6029, -5667, -4657, -3479, -2274, -861, 846, 2331, 2914, 2685, 2219, 1701, 971,
5, -951, -1360, -534, 1686, 4299, 5901, 6101, 5434, 4483, 3631, 3104, 2797, 2334, 1426, 147, -1102, -1962, -2375, -2362, -2025, -1735, -1673, -1409,
-452, 1132, 2900, 4366, 5197, 5373, 5001, 4009, 2512, 1150, 446, 290, 356, 337, -145, -1188, -2250, -2626, -2204, -1424, -673, -66, 443, 972,
1464, 1645, 1380, 854, 424, 401, 694, 727, -59, -1598, -2995, -3199, -1976, -155, 949, 626, -573, -1449, -1525, -1432, -1774, -2191, -1852, -535,
1011, 1865, 1820, 1133, 23, -1083, -1552, -1330, -1125, -1432, -1906, -1877, -1020, 407, 1661, 2119, 1801, 1128, 518, 277, 543, 1222, 2086, 2820,
2968, 2251, 940, -246, -645, -117, 929, 1992, 2928, 3702, 3819, 2580, -52, -3159, -5522, -6532, -6292, -5004, -2677, 321, 3015, 4559, 4844, 4270,
3292, 2265, 1451, 911, 675, 946, 1658, 2328, 2809, 3357, 3769, 3407, 2221, 950, 111, -525, -1257, -2001, -2582, -2947, -3044, -2712, -1645, 387,
3062, 5513, 6944, 6945, 5534, 3217, 685, -1435, -2429, -1846, -151, 1608, 2625, 2401, 707, -1938, -4143, -4607, -3222, -1103, 403, 540, -843, -3281,
-5693, -6995, -6756, -5271, -3262, -1432, -116, 576, 627, 362, 264, 495, 950, 1453, 1878, 2359, 3110, 3776, 3546, 2246, 748, 52, 442, 1545,
2636, 2981, 2255, 823, -343, -450, 362, 1416, 2231, 2554, 2414, 2302, 2578, 2826, 2384, 1341, 524, 530, 1087, 1477, 1414, 1274, 1505, 2173,
2945, 3273, 2879, 2072, 1348, 909, 418, -831, -2706, -3952, -4165, -4190, -4206, -3613, -2377, -1079, -325, -130, 176, 845, 1549, 2249, 2993, 3355,
2579, 540, -1436, -1841, -956, -241, 63, 485, 764, 1879, 4196, 4159, 139, -3804, -4101, -2261, -712, 370, 120, -963, 111, 2414, 2476, 215,
-2290, -3268, -2580, -1141, 739, 1706, 618, -1096, -2377, -3423, -4303, -4793, -4196, -2885, -2120, -1868, -1909, -2652, -3596, -3952, -4155, -4406, -4256, -4050,
-3997, -3917, -3518, -2021, -174, 421, 327, 852, 1747, 2046, 1577, 740, -84, -449, 20, 1107, 2044, 2289, 1940, 1547, 1727, 2544, 3250, 2974,
1534, -504, -2175, -2592, -1492, 489, 2054, 2062, 318, -2347, -4739, -6143, -6497, -6043, -5055, -3798, -2605, -1631, -703, 226, 993, 1615, 2177, 2473,
2234, 1529, 656, -211, -1067, -1884, -2554, -2867, -2620, -1885, -1022, -401, -203, -389, -811, -1322, -1662, -1529, -871, 135, 1310, 2454, 3140, 2910,
1843, 565, -493, -1469, -2576, -3574, -3827, -2883, -1031, 1000, 2786, 4238, 5151, 5197, 4456, 3482, 2624, 1869, 1375, 1357, 1684, 2121, 2592, 2962,
2951, 2292, 866, -1005, -2574, -3367, -3599, -3797, -4160, -4250, -3403, -1569, 562, 2301, 3314, 3293, 2085, 257, -1127, -1329, -287, 1349, 2542, 2643,
1714, 197, -1266, -1971, -1510, 17, 1962, 3434, 3954, 3527, 2255, 431, -1425, -2947, -3832, -3779, -2998, -2145, -1614, -1450, -1673, -2168, -2544, -2433,
-1764, -756, 278, 1162, 1795, 2093, 2118, 1923, 1278, -8, -1529, -2432, -2285, -1377, -282, 429, 418, -131, -653, -752, -648, -1002, -2154, -3618,
-4528, -4356, -3029, -935, 1106, 2468, 3140, 3415, 3580, 3842, 4149, 4215, 3862, 3070, 1831, 237, -1375, -2521, -2964, -2843, -2188, -808, 1049, 2604,
3351, 3528, 3652, 3905, 4139, 4067, 3313, 1745, -288, -2293, -3902, -4885, -5267, -5296, -5087, -4536, -3590, -2421, -1402, -716, -204, 72, -270, -1210,
-2122, -2503, -2356, -1937, -1591, -1564, -1840, -2261, -2526, -2203, -1164, 165, 1186, 1561, 1391, 1134, 1126, 1116, 670, -74, -478, -7, 1307, 2787,
3635, 3694, 3387, 3034, 2485, 1514, 437, -3, 486, 1241, 1238, 270, -968, -1841, -2143, -1883, -1244, -539, -18, 231, 424, 1009, 2163, 3586,
4948, 6007, 6297, 5415, 3604, 1530, -266, -1357, -1503, -874, 64, 930, 1641, 2166, 2317, 1931, 900, -788, -2721, -4125, -4526, -4164, -3656, -3291,
-2905, -2297, -1387, -270, 810, 1646, 2157, 2413, 2522, 2567, 2604, 2450, 1746, 651, 37, 518, 1622, 2291, 2033, 1161, 79, -1141, -2486, -3605,
-3995, -3512, -2305, -483, 1709, 3779, 5396, 6464, 6783, 6305, 5552, 5071, 4656, 3768, 2448, 1338, 860, 704, 308, -394, -1125, -1908, -2953, -4031,
-4508, -3955, -2433, -337, 1802, 3372, 3840, 3197, 1996, 816, -32, -517, -1008, -2019, -3646, -5425, -6657, -6762, -5581, -3585, -1467, 463, 2049, 2946,
2920, 2172, 1241, 667, 619, 829, 1170, 1882, 2890, 3588, 3404, 2114, -94, -2607, -4689, -5861, -6085, -5757, -5248, -4513, -3392, -1996, -590, 657,
1687, 2419, 2690, 2381, 1569, 518, -397, -834, -669, -122, 400, 552, 100, -1027, -2648, -4258, -5175, -5006, -3914, -2373, -747, 844, 2230, 2937,
2471, 897, -870, -1714, -1428, -819, -821, -1737, -3086, -4009, -4056, -3643, -3509, -3900, -4627, -5435, -6074, -6343, -6083, -5291, -4371, -3864, -3847, -3915,
-3524, -2327, -618, 672, 775, -393, -2363, -4318, -5469, -5619, -5003, -3830, -2475, -1601, -1571, -2159, -2756, -2708, -1734, -33, 1941, 3549, 4286, 4253,
3861, 3230, 2271, 1033, -295, -1417, -2028, -2130, -2026, -1969, -2102, -2432, -2750, -2877, -2725, -2093, -933, 351, 1287, 1691, 1587, 1014, -74, -1587,
-3061, -4118, -4908, -5601, -5927, -5684, -5189, -4713, -3961, -2664, -1241, -298, 160, 492, 664, 335, -395, -963, -985, -476, 356, 1264, 2014, 2348,
2018, 1066, 38, -484, -565, -637, -728, -419, 506, 1779, 2876, 3253, 2728, 1776, 1163, 1318, 2084, 2936, 3389, 3250, 2730, 2366, 2468, 2765,
2834, 2581, 2134, 1648, 1337, 1435, 2018, 2827, 3215, 2443, 390, -1947, -3174, -2910, -1898, -923, -353, -381, -982, -1840, -2467, -2390, -1439, -3,
1228, 1941, 2224, 2152, 1759, 1155, 615, 483, 659, 600, 189, -87, 16, 310, 689, 1049, 976, 132, -1200, -2267, -2652, -2731, -3004, -3191,
-2501, -752, 1318, 2910, 3623, 3227, 1742, -380, -2497, -4085, -4761, -4432, -3507, -2429, -1379, -633, -249, 272, 1145, 1838, 1868, 1302, 512, -124,
-394, -259, 153, 383, -205, -1767, -3608, -4694, -4463, -3138, -1469, -110, 719, 1052, 1129, 1246, 1382, 1282, 926, 811, 1492, 2855, 4210, 4776,
4001, 2173, 503, -95, 203, 747, 1042, 695, -585, -2466, -3849, -3732, -2009, 413, 2068, 1934, 267, -1603, -2377, -1812, -741, -270, -1071, -3029,
-5288, -6979, -7808, -7746, -6732, -4925, -2859, -1189, -168, 156, -656, -1708, -663, 1889, 2968, 2343, 2097, 3524, 6269, 8427, 8273, 6072, 3567, 2123,
1615, 1166, 904, 1570, 2345, 2579, 3552, 4816, 4020, 1582, 6, 93, 915, 1122, 502, 184, 289, 349, 1257, 3116, 4072, 3415, 2148, 687,
-1127, -2772, -3428, -2961, -2123, -1409, -269, 1226, 1718, 907, 45, -117, 300, 842, 774, 28, -758, -1423, -2250, -3096, -3328, -2951, -2605, -2286,
-1626, -785, -56, 712, 1599, 2006, 1392, 48, -1422, -2617, -3064, -2580, -1740, -1139, -783, -364, 334, 1114, 1440, 1046, 164, -918, -2029, -2912,
-3393, -3587, -3483, -2798, -1709, -924, -649, -386, 151, 705, 934, 816, 677, 767, 897, 733, 192, -504, -975, -1079, -1205, -1829, -2811, -3400,
-2921, -1312, 918, 2968, 4010, 3614, 2007, -103, -1736, -1958, -530, 1778, 3720, 4491, 4080, 3021, 1966, 1149, 362, -484, -1219, -1706, -1892, -1709,
-1154, -456, 65, 373, 594, 602, 183, -580, -1501, -2476, -3336, -3880, -3918, -3271, -2084, -968, -566, -995, -1796, -2532, -3127, -3481, -3374, -2736,
-1640, -415, 272, 45, -585, -680, 88, 1140, 1577, 966, -377, -1822, -2994, -3791, -4054, -3528, -2207, -503, 939, 1519, 959, -316, -1301, -1178,
40, 1513, 2196, 1657, 352, -745, -816, 208, 1571, 2381, 2318, 1605, 616, -290, -759, -702, -567, -879, -1633, -2401, -2786, -2717, -2271, -1491,
-613, -19, 69, -482, -1755, -3475, -5113, -6284, -6910, -7094, -6822, -5987, -4685, -3187, -1766, -708, -279, -616, -1589, -2697, -3251, -2985, -2352, -1924,
-1729, -1406, -784, -132, 133, -132, -711, -1088, -687, 638, 2387, 3708, 3922, 2962, 1514, 536, 381, 584, 446, -451, -1976, -3527, -4496, -4662,
-4233, -3580, -2873, -2061, -1120, -47, 1167, 2221, 2582, 2121, 1211, 306, -209, -134, 185, 32, -899, -2110, -2678, -2125, -955, -181, -295, -857,
-1082, -764, -360, -179, -50, 233, 678, 1222, 1741, 1968, 1754, 1329, 947, 578, 114, -448, -922, -934, -399, 132, -52, -1117, -2500, -3337,
-3140, -2133, -923, -118, -167, -1034, -2010, -2201, -1334, 25, 1133, 1736, 2085, 2497, 2865, 2819, 2319, 1771, 1681, 2121, 2645, 2893, 2926, 2944,
3036, 3124, 3036, 2547, 1382, -456, -2344, -3450, -3467, -2621, -1252, 276, 1631, 2708, 3490, 3824, 3524, 2539, 1156, 20, -291, 269, 1096, 1310,
566, -641, -1678, -2375, -2713, -2532, -1858, -1170, -994, -1159, -927, -12, 1103, 1950, 2487, 2646, 2201, 1275, 344, -326, -718, -886, -1016, -1203,
-1267, -958, -132, 1110, 2330, 3194, 3859, 4513, 4986, 4977, 4340, 3246, 2091, 1270, 1080, 1396, 1665, 1619, 1519, 1598, 1755, 1853, 1853, 1677,
1302, 862, 451, 89, -131, -150, -11, 231, 498, 708, 867, 1050, 1329, 1683, 1915, 1715, 1067, 339, -173, -352, -80, 720, 1890, 2941,
3482, 3722, 4170, 4956, 5736, 6110, 5945, 5389, 4751, 4191, 3533, 2566, 1360, 147, -911, -1768, -2359, -2561, -2493, -2587, -3034, -3516, -3530, -2739,
-1246, 364, 1400, 1503, 802, -201, -857, -805, -357, -26, 143, 306, 417, 534, 977, 1815, 2494, 2320, 1204, -236, -1328, -1940, -2263, -2346,
-2134, -1630, -865, 115, 1053, 1486, 1236, 639, 263, 553, 1482, 2498, 3026, 2973, 2630, 2257, 1950, 1726, 1515, 1319, 1445, 2098, 2865, 3110,
2741, 2179, 1719, 1346, 1011, 751, 661, 825, 1118, 1294, 1387, 1700, 2265, 2697, 2757, 2690, 2811, 3026, 2972, 2473, 1614, 464, -944, -2456,
-3769, -4429, -4048, -2802, -1458, -694, -649, -1078, -1534, -1665, -1529, -1277, -705, 460, 2033, 3399, 3922, 3361, 2124, 955, 307, 320, 996, 1872,
2109, 1256, -357, -1869, -2379, -1620, -258, 728, 879, 365, -406, -1230, -2119, -3040, -3953, -4904, -5890, -6698, -6829, -5716, -3282, -399, 1513, 1705,
602, -861, -2018, -2534, -2171, -985, 441, 1361, 1393, 761, 45, -323, -246, 110, 546, 1130, 1963, 2670, 2657, 1829, 700, -181, -641, -678,
-303, 316, 924, 1416, 1800, 1934, 1600, 773, -443, -1831, -2917, -3243, -2862, -2314, -1977, -1731, -1439, -1397, -1979, -3042, -3882, -3887, -3167, -2283,
-1458, -509, 624, 1683, 2411, 2816, 2996, 2920, 2498, 1775, 1002, 454, 124, -125, -200, 154, 1007, 2081, 2847, 2847, 2046, 952, 254, 273,
826, 1538, 2142, 2544, 2697, 2516, 2008, 1344, 607, -409, -1976, -3994, -5763, -6443, -5893, -4707, -3414, -2098, -909, -370, -922, -2398, -4001, -4690,
-3860, -1816, 455, 2089, 2895, 3126, 2833, 1809, 264, -1069, -1695, -1630, -1114, -515, -143, 87, 437, 832, 879, 460, 8, -8, 466, 1174,
1733, 1870, 1576, 982, 466, 118, -879, -2592, -3377, -2243, -1146, -1958, -2615, -1653, -449, 771, 1832, 1784, 1289, 1277, 1328, 892, 598, 794,
729, 845, 1414, 929, -801, -2383, -3326, -3120, -1086, 1193, 2117, 2315, 2465, 2612, 2069, 820, 434, 612, -143, -987, -573, 607, 1264, 1376,
1900, 2587, 2913, 3252, 3615, 3674, 3013, 1563, 540, 567, 867, 1313, 2227, 3527, 5080, 6348, 6537, 5520, 4027, 2766, 1849, 1403, 1593, 2197,
2759, 2937, 2579, 1618, 267, -919, -1469, -1288, -702, -239, -136, -183, -67, 332, 941, 1657, 2304, 2571, 2222, 1251, -107, -1406, -2107, -1806,
-567, 926, 1836, 1915, 1592, 1328, 1136, 854, 578, 554, 796, 1079, 1348, 1826, 2559, 3170, 3257, 2793, 2042, 1308, 788, 412, -101, -825,
-1415, -1466, -885, 162, 1536, 3129, 4625, 5505, 5455, 4611, 3322, 1952, 828, 18, -722, -1632, -2721, -3754, -4350, -4323, -3923, -3475, -3019, -2536,
-2110, -1566, -574, 734, 1957, 2942, 3494, 3182, 1973, 597, -37, 365, 1276, 1862, 1771, 1307, 953, 774, 289, -843, -2064, -2370, -1529, -268,
628, 839, 276, -991, -2591, -4048, -5114, -5710, -5726, -5032, -3645, -1844, -227, 469, -60, -1398, -2727, -3243, -2521, -838, 1056, 2447, 2903, 2442,
1592, 1097, 1512, 2826, 4231, 4744, 4245, 3430, 2876, 2598, 2431, 2426, 2771, 3421, 3841, 3384, 2036, 355, -1243, -2602, -3673, -4618, -5507, -6016,
-5812, -5034, -4189, -3518, -2692, -1324, 373, 1835, 2809, 3279, 3183, 2653, 2107, 1832, 1822, 1902, 1727, 956, -429, -2147, -3819, -5159, -6014, -6300,
-6055, -5334, -4022, -2135, -108, 1416, 1964, 1707, 1422, 1511, 1623, 1523, 1593, 2256, 3387, 4378, 4552, 3649, 1907, -284, -2488, -4105, -4688, -4366,
-3646, -2770, -1612, -283, 620, 571, -274, -1209, -1796, -2275, -3048, -4073, -4846, -4801, -3774, -2217, -804, 230, 1078, 1850, 2540, 3176, 3533, 3098,
1741, 86, -975, -876, 415, 2325, 3850, 4136, 3025, 1125, -475, -953, -473, 96, 157, -297, -1038, -1760, -2006, -1472, -428, 516, 864, 392,
-663, -1638, -2058, -1955, -1457, -640, 185, 613, 567, 293, 4, -262, -401, -256, 54, 408, 951, 1745, 2674, 3648, 4445, 4647, 4062, 3008,
2034, 1387, 869, 146, -778, -1347, -973, 245, 1746, 3184, 4406, 5130, 5092, 4421, 3650, 3139, 2747, 2219, 1561, 913, 323, -156, -505, -873,
-1242, -1263, -731, 34, 439, 357, 319, 700, 1241, 1379, 609, -1150, -3265, -4733, -4962, -4115, -2744, -1315, -126, 780, 1697, 2808, 3855, 4476,
4467, 3789, 2604, 1032, -1043, -3542, -5875, -7328, -7328, -5526, -2359, 731, 2519, 3035, 3047, 3123, 3181, 2702, 1511, 327, 161, 1119, 2268, 2641,
1963, 596, -813, -1743, -1940, -1420, -467, 590, 1598, 2541, 3480, 4367, 4867, 4497, 2974, 537, -2051, -3944, -4602, -3868, -2062, 59, 1721, 2480,
2344, 1564, 322, -1204, -2701, -3795, -4080, -3283, -1654, 162, 1606, 2247, 1903, 882, -333, -1519, -2641, -3513, -3816, -3320, -2102, -784, -204, -561,
-1237, -1452, -981, -263, 232, 548, 892, 1299, 2020, 3475, 5392, 6689, 6570, 5308, 3839, 2849, 2448, 2349, 2171, 1942, 1970, 2196, 2302, 2109,
1562, 892, 564, 554, 247, -715, -1985, -2856, -2787, -1626, 273, 1996, 2665, 2117, 826, -434, -1132, -1480, -2017, -2702, -2849, -2002, -725, -226,
-1083, -2603, -3455, -2850, -1021, 1254, 3189, 4105, 3631, 2155, 648, -428, -1514, -3014, -4618, -5560, -5206, -3561, -1333, 499, 1220, 909, 449, 634,
1421, 2262, 2779, 2993, 3165, 3469, 3731, 3441, 2098, -22, -1758, -2320, -1996, -1480, -1255, -1523, -1867, -1535, -1159, -1743, -1928, -826, -344, -1081,
-1139, 155, 1091, 6, -2350, -4119, -5376, -6469, -5456, -1790, 1047, 1085, 731, 1824, 2840, 2386, 1426, 620, -436, -1508, -2615, -4283, -6104, -7184,
-6620, -3814, 249, 3610, 5402, 6000, 5821, 4430, 1419, -2048, -4401, -5227, -4552, -2762, -991, -371, -890, -1144, -217, 1064, 1818, 2069, 1803, 1007,
-304, -2514, -4948, -5561, -3638, -805, 757, 401, -630, -933, -382, 822, 2394, 3085, 1999, 156, -1026, -2002, -3750, -5482, -6187, -6321, -6365, -6079,
-5292, -3973, -2203, -179, 1762, 2552, 1328, -900, -2497, -3195, -3363, -3116, -2602, -1944, -1098, -247, 210, 54, -724, -1809, -2567, -2645, -2239, -1898,
-2073, -2795, -3912, -5110, -5701, -5182, -3861, -2340, -900, 263, 1161, 2076, 2818, 2764, 1832, 563, -611, -1592, -2399, -3015, -3181, -2615, -1542, -412,
664, 1530, 1834, 1641, 1449, 1596, 1947, 1917, 915, -1020, -3154, -4562, -4777, -4147, -3375, -2785, -2269, -1668, -914, 58, 1293, 2469, 2818, 1811,
-237, -2485, -4216, -5080, -4980, -3868, -1785, 748, 2734, 3334, 2340, 279, -1660, -2255, -1144, 814, 2270, 2625, 2144, 1179, -20, -1185, -2139, -2739,
-2848, -2525, -2081, -1821, -1646, -1029, 458, 2626, 4651, 5649, 5249, 3551, 1003, -1613, -3562, -4581, -4862, -4821, -4755, -4569, -3993, -3066, -2058, -1023,
70, 938, 1205, 841, 127, -558, -752, -111, 1243, 2900, 4565, 6064, 7093, 7214, 6254, 4668, 3256, 2411, 1947, 1490, 669, -647, -2026, -2809,
-2774, -2257, -1851, -1957, -2424, -2696, -2399, -1618, -636, 276, 898, 1159, 1139, 993, 902, 926, 855, 490, 49, 18, 573, 1331, 1836, 1988,
1764, 1064, 16, -1103, -2135, -2935, -3404, -3590, -3478, -2933, -1970, -768, 510, 1688, 2482, 2628, 2065, 1049, 58, -597, -952, -1172, -1373, -1429,
-1097, -532, -254, -478, -786, -594, 89, 699, 944, 997, 938, 691, 339, -9, -388, -748, -860, -677, -500, -550, -660, -604, -474, -476,
-625, -833, -957, -757, -186, 396, 522, 98, -559, -1073, -1184, -801, -152, 328, 469, 454, 472, 566, 818, 1425, 2406, 3392, 3838, 3431,
2334, 1055, 84, -376, -419, -202, 205, 704, 1111, 1341, 1312, 821, -186, -1416, -2435, -3018, -3218, -3082, -2640, -2066, -1437, -605, 403, 1284,
1804, 1914, 1549, 609, -842, -2423, -3519, -3661, -2968, -2053, -1492, -1509, -1918, -2207, -1969, -1336, -873, -1034, -1741, -2424, -2588, -2314, -1910, -1330,
-355, 831, 1566, 1269, -1, -1630, -2904, -3372, -2986, -2094, -1231, -778, -804, -1167, -1561, -1660, -1400, -952, -457, 28, 426, 673, 862, 1187,
1693, 2163, 2314, 2035, 1489, 977, 587, 174, -337, -877, -1293, -1374, -1040, -532, -180, 12, 302, 712, 972, 938, 667, 281, -38, -176,
-267, -623, -1475, -2722, -4020, -5065, -5575, -5288, -4254, -3041, -2390, -2483, -2795, -2841, -2677, -2530, -2282, -1611, -523, 545, 1110, 940, 219, -469,
-555, 16, 664, 735, 182, -586, -1238, -1578, -1496, -1129, -837, -889, -1243, -1650, -1926, -2112, -2305, -2327, -1844, -827, 382, 1432, 2233, 2858,
3226, 3122, 2594, 2024, 1606, 1116, 405, -356, -960, -1345, -1522, -1429, -902, 98, 1247, 1841, 1290, -204, -1731, -2542, -2514, -1789, -576, 634,
1188, 683, -753, -2520, -3861, -4177, -3303, -1762, -375, 578, 1379, 2071, 2176, 1317, -46, -840, -520, 401, 1150, 1480, 1557, 1620, 1802, 1986,
1884, 1405, 927, 931, 1301, 1450, 1048, 218, -673, -1162, -863, 260, 1756, 3060, 3951, 4347, 4040, 2964, 1569, 625, 619, 1445, 2630, 3709,
4272, 4073, 3363, 2625, 1881, 745, -846, -2287, -2720, -1727, 365, 2721, 4554, 5459, 5330, 4317, 2902, 1597, 576, -134, -388, -91, 468, 680,
190, -702, -1435, -1773, -1773, -1404, -635, 359, 1350, 2115, 2410, 2163, 1593, 1008, 621, 495, 531, 598, 574, 360, -49, -581, -1046, -1145,
-745, -24, 734, 1260, 1325, 803, -241, -1433, -2303, -2657, -2442, -1512, 38, 1463, 1804, 936, -216, -692, -419, 86, 545, 1081, 1671, 1921,
1521, 564, -436, -736, 66, 1637, 3269, 4504, 5198, 5178, 4186, 2220, -231, -2406, -3792, -4336, -4020, -2705, -674, 1261, 2356, 2342, 1633, 1143,
1471, 2384, 3264, 3730, 3700, 3284, 2762, 2257, 1610, 902, 607, 935, 1490, 1737, 1620, 1412, 1203, 1053, 1171, 1455, 1480, 1114, 672, 487,
509, 402, -5, -495, -758, -788, -756, -646, -280, 344, 920, 1133, 1033, 982, 1215, 1549, 1715, 1829, 2080, 2322, 2319, 2047, 1505, 622,
-417, -1225, -1675, -1916, -2039, -1915, -1329, -330, 590, 844, 253, -882, -2108, -2956, -2996, -2183, -1028, -180, 99, -29, -337, -794, -1487, -2326,
-2933, -2950, -2422, -1703, -1056, -571, -399, -704, -1338, -1826, -1740, -1062, -149, 623, 1124, 1447, 1761, 2123, 2322, 2041, 1273, 363, -343, -774,
-1045, -1168, -1087, -984, -1180, -1705, -2244, -2481, -2279, -1623, -630, 312, 648, 81, -1168, -2561, -3533, -3642, -2681, -895, 964, 2053, 2036, 1218,
169, -609, -780, -262, 629, 1384, 1855, 2263, 2622, 2716, 2501, 2062, 1488, 1002, 819, 865, 889, 749, 495, 239, -45, -442, -851, -1103,
-1236, -1352, -1392, -1289, -1168, -1202, -1276, -1035, -375, 363, 727, 529, -96, -871, -1444, -1502, -1027, -319, 336, 850, 1132, 1056, 682, 149,
-472, -1016, -1260, -1201, -961, -636, -364, -213, -32, 369, 955, 1526, 1915, 1966, 1574, 844, 80, -403, -455, -191, 64, 84, -104, -298,
-363, -436, -644, -662, 45, 1496, 3063, 4105, 4410, 4060, 3183, 1963, 816, 350, 854, 1888, 2553, 2256, 1209, 94, -703, -1246, -1612, -1728,
-1456, -695, 424, 1508, 2180, 2278, 1813, 913, -147, -1063, -1787, -2511, -3186, -3480, -3295, -2828, -2197, -1475, -868, -519, -415, -501, -638, -640,
-519, -527, -790, -1050, -942, -432, 125, 347, 72, -647, -1440, -1724, -1239, -241, 843, 1687, 1993, 1575, 586, -612, -1647, -2087, -1650, -555,
632, 1539, 2046, 2018, 1437, 626, -47, -488, -602, -208, 544, 1138, 1144, 576, -176, -792, -1256, -1611, -1855, -1988, -1975, -1784, -1489, -1231,
-990, -585, -7, 561, 1041, 1450, 1785, 2066, 2349, 2609, 2600, 1937, 538, -1146, -2498, -3128, -2873, -1829, -497, 608, 1450, 2241, 2938, 3166,
2664, 1730, 954, 497, -18, -866, -1766, -2131, -1636, -611, 189, 343, 101, -72, -74, -151, -530, -1202, -1818, -1979, -1724, -1402, -1168, -914,
-499, 72, 552, 584, 159, -304, -412, -23, 822, 1878, 2723, 2942, 2423, 1560, 896, 512, 67, -526, -909, -870, -649, -531, -505, -402,
-137, 250, 696, 1081, 1208, 996, 594, 201, -56, -130, -117, -264, -787, -1620, -2285, -2239, -1404, -277, 564, 988, 1134, 1082, 934, 767,
509, 176, -75, -147, -62, 24, -15, -35, 206, 581, 699, 558, 573, 968, 1527, 1904, 1943, 1734, 1418, 1016, 508, -24, -368, -365,
-123, 176, 561, 1123, 1802, 2423, 2778, 2607, 1774, 592, -374, -875, -1032, -1002, -826, -534, -208, 76, 202, -38, -672, -1312, -1516, -1333,
-1150, -1082, -933, -556, 44, 686, 1067, 979, 337, -749, -1812, -2223, -1745, -789, 27, 453, 579, 562, 489, 356, 264, 345, 485, 529,
454, 224, -114, -354, -319, 0, 396, 596, 567, 514, 565, 559, 237, -324, -765, -917, -975, -1182, -1455, -1481, -1175, -693, -171, 163,
-93, -1095, -2361, -3028, -2562, -1260, 96, 1017, 1474, 1561, 1187, 121, -1597, -3453, -4720, -4911, -4083, -2792, -1663, -1010, -812, -831, -706, -203,
586, 1343, 1637, 1109, -211, -1842, -3038, -3159, -2208, -963, -342, -590, -1219, -1676, -1827, -1771, -1590, -1477, -1621, -1880, -1902, -1456, -516, 586,
1280, 1429, 1434, 1476, 1332, 945, 620, 703, 1270, 1984, 2407, 2465, 2294, 1782, 664, -827, -1892, -1919, -967, 452, 1727, 2367, 2158, 1337,
563, 396, 763, 1103, 916, 114, -966, -1869, -2488, -3010, -3420, -3491, -3171, -2585, -1746, -600, 658, 1607, 2008, 1919, 1412, 517, -599, -1702,
-2587, -2990, -2736, -1917, -590, 1011, 2251, 3020, 3835, 4527, 4179, 2979, 2167, 1847, 1041, 64, 219, 948, 568, -814, -1317, -660, 56, 522,
88, -965, -1402, -1375, -1049, -429, -619, -880, -358, -775, -1837, -2316, -2339, -1542, -261, 856, 1509, 530, -1825, -3003, -2193, -892, -297, -445,
-208, 1175, 2318, 1854, 771, -273, -1407, -1573, -1096, -1344, -2012, -2446, -2187, -1280, -684, -406, 277, 1358, 2290, 2622, 2116, 829, -331, -727,
-534, 358, 1661, 2049, 1029, -169, -899, -1523, -1828, -1605, -1003, 45, 1539, 3104, 4332, 4951, 4680, 3494, 1887, 508, -289, -461, -235, -84,
-332, -824, -1390, -2224, -3415, -4571, -5074, -4541, -3149, -1567, -457, 6, 149, 304, 426, 275, -224, -932, -1644, -2220, -2544, -2374, -1517, -186,
1058, 1734, 1848, 1774, 1763, 1841, 1831, 1446, 688, -35, -292, 101, 951, 1768, 2264, 2483, 2532, 2579, 2704, 2585, 1942, 1112, 627, 459,
135, -576, -1415, -1888, -1617, -638, 478, 1034, 793, 65, -554, -503, 311, 1398, 2096, 2049, 1444, 781, 422, 487, 829, 924, 248, -1169,
-2823, -4169, -4886, -4879, -4249, -3370, -2613, -1893, -947, 107, 831, 982, 789, 621, 661, 961, 1425, 1693, 1492, 968, 339, -492, -1676, -2881,
-3434, -3037, -2072, -1215, -737, -405, -148, -291, -1030, -2036, -2678, -2461, -1238, 687, 2642, 4102, 5040, 5625, 5809, 5360, 4038, 1726, -1209, -3721,
-4760, -4102, -2358, -578, 388, 588, 773, 1352, 1857, 1564, 430, -715, -1026, -568, -63, 100, -33, -481, -1096, -1308, -630, 845, 2398, 3023,
2227, 588, -759, -1243, -1175, -1211, -1712, -2464, -3074, -3512, -3955, -4319, -4302, -3721, -2756, -1887, -1352, -990, -788, -881, -959, -499, 450, 1305,
1612, 1367, 1026, 1214, 2098, 3161, 3678, 3314, 2387, 1545, 1220, 1452, 2019, 2607, 2888, 2653, 1926, 817, -538, -1838, -2694, -2854, -2221, -825,
961, 2410, 2923, 2445, 1477, 642, 144, -139, -365, -615, -737, -438, 277, 954, 1076, 543, -351, -1211, -1641, -1482, -975, -354, 379, 1021,
1160, 793, 347, 207, 520, 1001, 1053, 496, -314, -1074, -1591, -1698, -1528, -1365, -1213, -916, -578, -496, -677, -778, -479, 271, 1127, 1559,
1424, 980, 415, -171, -486, -214, 444, 781, 349, -489, -944, -634, 134, 916, 1547, 1905, 1730, 931, -142, -915, -969, -279, 656, 1120,
811, 3, -745, -954, -663, -429, -686, -1381, -2141, -2582, -2570, -2367, -2294, -2288, -2046, -1405, -394, 764, 1630, 1868, 1582, 1093, 501, -292,
-1213, -1870, -1859, -1122, 31, 1029, 1365, 1085, 686, 548, 708, 992, 1170, 1140, 986, 783, 461, 74, -75, 167, 562, 756, 699, 726,
1113, 1640, 1801, 1378, 623, -91, -547, -697, -657, -661, -864, -1205, -1490, -1498, -1211, -993, -1209, -1656, -1755, -1343, -843, -596, -501, -279,
249, 977, 1452, 1260, 576, 64, 132, 506, 520, -184, -1223, -1830, -1664, -1077, -678, -814, -1384, -1933, -2073, -1919, -1943, -2358, -2868, -3079,
-2941, -2660, -2319, -1808, -1065, -300, 84, -91, -552, -875, -796, -255, 557, 1249, 1578, 1655, 1725, 1932, 2305, 2711, 2926, 2942, 2954, 3018,
2965, 2676, 2149, 1358, 341, -700, -1489, -1711, -1228, -425, -7, -251, -817, -1334, -1693, -1751, -1307, -470, 349, 907, 1292, 1581, 1632, 1311,
730, 149, -246, -384, -288, -149, -271, -776, -1398, -1633, -1159, -151, 816, 1298, 1338, 1263, 1340, 1698, 2240, 2606, 2438, 1821, 876, -666,
-2076, -2430, -2390, -2272, -1824, -992, 369, 1318, 1316, 1007, 32, -1419, -1925, -1645, -935, 553, 1655, 2175, 3110, 3381, 2868, 2703, 1893, 176,
-898, -700, -148, -314, -675, -793, -1098, -1703, -2540, -2975, -2637, -1863, -676, 636, 1182, 765, 238, 383, 728, 418, 64, -58, -897, -2077,
-2840, -3467, -3735, -3092, -1592, 26, 933, 1115, 594, -733, -2129, -2653, -2032, -595, 956, 2159, 2978, 3297, 3028, 2302, 1356, 574, 380, 935,
1765, 2045, 1510, 617, -122, -783, -1663, -2369, -2318, -1560, -511, 302, 531, 351, 29, -429, -865, -1005, -950, -821, -557, -286, -188, -253,
-435, -704, -1021, -1261, -1221, -1045, -1025, -1163, -1430, -1715, -1799, -1663, -1310, -719, -33, 542, 687, 191, -430, -365, 422, 1356, 1967, 2000,
1451, 701, 144, -259, -740, -1442, -2211, -2565, -2366, -1919, -1511, -1247, -946, -385, 332, 844, 835, 392, -54, -244, -367, -678, -1040, -1080,
-741, -384, -312, -502, -803, -1122, -1439, -1790, -2105, -2118, -1667, -983, -499, -456, -808, -1324, -1677, -1635, -1284, -932, -737, -584, -352, -74,
198, 492, 721, 599, 53, -548, -759, -499, -107, 24, -143, -344, -447, -570, -701, -637, -287, 253, 785, 1093, 1053, 680, 239, 161,
570, 1171, 1659, 1841, 1535, 698, -419, -1384, -1801, -1433, -316, 1127, 2301, 2786, 2530, 1843, 1167, 770, 606, 470, 286, 146, 24, -233,
-552, -562, -12, 948, 1897, 2413, 2319, 1688, 742, -177, -715, -648, -57, 596, 778, 332, -486, -1269, -1661, -1514, -972, -374, 17, 241,
498, 848, 1169, 1313, 1120, 492, -342, -897, -899, -546, -294, -370, -605, -822, -1064, -1343, -1463, -1271, -938, -833, -1134, -1616, -1817, -1485,
-902, -607, -721, -830, -547, 138, 1004, 1748, 2024, 1681, 1018, 524, 335, 190, -135, -517, -623, -300, 343, 1133, 1847, 2174, 1950, 1342,
621, 12, -224, -5, 406, 755, 992, 1035, 729, 87, -624, -1008, -894, -560, -344, -249, -68, 328, 747, 717, 24, -1013, -1917, -2477,
-2689, -2573, -2219, -1714, -1045, -253, 538, 1248, 1777, 2015, 1914, 1447, 738, 102, -308, -607, -808, -703, -349, -245, -678, -1368, -1948, -2216,
-2019, -1360, -463, 376, 928, 1099, 981, 834, 867, 983, 937, 673, 332, -14, -422, -882, -1189, -1049, -359, 548, 1202, 1582, 2001, 2575,
3189, 3632, 3609, 3079, 2503, 2319, 2326, 1989, 1094, -118, -1097, -1233, -392, 768, 1317, 902, -127, -1189, -1731, -1447, -537, 407, 940, 1028,
741, 22, -955, -1572, -1335, -527, 69, 15, -564, -1318, -1833, -1843, -1381, -636, 99, 534, 700, 824, 909, 796, 527, 373, 551, 1033,
1468, 1345, 556, -375, -879, -893, -699, -613, -879, -1484, -2095, -2302, -1866, -819, 538, 1687, 2138, 1845, 1196, 591, 191, -108, -531, -1162,
-1785, -1936, -1324, -185, 947, 1685, 1990, 2083, 2148, 2102, 1705, 900, -56, -859, -1421, -1736, -1644, -1076, -432, -281, -680, -1187, -1433, -1303,
-821, -216, 118, -77, -694, -1298, -1518, -1313, -806, -136, 457, 753, 676, 274, -252, -559, -399, 227, 1121, 1916, 2146, 1597, 576, -380,
-972, -1236, -1358, -1463, -1509, -1382, -1106, -904, -942, -1049, -864, -208, 831, 1958, 2745, 2832, 2166, 1105, 248, -39, 69, 191, 201, 175,
68, -189, -438, -369, 148, 842, 1223, 999, 282, -606, -1394, -1820, -1632, -879, 9, 492, 215, -717, -1757, -2388, -2501, -2224, -1717, -1172,
-705, -346, -188, -367, -856, -1437, -1799, -1593, -654, 674, 1761, 2157, 1679, 482, -782, -1391, -1196, -594, 33, 611, 1174, 1590, 1644, 1243,
485, -376, -1083, -1468, -1464, -1132, -628, -193, -106, -471, -1013, -1242, -898, -249, 94, -231, -1007, -1619, -1516, -680, 296, 808, 695, 117,
-545, -814, -540, -49, 203, -44, -684, -1328, -1603, -1264, -285, 974, 1974, 2412, 2274, 1744, 1151, 786, 724, 818, 853, 725, 517, 438,
670, 1221, 1882, 2330, 2392, 2232, 2131, 2056, 1641, 741, -236, -733, -628, -208, 258, 744, 1238, 1577, 1624, 1478, 1402, 1531, 1710, 1695,
1398, 928, 506, 309, 296, 211, -104, -451, -542, -415, -299, -257, -187, -55, 33, 8, -93, -249, -484, -706, -666, -188, 624, 1471,
2069, 2231, 1964, 1524, 1157, 953, 943, 1093, 1312, 1497, 1438, 946, 222, -233, -235, -121, -262, -607, -855, -864, -780, -830, -1027, -1143,
-1003, -607, -48, 500, 873, 1104, 1388, 1843, 2376, 2744, 2708, 2179, 1333, 566, 238, 417, 875, 1274, 1343, 983, 262, -601, -1210, -1156,
-433, 414, 788, 688, 631, 950, 1433, 1587, 1089, 57, -1092, -2026, -2618, -2790, -2463, -1725, -856, -125, 349, 602, 736, 863, 1063, 1263,
1171, 486, -690, -1726, -1977, -1407, -499, 285, 752, 810, 362, -468, -1251, -1626, -1565, -1278, -1047, -1071, -1370, -1803, -2083, -1853, -1002, 63,
738, 849, 651, 415, 334, 524, 839, 948, 719, 418, 361, 504, 562, 389, 109, 36, 400, 1028, 1539, 1821, 2026, 2247, 2404, 2322,
1969, 1485, 904, 181, -529, -855, -560, 205, 1164, 2147, 2964, 3296, 2928, 2078, 1272, 782, 478, 175, -224, -728, -1131, -1143, -779, -432,
-416, -623, -798, -844, -719, -413, -120, -101, -443, -1088, -1882, -2562, -2877, -2773, -2397, -1926, -1467, -962, -239, 649, 1313, 1464, 1216, 758,
84, -731, -1356, -1542, -1396, -1085, -564, 266, 1250, 1963, 2142, 2009, 1909, 1762, 1176, -8, -1496, -2744, -3404, -3546, -3336, -2782, -1911, -876,
106, 842, 1294, 1489, 1372, 1007, 633, 336, -36, -579, -1038, -918, -15, 1189, 1878, 1599, 526, -827, -2025, -2869, -3274, -3131, -2429, -1429,
-533, 25, 295, 377, 219, -220, -709, -807, -194, 1027, 2258, 2912, 3019, 2961, 2850, 2475, 1618, 258, -1296, -2397, -2415, -1350, 16, 832,
896, 470, -142, -662, -916, -928, -812, -681, -702, -993, -1385, -1568, -1426, -931, -45, 1034, 1911, 2209, 1741, 806, 0, -393, -402, -172,
-58, -323, -661, -479, 325, 1257, 1863, 2045, 1839, 1352, 773, 306, 75, -44, -285, -535, -449, 75, 880, 1749, 2404, 2630, 2487, 2235,
2040, 1802, 1401, 979, 692, 420, -41, -695, -1283, -1507, -1307, -926, -765, -1073, -1691, -2217, -2440, -2470, -2478, -2492, -2460, -2364, -2149, -1713,
-1043, -258, 393, 705, 799, 999, 1448, 1907, 1914, 1258, 285, -440, -597, -158, 665, 1448, 1846, 1933, 1997, 2119, 2175, 2048, 1725, 1345,
1057, 845, 702, 815, 1265, 1736, 1910, 1885, 1766, 1284, 240, -1069, -2056, -2338, -2002, -1378, -666, 98, 913, 1769, 2548, 2938, 2669, 1867,
937, 174, -314, -509, -504, -462, -461, -385, 19, 824, 1731, 2350, 2452, 1947, 917, -324, -1434, -2190, -2468, -2315, -1922, -1493, -1246, -1302,
-1531, -1588, -1121, -194, 610, 885, 870, 952, 1164, 1363, 1477, 1552, 1688, 1889, 2042, 2007, 1658, 962, 55, -770, -1219, -1167, -715, -122,
317, 445, 286, -28, -370, -723, -1165, -1751, -2421, -2943, -2998, -2463, -1515, -447, 425, 855, 829, 503, 16, -551, -1123, -1602, -1895, -1891,
-1478, -702, 188, 905, 1244, 1098, 525, -318, -1284, -2189, -2819, -3072, -3022, -2756, -2286, -1752, -1413, -1300, -1166, -846, -483, -368, -613, -1007,
-1187, -904, -160, 771, 1535, 1967, 2141, 2154, 1961, 1417, 501, -544, -1345, -1751, -1987, -2282, -2464, -2190, -1516, -861, -469, -302, -270, -257,
-136, 150, 616, 1191, 1646, 1744, 1465, 973, 423, -111, -639, -1174, -1627, -1861, -1790, -1461, -1115, -956, -946, -928, -764, -375, 269, 1078,
1786, 2067, 1725, 791, -481, -1706, -2499, -2642, -2227, -1598, -1062, -765, -797, -1125, -1459, -1433, -990, -462, -211, -291, -417, -225, 320, 902,
1200, 1146, 861, 506, 199, -2, -48, 118, 368, 430, 281, 183, 295, 537, 798, 1010, 1060, 808, 249, -517, -1427, -2355, -3065, -3344,
-3114, -2502, -1769, -1118, -622, -265, -6, 158, 174, -51, -541, -1151, -1664, -1874, -1674, -1173, -591, -44, 436, 823, 1049, 953, 425, -311,
-780, -774, -543, -343, -110, 243, 564, 649, 491, 227, -27, -209, -277, -193, 81, 553, 1077, 1294, 894, 14, -813, -1230, -1353, -1374,
-1253, -907, -369, 218, 629, 702, 520, 311, 201, 135, -30, -323, -629, -890, -1133, -1304, -1337, -1302, -1252, -1132, -949, -761, -542, -226,
198, 637, 870, 748, 404, 124, 81, 284, 687, 1200, 1664, 1861, 1587, 851, -82, -864, -1254, -1249, -1003, -593, -53, 407, 548, 420,
314, 419, 648, 838, 977, 1116, 1238, 1351, 1526, 1735, 1771, 1416, 680, -111, -531, -441, -39, 469, 936, 1127, 858, 294, -120, -117,
168, 479, 694, 778, 723, 585, 418, 171, -166, -450, -574, -549, -345, 37, 451, 776, 1039, 1256, 1344, 1230, 884, 296, -398, -895,
-1003, -782, -438, -156, 53, 291, 556, 693, 512, -57, -871, -1608, -1918, -1638, -910, -55, 687, 1227, 1532, 1664, 1732, 1694, 1425, 959,
473, 89, -176, -299, -147, 383, 1163, 1869, 2261, 2316, 2130, 1743, 1099, 277, -434, -769, -637, -119, 536, 967, 925, 443, -226, -768,
-997, -993, -932, -874, -851, -904, -921, -783, -569, -394, -264, -204, -271, -377, -308, 1, 355, 519, 436, 175, -156, -454, -704, -890,
-919, -774, -505, -102, 353, 597, 488, 145, -245, -513, -516, -243, 150, 413, 416, 254, 19, -282, -588, -830, -980, -979, -791, -439,
25, 452, 590, 349, -68, -427, -713, -993, -1212, -1322, -1435, -1638, -1795, -1701, -1330, -893, -677, -796, -1104, -1366, -1414, -1174, -704, -195,
142, 214, 101, 3, 45, 117, 78, 27, 172, 501, 770, 836, 806, 761, 620, 354, 101, 38, 208, 509, 783, 871, 646, 134,
-413, -683, -623, -475, -459, -557, -648, -729, -827, -805, -516, -105, 130, 101, -25, -17, 239, 638, 932, 974, 834, 637, 434, 253,
78, -130, -347, -514, -620, -664, -641, -593, -579, -590, -527, -348, -177, -80, 146, 767, 1740, 2696, 3256, 3239, 2659, 1760, 988, 701,
887, 1229, 1400, 1276, 1018, 880, 871, 730, 283, -316, -761, -900, -790, -537, -289, -277, -588, -997, -1216, -1129, -777, -357, -175, -449,
-1114, -1799, -2044, -1720, -1150, -723, -543, -506, -546, -774, -1303, -1968, -2401, -2325, -1704, -799, -54, 231, 124, -111, -311, -380, -169, 340,
869, 1119, 1071, 845, 489, 34, -373, -504, -261, 168, 495, 589, 492, 380, 507, 932, 1403, 1667, 1700, 1523, 1051, 304, -426, -816,
-776, -487, -278, -360, -580, -609, -312, 223, 888, 1554, 2054, 2319, 2387, 2262, 1869, 1130, 34, -1253, -2351, -2892, -2770, -2163, -1394, -773,
-499, -568, -727, -727, -564, -368, -211, -111, -71, -69, -55, 15, 130, 263, 467, 733, 874, 801, 724, 875, 1220, 1546, 1696, 1636,
1389, 956, 411, -49, -299, -339, -175, 96, 259, 172, -133, -553, -974, -1291, -1406, -1280, -1022, -841, -828, -848, -761, -573, -336, -106,
-15, -201, -657, -1215, -1663, -1811, -1534, -828, 197, 1297, 2092, 2291, 1939, 1410, 1094, 1085, 1198, 1250, 1206, 1067, 792, 431, 99, -207,
-498, -661, -579, -220, 287, 727, 1018, 1190, 1181, 867, 249, -469, -981, -1061, -719, -154, 443, 946, 1257, 1403, 1497, 1544, 1438, 1112,
649, 198, -205, -635, -1066, -1302, -1221, -946, -693, -545, -503, -600, -786, -877, -780, -561, -293, -29, 163, 235, 145, -106, -387, -466,
-200, 305, 717, 717, 350, 16, 40, 381, 766, 998, 1115, 1213, 1248, 1103, 740, 224, -324, -783, -1070, -1163, -1109, -1014, -929, -794,
-572, -340, -145, 113, 473, 769, 791, 460, -96, -642, -1000, -1053, -767, -336, -87, -135, -309, -441, -522, -580, -580, -463, -227, 55,
298, 481, 646, 834, 1024, 1085, 898, 562, 332, 341, 459, 427, 147, -223, -495, -586, -479, -249, -65, -30, -125, -321, -627, -1012,
-1362, -1502, -1330, -905, -397, 27, 300, 451, 483, 329, -48, -548, -929, -1000, -773, -405, -124, -106, -262, -321, -169, 94, 398, 753,
1121, 1384, 1442, 1287, 994, 689, 449, 258, 62, -140, -244, -81, 378, 899, 1150, 1023, 669, 265, -55, -134, 101, 497, 730, 549,
2, -604, -968, -990, -827, -694, -582, -362, -85, 27, -136, -454, -645, -490, -65, 379, 745, 1087, 1392, 1525, 1364, 953, 489, 101,
-268, -670, -938, -833, -374, 114, 368, 435, 508, 694, 949, 1120, 1063, 799, 513, 355, 289, 146, -163, -483, -586, -517, -561, -879,
-1267, -1357, -1084, -740, -509, -278, 72, 463, 722, 738, 463, -66, -607, -846, -698, -308, 134, 416, 302, -291, -1198, -2059, -2450, -2131,
-1256, -250, 505, 801, 623, 164, -291, -554, -578, -418, -232, -205, -424, -765, -966, -871, -571, -319, -284, -364, -366, -320, -400, -575,
-606, -422, -216, -142, -195, -350, -499, -448, -235, -168, -413, -826, -1129, -1118, -746, -73, 776, 1555, 1901, 1555, 594, -612, -1678, -2338,
-2418, -1841, -760, 419, 1280, 1676, 1733, 1650, 1462, 1041, 410, -118, -242, -3, 277, 398, 458, 597, 746, 717, 426, -26, -443, -687,
-762, -698, -446, -17, 389, 563, 520, 373, 138, -177, -486, -683, -674, -451, -198, -138, -232, -246, -63, 189, 261, 19, -344, -447,
-86, 583, 1183, 1409, 1223, 793, 366, 133, 115, 120, -86, -505, -854, -843, -468, 44, 508, 853, 1025, 994, 797, 486, 77, -389,
-819, -1097, -1105, -760, -117, 586, 1031, 1027, 658, 227, 32, 164, 467, 679, 690, 604, 539, 467, 314, 64, -223, -427, -438, -272,
-113, -194, -568, -1032, -1355, -1481, -1487, -1414, -1217, -825, -231, 489, 1180, 1640, 1691, 1317, 759, 323, 78, -107, -269, -269, -56, 190,
316, 394, 501, 532, 374, 109, -46, 50, 358, 707, 926, 864, 444, -202, -791, -1114, -1146, -1003, -845, -760, -747, -708, -542, -301,
-98, 47, 211, 480, 836, 1138, 1267, 1219, 1057, 812, 410, -174, -754, -1052, -996, -781, -644, -630, -576, -327, 51, 396, 632, 785,
927, 1059, 1052, 806, 447, 242, 312, 515, 634, 572, 366, 114, -48, 20, 320, 613, 631, 349, -59, -406, -560, -483, -224, 139,
505, 734, 764, 689, 622, 537, 355, 109, -91, -168, -67, 178, 389, 400, 234, 11, -165, -251, -270, -266, -264, -289, -349, -397,
-348, -150, 155, 465, 677, 727, 592, 344, 146, 79, 44, -87, -312, -568, -839, -1082, -1181, -1090, -884, -673, -546, -535, -593, -625,
-531, -266, 63, 253, 244, 183, 184, 179, 81, -90, -270, -433, -574, -666, -622, -402, -142, -9, 4, -17, -88, -254, -521, -868,
-1240, -1505, -1490, -1104, -441, 266, 794, 1036, 986, 709, 368, 189, 222, 325, 302, -91, -496, -299, 109, 276, 332, -13, -511, -74,
943, 1267, 638, -438, -1236, -1376, -878, -209, -99, -324, -127, 426, 683, 431, 22, -360, -477, -107, 18, -557, -940, -762, -748, -978,
-797, -17, 848, 1059, 591, 218, 152, 117, 138, 108, 36, 257, 659, 779, 524, 251, 157, 96, -81, -366, -501, -375, -311, -441,
-445, -73, 409, 421, 24, -68, 236, 403, 198, -95, -123, -2, -20, -90, -38, 175, 468, 646, 626, 527, 551, 782, 1089, 1215,
1009, 563, 131, -35, 168, 621, 1075, 1336, 1305, 956, 393, -132, -346, -182, 135, 302, 235, 85, -36, -158, -315, -499, -643, -632,
-411, -61, 249, 366, 287, 163, 114, 130, 124, 20, -147, -271, -306, -274, -192, -94, -27, 0, 11, 51, 163, 364, 586, 693,
592, 351, 127, -5, -72, -122, -187, -241, -244, -193, -112, -21, 77, 185, 251, 207, 86, 19, 56, 132, 184, 166, 26, -230,
-488, -606, -510, -217, 145, 428, 587, 650, 658, 654, 644, 571, 370, 32, -346, -583, -577, -443, -366, -398, -452, -383, -108, 272,
545, 564, 327, -50, -423, -674, -740, -637, -404, -53, 325, 547, 558, 466, 368, 283, 209, 144, 36, -205, -582, -966, -1179, -1117,
-827, -421, -4, 305, 392, 240, -34, -234, -217, -19, 181, 246, 189, 88, -25, -162, -303, -392, -391, -355, -376, -449, -446, -255,
68, 296, 258, 51, -82, -30, 133, 301, 434, 496, 437, 248, -19, -276, -412, -360, -184, -48, -40, -66, 36, 277, 477, 481,
303, 49, -170, -260, -169, 70, 379, 671, 861, 913, 830, 575, 125, -416, -869, -1087, -990, -565, 78, 757, 1320, 1691, 1870, 1906,
1843, 1698, 1440, 978, 266, -543, -1142, -1301, -1045, -601, -170, 182, 429, 521, 440, 246, 63, -41, -95, -99, 6, 206, 333, 221,
-84, -412, -681, -903, -1088, -1223, -1289, -1243, -1054, -789, -578, -431, -248, -57, -4, -107, -215, -184, 13, 348, 750, 1057, 1086, 771,
175, -540, -1104, -1242, -886, -284, 166, 252, 91, -43, 28, 335, 754, 1064, 1171, 1177, 1200, 1258, 1292, 1199, 955, 675, 507, 512,
607, 622, 455, 149, -184, -464, -630, -663, -620, -540, -376, -95, 241, 521, 696, 774, 710, 411, -114, -697, -1128, -1246, -982, -468,
-36, 81, -49, -192, -169, 42, 371, 744, 1110, 1423, 1644, 1721, 1580, 1185, 606, 33, -304, -299, -50, 180, 139, -242, -795, -1277,
-1562, -1652, -1569, -1303, -857, -339, 81, 331, 486, 684, 948, 1127, 1057, 687, 6, -726, -807, 74, 1037, 976, 13, -768, -570, 490,
1587, 1982, 1553, 671, 5, 27, 434, 637, 592, 417, -78, -836, -1223, -865, -252, 0, -46, -46, -44, -302, -746, -1116, -1445, -1723,
-1615, -1052, -459, -191, -216, -290, -297, -317, -323, -172, 121, 425, 584, 480, 230, 137, 352, 755, 1110, 1177, 848, 317, -100, -379,
-652, -819, -688, -281, 127, 246, 119, 71, 234, 428, 424, 162, -155, -227, 30, 402, 562, 376, -3, -427, -844, -1092, -952, -416,
289, 882, 1192, 1172, 802, 221, -192, -227, -72, 39, 97, 110, -4, -213, -330, -238, -15, 157, 196, 206, 286, 312, 86, -317,
-580, -537, -301, -2, 325, 614, 785, 808, 623, 238, -181, -515, -763, -908, -915, -820, -702, -609, -545, -500, -444, -330, -133, 102,
293, 369, 337, 269, 233, 204, 88, -163, -474, -692, -724, -569, -268, 102, 406, 517, 420, 218, 69, 68, 173, 284, 375, 464,
535, 537, 436, 245, 42, -69, -69, -41, -77, -194, -382, -597, -765, -858, -895, -884, -816, -660, -406, -131, 74, 238, 450, 720,
980, 1177, 1246, 1107, 798, 462, 167, -114, -359, -488, -482, -413, -384, -441, -534, -582, -552, -461, -332, -157, 102, 427, 684, 748,
609, 371, 176, 77, 20, -38, -86, -132, -209, -313, -417, -506, -534, -446, -249, -14, 160, 254, 339, 471, 606, 645, 507, 205,
-116, -268, -205, -72, -27, -66, -81, -43, -13, -5, 27, 103, 193, 249, 200, -21, -376, -713, -910, -918, -725, -393, -68, 123,
125, -71, -391, -641, -634, -397, -147, -16, 48, 100, 102, 11, -132, -237, -217, -34, 261, 554, 766, 909, 1003, 1015, 903, 661,
320, -34, -322, -533, -646, -580, -327, -18, 184, 234, 215, 219, 249, 246, 162, -9, -251, -509, -681, -719, -686, -641, -561, -435,
-304, -218, -195, -192, -155, -45, 144, 358, 509, 552, 471, 272, 27, -135, -125, 44, 262, 374, 274, 1, -298, -491, -512, -374,
-171, -36, -60, -212, -355, -360, -212, 21, 278, 504, 648, 676, 555, 278, -76, -354, -409, -205, 118, 334, 348, 268, 223, 226,
232, 213, 191, 237, 375, 500, 471, 297, 137, 116, 226, 361, 404, 299, 79, -153, -286, -286, -201, -81, 25, 83, 121, 201,
299, 296, 111, -199, -486, -616, -542, -335, -126, 3, 91, 209, 359, 471, 467, 354, 226, 157, 125, 77, 17, -9, 26, 114,
220, 299, 315, 252, 101, -121, -350, -504, -520, -375, -118, 142, 304, 323, 237, 132, 47, -57, -194, -275, -199, 25, 268, 387,
331, 163, -17, -140, -196, -228, -285, -363, -379, -232, 62, 362, 526, 522, 426, 337, 313, 365, 466, 571, 638, 609, 447, 181,
-108, -336, -420, -349, -206, -117, -148, -248, -308, -288, -205, -79, 40, 70, -17, -131, -129, 28, 256, 440, 499, 389, 128, -175,
-370, -347, -134, 140, 356, 422, 309, 76, -186, -386, -428, -280, -31, 172, 221, 102, -85, -195, -137, 48, 218, 251, 177, 104,
88, 122, 197, 304, 394, 389, 278, 151, 58, -49, -194, -309, -345, -294, -155, 40, 232, 357, 365, 275, 179, 142, 145, 161,
214, 270, 180, -91, -343, -346, -155, -59, -92, -140, -290, -360, -120, 2, -336, -706, -753, -638, -477, -292, -118, 149, 543, 795,
683, 363, 157, 149, 86, -148, -343, -371, -229, 22, 135, 50, 42, 187, 275, 240, 243, 320, 310, 198, 106, 0, -163, -295,
-319, -227, -64, 88, 158, 162, 230, 414, 580, 591, 431, 146, -213, -582, -853, -889, -653, -287, 3, 79, -77, -285, -372, -411,
-437, -380, -303, -275, -206, -26, 176, 286, 297, 293, 382, 560, 636, 506, 305, 152, 27, -75, -119, -131, -142, -133, -104, -57,
21, 121, 219, 284, 291, 245, 178, 103, -16, -212, -444, -637, -745, -780, -769, -693, -558, -437, -368, -328, -302, -296, -332, -420,
-519, -555, -471, -279, -84, -5, -65, -159, -156, -40, 78, 114, 85, 41, 7, 5, 46, 88, 72, -2, -67, -67, -28, 0,
38, 126, 235, 303, 302, 250, 181, 120, 81, 50, -2, -72, -125, -160, -198, -242, -278, -288, -255, -155, 13, 240, 482, 641,
632, 474, 279, 156, 145, 200, 240, 218, 126, -11, -144, -225, -245, -212, -130, -39, 3, -2, -10, -14, -63, -167, -274, -346,
-386, -374, -263, -52, 176, 317, 338, 282, 193, 90, -7, -73, -89, -62, -7, 71, 172, 286, 419, 578, 733, 814, 765, 595,
358, 99, -128, -259, -250, -106, 113, 306, 381, 328, 218, 130, 67, -10, -121, -256, -384, -443, -369, -162, 91, 294, 388, 354,
196, -17, -154, -110, 94, 325, 451, 441, 360, 274, 213, 165, 99, 21, -16, 24, 144, 301, 428, 473, 420, 284, 109, -39,
-108, -92, -19, 58, 98, 104, 97, 95, 135, 235, 361, 459, 479, 392, 214, 2, -161, -207, -141, -26, 66, 89, 48, -15,
-63, -58, 27, 157, 235, 194, 65, -52, -87, -45, 7, 1, -58, -91, -62, -7, 43, 88, 119, 125, 98, 35, -60, -161,
-230, -277, -335, -392, -404, -364, -298, -223, -123, 4, 111, 134, 88, 52, 58, 61, 17, -66, -153, -196, -153, -4, 217, 433,
546, 501, 334, 144, 4, -57, -42, 9, 35, -3, -99, -218, -305, -307, -213, -68, 57, 99, 32, -97, -187, -162, -55, 38,
64, 35, -4, 0, 91, 253, 407, 475, 448, 368, 274, 206, 194, 209, 183, 78, -79, -218, -276, -252, -197, -139, -56, 85,
267, 419, 461, 370, 192, 3, -165, -318, -431, -449, -348, -153, 67, 234, 303, 273, 162, 21, -73, -73, 21, 144, 186, 88,
-110, -294, -361, -313, -239, -204, -203, -175, -85, 36, 153, 275, 409, 508, 484, 309, 74, -105, -194, -219, -227, -237, -237, -202,
-116, -6, 73, 94, 67, 18, -26, -64, -121, -213, -306, -354, -345, -286, -184, -67, 23, 68, 64, 17, -43, -55, 26, 161,
265, 299, 291, 285, 294, 280, 216, 122, 30, -36, -63, -37, 32, 113, 177, 232, 302, 387, 443, 429, 329, 165, 9, -81,
-130, -190, -266, -337, -380, -349, -215, -13, 174, 273, 235, 70, -140, -295, -345, -325, -301, -294, -268, -186, -44, 120, 256, 304,
216, 12, -214, -382, -463, -466, -408, -323, -260, -246, -245, -188, -45, 138, 283, 333, 304, 262, 258, 293, 342, 379, 378, 299,
116, -136, -387, -569, -628, -549, -388, -209, -27, 137, 227, 175, -9, -203, -260, -167, -40, 13, -13, -40, 6, 97, 131, 82,
13, -34, -73, -103, -116, -137, -180, -194, -110, 60, 217, 260, 180, 69, 27, 72, 153, 214, 210, 118, -37, -188, -251, -179,
2, 204, 319, 291, 168, 64, 50, 78, 51, -70, -243, -366, -352, -202, -16, 125, 208, 233, 189, 102, 42, 37, 46, 45,
71, 133, 208, 256, 235, 142, 61, 89, 222, 354, 393, 342, 243, 130, 50, 51, 97, 107, 54, -38, -119, -129, -50, 64,
135, 124, 52, -52, -167, -236, -194, -47, 115, 203, 187, 84, -39, -107, -114, -125, -161, -170, -109, -9, 71, 111, 110, 46,
-73, -166, -150, -53, 17, 10, -19, -27, -19, 1, 55, 152, 258, 322, 334, 300, 220, 113, 26, -4, 7, 15, -29, -140,
-261, -294, -186, 17, 203, 268, 185, 2, -208, -379, -461, -405, -189, 130, 417, 546, 480, 278, 40, -138, -208, -178, -105, -42,
19, 126, 280, 422, 504, 503, 409, 246, 75, -53, -136, -189, -196, -109, 94, 349, 530, 570, 526, 498, 523, 563, 546, 441,
296, 172, 83, 18, -18, -38, -78, -154, -236, -262, -197, -81, 5, 27, 9, -7, 2, 28, 46, 43, 3, -91, -221, -315,
-297, -164, -13, 71, 102, 137, 200, 279, 371, 458, 487, 423, 302, 196, 132, 88, 57, 68, 124, 163, 117, 5, -80, -88,
-53, -43, -92, -184, -266, -273, -184, -28, 128, 214, 196, 86, -60, -169, -186, -111, 9, 114, 143, 93, 27, -1, 2, 9,
-3, -43, -90, -104, -53, 40, 104, 79, -9, -84, -96, -58, -7, 20, 14, -1, -5, -14, -37, -57, -60, -51, -26, 12,
43, 56, 84, 154, 230, 234, 132, -18, -124, -153, -144, -130, -102, -59, -18, 21, 72, 120, 139, 135, 133, 143, 143, 112,
36, -81, -199, -252, -218, -146, -80, -18, 36, 60, 57, 55, 65, 72, 52, 2, -46, -58, -34, -9, 1, 9, 10, -23,
-91, -169, -233, -275, -299, -311, -288, -204, -65, 99, 253, 342, 335, 272, 211, 175, 155, 134, 88, -4, -127, -227, -277, -284,
-259, -204, -133, -57, 26, 118, 194, 223, 188, 123, 79, 62, 26, -51, -131, -154, -102, 5, 126, 206, 225, 203, 155, 74,
-24, -104, -137, -123, -83, -37, 9, 63, 113, 118, 57, -33, -97, -110, -78, -21, 20, 17, -26, -73, -82, -50, 9, 81,
135, 132, 64, -22, -67, -54, -8, 36, 70, 91, 104, 112, 118, 110, 79, 38, 16, 25, 37, 17, -36, -87, -115, -133,
-149, -146, -111, -54, -2, 22, 22, 19, 35, 68, 100, 112, 105, 91, 77, 68, 69, 76, 78, 69, 45, 15, -5, -15,
-28, -53, -71, -62, -35, -18, -18, -19, -11, 4, 30, 70, 106, 111, 81, 44, 25, 27, 24, 3, -27, -44, -33, -6,
20, 43, 73, 112, 145, 172, 202, 228, 228, 199, 162, 133, 118, 124, 138, 130, 80, 7, -51, -83, -110, -158, -226, -282,
-290, -244, -171, -103, -55, -24, 1, 38, 84, 119, 116, 78, 46, 52, 92, 131, 132, 80, -6, -99, -169, -191, -156, -74,
25, 111, 156, 154, 118, 80, 72, 93, 108, 86, 40, 14, 28, 65, 104, 137, 156, 153, 132, 93, 21, -86, -202, -271,
-248, -139, -3, 100, 153, 162, 131, 59, -48, -158, -222, -204, -125, -38, 25, 76, 135, 203, 263, 289, 259, 172, 53, -67,
-167, -224, -226, -184, -116, -34, 35, 63, 47, 6, -28, -32, -14, -10, -31, -56, -59, -36, -12, -24, -82, -150, -173, -140,
-90, -66, -64, -43, 28, 140, 236, 263, 217, 125, 15, -84, -145, -156, -136, -114, -102, -100, -106, -113, -106, -68, 11, 111,
179, 189, 160, 123, 86, 51, 20, -7, -35, -56, -58, -32, 17, 67, 92, 70, 0, -86, -131, -108, -43, 21, 62, 77,
71, 52, 26, -3, -16, 3, 47, 85, 97, 88, 61, 14, -46, -112, -177, -235, -277, -285, -252, -192, -134, -92, -64, -43,
-24, -11, -12, -34, -75, -110, -108, -59, 15, 93, 170, 246, 297, 296, 250, 198, 167, 154, 140, 104, 42, -22, -50, -24,
32, 86, 115, 98, 33, -56, -138, -197, -222, -197, -121, -30, 29, 43, 26, -7, -47, -75, -71, -27, 32, 65, 49, -11,
-90, -156, -183, -172, -136, -77, 0, 70, 107, 103, 74, 45, 27, 10, -13, -32, -17, 45, 138, 212, 215, 134, 2, -128,
-211, -217, -141, -14, 104, 157, 129, 41, -65, -154, -196, -187, -143, -83, -15, 56, 124, 165, 165, 137, 111, 97, 85, 58,
6, -57, -100, -100, -74, -44, -13, 30, 89, 155, 210, 229, 201, 144, 83, 27, -20, -52, -74, -94, -109, -97, -47, 12,
37, 1, -70, -132, -149, -121, -80, -63, -70, -72, -49, -16, -4, -15, -26, -15, 14, 44, 57, 56, 49, 45, 47, 55,
66, 73, 69, 49, 16, -12, -18, 8, 63, 125, 170, 171, 126, 64, 34, 54, 94, 118, 125, 127, 123, 100, 62, 22,
-6, -17, -11, 4, 16, 17, 9, 0, -3, -2, -11, -35, -65, -84, -86, -69, -36, 0, 17, 14, 3, -7, -23, -42,
-57, -69, -85, -104, -113, -104, -76, -36, 7, 49, 82, 104, 113, 115, 120, 136, 156, 152, 104, 24, -41, -67, -56, -28,
-6, -3, -28, -66, -99, -115, -118, -108, -82, -36, 21, 68, 91, 81, 47, 16, 0, -8, -24, -46, -64, -68, -53, -27,
-5, 0, -14, -41, -77, -116, -139, -126, -72, -3, 49, 75, 76, 62, 45, 34, 31, 24, 1, -37, -79, -103, -93, -57,
-27, -26, -49, -77, -96, -108, -117, -125, -123, -107, -83, -72, -86, -109, -109, -79, -35, 1, 23, 21, 1, -14, -7, 17,
42, 54, 56, 56, 58, 59, 52, 31, 1, -22, -27, -10, 13, 26, 31, 38, 45, 43, 41, 54, 85, 122, 153, 170,
171, 166, 165, 164, 148, 100, 17, -73, -139, -154, -119, -55, 8, 46, 43, 1, -60, -118, -155, -159, -119, -36, 59, 124,
136, 109, 74, 57, 55, 44, 13, -17, -33, -43, -53, -48, -15, 30, 61, 51, 1, -61, -101, -97, -58, -4, 46, 75,
69, 31, -10, -23, 11, 77, 132, 139, 97, 41, 0, -27, -51, -65, -67, -57, -38, -12, 9, 20, 28, 41, 53, 61,
65, 67, 67, 64, 58, 47, 34, 19, 0, -19, -37, -60, -90, -115, -116, -80, -17, 40, 65, 48, 10, -17, -27, -34,
-58, -100, -142, -166, -152, -105, -52, -22, -17, -14, 9, 59, 118, 159, 169, 150, 106, 47, -6, -40, -57, -68, -73, -67,
-53, -31, -2, 26, 43, 39, 9, -37, -78, -91, -70, -30, 8, 24, 12, -15, -41, -56, -51, -29, -7, 8, 29, 61,
93, 102, 78, 35, -3, -27, -35, -36, -44, -69, -95, -99, -83, -70, -72, -80, -82, -73, -52, -12, 38, 85, 121, 142,
145, 129, 102, 68, 31, -1, -14, -3, 19, 28, 15, -8, -34, -60, -79, -80, -53, -2, 51, 84, 78, 34, -27, -82,
-108, -109, -98, -89, -86, -88, -83, -64, -37, -10, 12, 32, 40, 32, 14, -4, -17, -22, -20, -15, -12, -11, -1, 23,
51, 69, 74, 73, 68, 55, 37, 16, 0, -7, -9, -10, -14, -22, -31, -34, -30, -18, -6, 5, 16, 27, 31, 24,
5, -20, -44, -61, -68, -65, -63, -73, -92, -107, -104, -77, -32, 14, 47, 58, 48, 23, -5, -24, -25, -15, -6, -4,
-8, -12, -20, -32, -47, -58, -56, -39, -8, 26, 52, 64, 68, 67, 57, 39, 23, 13, 4, -7, -22, -38, -51, -53,
-42, -25, -7, 11, 29, 41, 42, 39, 36, 32, 28, 24, 24, 20, 7, -14, -37, -53, -56, -51, -45, -40, -36, -27,
-8, 18, 44, 62, 71, 77, 78, 72, 57, 37, 18, 1, -16, -32, -41, -41, -32, -20, -14, -15, -20, -23, -22, -16,
-7, -1, -7, -21, -35, -42, -47, -52, -56, -55, -46, -28, -4, 14, 22, 18, 10, 1, -5, -14, -25, -31, -22, -3,
12, 21, 31, 44, 58, 68, 68, 54, 27, 0, -25, -45, -59, -62, -51, -32, -17, -13, -15, -15, -13, -10, -4, 4,
10, 7, -2, -12, -15, -14, -8, -1, 1, -2, -11, -20, -22, -17, -12, -10, -10, -11, -9, -1, 11, 26, 37, 42,
37, 27, 14, 3, -1, 4, 19, 35, 40, 27, 4, -16, -26, -25, -15, 0, 14, 22, 21, 14, 5, 0, 0, 7,
13, 10, -5, -25, -42, -47, -40, -29, -24, -27, -32, -33, -28, -16, -4, 0, -3, -11, -18, -16, -3, 15, 34, 48,
55, 52, 37, 19, 9, 10, 17, 20, 15, 6, -5, -12, -11, -6, -6, -13, -21, -26, -28, -24, -14, -3, 5, 12,
17, 19, 14, 5, -1, -7, -12, -18, -21, -16, -7, 3, 11, 11, 7, 2, -1, -5, -6, -4, -1, -1, -2, -1,
0, 0, -2, -6, -9, -13, -19, -26, -27, -21, -9, 1, 9, 14, 19, 23, 22, 19, 15, 13, 12, 15, 19, 20,
17, 14, 11, 7, 3, -2, -7, -10, -10, -8, -5, 0, 8, 17, 25, 27, 24, 14, 3, -4, -8, -10, -10, -9,
-4, 2, 7, 7, 1, -6, -14, -19, -18, -8, 4, 15, 21, 22, 18, 13, 7, 1, -2, -4, -2, 1, 0, -4,
-10, -14, -17, -19, -18, -13, -4, 4, 7, 5, 1, -1, -3, -5, -6, -3, 3, 10, 15, 18, 19, 18, 16, 14,
11, 5, -3, -10, -12, -10, -6, -1, 2, 4, 4, 0, -4, -6, -3, 4, 16, 25, 27, 23, 13, 3, -4, -7,
-8, -9, -10, -10, -9, -9, -9, -8, -6, -4, -2, 0, 3, 6, 8, 10, 10, 9, 9, 10, 12, 12, 8, 1,
-4, -8, -9, -10, -11, -10, -7, -3, 1, 4, 6, 5, 2, -2, -5, -8, -7, -4, 0, 0, 1, 3, 3, 3,
0, -5, -11, -15, -18, -18, -12, -5, 0, 3, 4, 4, 5, 7, 8, 9, 8, 6, 3, 2, 1, 0, -2, -5,
-8, -10, -11, -11, -9, -6, -3, -1, 0, 1, 5, 7, 8, 4, 0, -4, -6, -7, -7, -6, -2, 2, 8, 13,
16, 14, 10, 6, 5, 5, 7, 8, 8, 6, 5, 5, 7, 9, 8, 4, 0, -5, -7, -8, -6, -3, 0, 3,
4, 2, -1, -3, -6, -7, -8, -9, -9, -8, -6, -4, 0, 4, 9, 12, 13, 12, 10, 8, 6, 5, 5, 7,
7, 6, 4, 2, 0, -2, -4, -7, -9, -11, -9, -4, 0, 3, 3, 2, 1, 2, 3, 3, 0, -3, -5, -6,
-4, 0, 3, 5, 5, 4, 3, 3, 3, 3, 3, 3, 2, 2, 1, 0, 0, -1, -1, 0, 0, 1, 1, 2,
5, 8, 9, 9, 6, 3, 0, -2, -3, -3, -1, 1, 2, 3, 1, 0, -2, -2, -1, -1, -1, -1, -2, -3,
-3, -2, 0, 0, 1, 1, 1, 0, 0, 0, 0, 2, 3, 2, 0, 0, -1, 0, 0, -1, -2, -3, -3, -2,
0, 2, 3, 2, 0, 0, 0, 1, 2, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 1,
1, 1, 0, 0, -1, -2, -2, -2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// ---------------------------------------------------------------- sound (explosion + fanfare)
struct Sfx { std::vector<float> data; int rate = 22050; };
static Sfx g_sfxBoom, g_sfxTada;

struct Voice { const Sfx* sfx = nullptr; double pos = 0; float gain = 1; };
static Voice g_voices[6];
static SDL_AudioDeviceID g_audioDev = 0;
static int g_audioRate = 44100;

// Victory fanfare "Ta - da-da-da-da-da-da - DAAA!": a brass stab, a quick rising run, and a big
// ringing final chord with glitter (synthesized at start-up, about 3.3 seconds).
static void makeFanfare(Sfx& s) {
    const int R = 22050;
    s.rate = R;
    const double TOTAL = 3.3, FINAL = 1.08;           // length / start of the final chord
    const int n = (int)(TOTAL * R);
    s.data.assign(n, 0.0f);
    uint32_t seed = 12345u;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 8388608.0f - 1.0f; };
    const double TWO_PI = 6.283185307179586;
    auto brass = [&](double f, double t0, double len, double tau, float gain, int harmonics) {
        const int i0 = (int)(t0 * R), cnt = (int)(len * R);
        const double rel = std::min(0.08, len * 0.35);
        for (int i = 0; i < cnt && i0 + i < n; ++i) {
            double t = (double)i / R;
            double env = std::min(1.0, t / 0.012) * std::exp(-t / tau);
            if (t > len - rel) env *= (len - t) / rel;
            double bright = 0.3 + 0.7 * std::exp(-t / 0.4);
            double vib = 0.03 * std::sin(TWO_PI * 5.5 * t) * std::min(1.0, std::max(0.0, (t - 0.35) / 0.3));
            double sum = 0;
            for (int k = 1; k <= harmonics && f * k < 7500; ++k)
                sum += std::pow((double)k, -0.8) * std::exp(-(k - 1) * (1.25 - bright) * 0.4) *
                       std::sin(k * (TWO_PI * f * t + vib));
            s.data[i0 + i] += (float)(sum * env * gain);
        }
    };
    // "Ta": a short brass stab
    for (double f : {392.00, 523.25, 659.25}) brass(f, 0.0, 0.16, 0.30, 0.20f, 8);
    brass(261.63, 0.0, 0.16, 0.30, 0.16f, 6);
    // "da-da-da-da-da-da": a quick rising run
    const double run[6] = {392.00, 523.25, 659.25, 783.99, 659.25, 783.99};
    for (int i = 0; i < 6; ++i) {
        double t0 = 0.24 + i * 0.125;
        double len = (i == 5) ? 0.17 : 0.11;
        brass(run[i], t0, len, 0.4, 0.26f, 8);
        brass(run[i] * 0.5, t0, len, 0.4, 0.10f, 4);            // an octave lower for body
    }
    // a quick stab leading into the finale
    for (double f : {783.99, 1046.50}) brass(f, 0.96, 0.10, 0.3, 0.14f, 6);
    // "DAAA!": the big final chord (each note also has a slightly detuned copy for width)
    for (double f : {523.25, 659.25, 783.99, 1046.50}) {
        brass(f, FINAL, TOTAL - FINAL - 0.05, 1.0, 0.17f, 8);
        brass(f * 1.004, FINAL, TOTAL - FINAL - 0.05, 1.0, 0.08f, 4);
    }
    brass(261.63, FINAL, TOTAL - FINAL - 0.05, 1.0, 0.22f, 8);
    brass(130.81, FINAL, TOTAL - FINAL - 0.05, 1.0, 0.12f, 4);
    // glitter after the finale hits
    for (int p = 0; p < 26; ++p) {
        double t0 = FINAL + 0.05 + 1.6 * (rnd() * 0.5 + 0.5), f = 2400 + 3600 * (rnd() * 0.5 + 0.5);
        double tau = 0.05 + 0.07 * (rnd() * 0.5 + 0.5);
        int i0 = (int)(t0 * R);
        for (int i = 0; i < (int)(0.4 * R) && i0 + i < n; ++i) {
            double t = (double)i / R;
            s.data[i0 + i] += (float)(0.045 * std::sin(TWO_PI * f * t) * std::exp(-t / tau) * std::min(1.0, t / 0.003));
        }
    }
    // soft cymbal swell under the finale
    float prev = 0;
    for (int i = (int)(FINAL * R); i < n; ++i) {
        float w = rnd();
        float hp = w - prev; prev = w;
        double t = (double)i / R - FINAL;
        s.data[i] += (float)(hp * 0.07 * std::exp(-t / 0.5) * std::min(1.0, t / 0.01));
    }
    float pk = 0;
    for (float v : s.data) pk = std::max(pk, std::fabs(v));
    const int fade = (int)(0.12 * R);
    for (int i = 0; i < n; ++i) {
        float g = 0.8f / std::max(pk, 1e-6f);
        if (i > n - fade) g *= (float)(n - i) / fade;
        s.data[i] *= g;
    }
}

static void SDLCALL audioCallback(void*, Uint8* stream, int len) {
    float* out = (float*)stream;
    const int frames = len / (int)(2 * sizeof(float));
    std::memset(stream, 0, len);
    for (int v = 0; v < 6; ++v) {
        Voice& vo = g_voices[v];
        if (!vo.sfx) continue;
        const std::vector<float>& d = vo.sfx->data;
        const double step = (double)vo.sfx->rate / g_audioRate;
        for (int i = 0; i < frames; ++i) {
            int ip = (int)vo.pos;
            if (ip + 1 >= (int)d.size()) { vo.sfx = nullptr; break; }
            float f = (float)(vo.pos - ip);
            float x = (d[ip] * (1.0f - f) + d[ip + 1] * f) * vo.gain;
            out[i * 2] += x;
            out[i * 2 + 1] += x;
            vo.pos += step;
        }
    }
    for (int i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i]);   // soft limiter
}

static void audioInit() {
    g_sfxBoom.rate = EXPLOSION_RATE;
    g_sfxBoom.data.resize(sizeof(g_explosion) / sizeof(g_explosion[0]));
    for (size_t i = 0; i < g_sfxBoom.data.size(); ++i) g_sfxBoom.data[i] = g_explosion[i] / 32768.0f;
    makeFanfare(g_sfxTada);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "Audio unavailable (the game runs silent): %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCallback;
    g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // SDL converts to what the device needs
    if (!g_audioDev) {
        std::fprintf(stderr, "No audio device (the game runs silent): %s\n", SDL_GetError());
        return;
    }
    g_audioRate = have.freq;
    SDL_PauseAudioDevice(g_audioDev, 0);
}

static void audioPlay(const Sfx& sfx, float gain) {
    if (!g_audioDev) return;
    SDL_LockAudioDevice(g_audioDev);
    int slot = 0;
    for (int i = 0; i < 6; ++i) {
        if (!g_voices[i].sfx) { slot = i; break; }
        if (g_voices[i].pos > g_voices[slot].pos) slot = i;     // none free: replace the oldest
    }
    g_voices[slot].sfx = &sfx;
    g_voices[slot].pos = 0;
    g_voices[slot].gain = gain;
    SDL_UnlockAudioDevice(g_audioDev);
}

static void audioPlayExplosion() { audioPlay(g_sfxBoom, 0.9f); }   // a player crashed
static void audioPlayTada() { audioPlay(g_sfxTada, 0.9f); }        // somebody won the match

static void audioShutdown() {
    if (g_audioDev) { SDL_CloseAudioDevice(g_audioDev); g_audioDev = 0; }
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

    audioInit();                  // sound effect (the game runs silent without a sound device)
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
                } else if (g_state == MENU && k == SDLK_DOWN) {
                    addCpu();
                } else if (g_state == MENU && k == SDLK_UP) {
                    removeCpu();
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
                } else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN || b == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                    if (g_state == MENU) { if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) addCpu(); else removeCpu(); }
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
    audioShutdown();
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
