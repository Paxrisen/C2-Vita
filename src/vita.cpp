// civ2vita — PS-Vita-Plattform: Framebuffer, Eingabe (Touch/Sticks/Tasten), Bildschirmtastatur, Texteingabe
#include "win.h"
#include <deque>
#include <cmath>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

int SCR_W = 960, SCR_H = 544;
uint32_t* g_screen = nullptr;
volatile bool g_screen_dirty = true;
int g_mouse_x = 480, g_mouse_y = 272;
bool g_lbutton = false, g_rbutton = false;
uint8_t g_keystate[256];
static void* g_fb[2]; static int g_fbi = 0;
static bool g_cursor_on = true;
static int g_last_cx = -1, g_last_cy = -1;
static std::deque<InputEv> g_ev;
int g_lang = -1;
static float g_fx = 480, g_fy = 272;

uint64_t now_us() { return sceKernelGetProcessTimeWide(); }
uint32_t now_ms() { return (uint32_t)(sceKernelGetProcessTimeWide() / 1000); }
void plat_sleep_ms(uint32_t ms) { sceKernelDelayThread(ms * 1000); }
void plat_draw_cursor(bool on) { g_cursor_on = on; g_screen_dirty = true; }
static CursorImg g_ccur; static bool g_has_ccur = false;
void plat_set_cursor(const CursorImg* c) { if (c) { g_ccur = *c; g_has_ccur = true; } else g_has_ccur = false; g_screen_dirty = true; }

void plat_init() {
    scePowerSetArmClockFrequency(444); scePowerSetBusClockFrequency(222); scePowerSetGpuClockFrequency(222); scePowerSetGpuXbarClockFrequency(166);
    const uint32_t fbsz = 0x200000;
    for (int i = 0; i < 2; i++) {
        SceUID b = sceKernelAllocMemBlock("civ2_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, fbsz, nullptr);
        if (b < 0) b = sceKernelAllocMemBlock("civ2_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, fbsz, nullptr);
        sceKernelGetMemBlockBase(b, &g_fb[i]);
        memset(g_fb[i], 0, fbsz);
    }
    g_screen = (uint32_t*)malloc(SCR_W * SCR_H * 4);
    for (int i = 0; i < SCR_W * SCR_H; i++) g_screen[i] = 0xFF000000u;
    SceDisplayFrameBuf fb; memset(&fb, 0, sizeof fb); fb.size = sizeof fb; fb.base = g_fb[0]; fb.pitch = SCR_W; fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; fb.width = SCR_W; fb.height = SCR_H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    memset(g_keystate, 0, sizeof g_keystate);
    g_keystate[0x90] = 0;
    { SceAppUtilInitParam ip; SceAppUtilBootParam bp; memset(&ip, 0, sizeof ip); memset(&bp, 0, sizeof bp); sceAppUtilInit(&ip, &bp);
      int l = 0; if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &l) == 0) g_lang = (l == SCE_SYSTEM_PARAM_LANG_GERMAN) ? 0 : 1; else g_lang = 0; }  // NumLock aus -> Ziffernblock-Richtungen als Pfeil/Home/End usw.
}

// Mauszeiger (Pfeil, 12x19): 1=schwarz 2=weiss
static const char* kArrow[19] = {
 "1           ","11          ","121         ","1221        ","12221       ","122221      ","1222221     ","12222221    ",
 "122222221   ","1222222221  ","12222211111 ","1221221     ","121 1221    ","11  1221    ","1    1221   ","     1221   ",
 "      1221  ","      1221  ","       11   " };
void plat_present(bool force) {
    bool moved = g_mouse_x != g_last_cx || g_mouse_y != g_last_cy;
    if (!force && !g_screen_dirty && !moved) return;
    g_screen_dirty = false; g_last_cx = g_mouse_x; g_last_cy = g_mouse_y;
    g_fbi ^= 1; uint32_t* fb = (uint32_t*)g_fb[g_fbi];
    memcpy(fb, g_screen, SCR_W * SCR_H * 4);
    if (g_cursor_on && g_has_ccur) for (int y = 0; y < g_ccur.h; y++) for (int x = 0; x < g_ccur.w; x++) {
        uint32_t v = g_ccur.px[y * g_ccur.w + x]; if (!v) continue;
        int X = g_mouse_x - g_ccur.hx + x, Y = g_mouse_y - g_ccur.hy + y; if (X < 0 || Y < 0 || X >= SCR_W || Y >= SCR_H) continue;
        fb[Y * SCR_W + X] = v == 1 ? (fb[Y * SCR_W + X] ^ 0x00FFFFFFu) : v;
    }
    else if (g_cursor_on) for (int y = 0; y < 19; y++) for (int x = 0; x < 12; x++) {
        char ch = kArrow[y][x]; if (ch == ' ') continue;
        int X = g_mouse_x + x, Y = g_mouse_y + y; if (X < 0 || Y < 0 || X >= SCR_W || Y >= SCR_H) continue;
        fb[Y * SCR_W + X] = ch == '1' ? 0xFF000000u : 0xFFFFFFFFu;
    }
    SceDisplayFrameBuf d; memset(&d, 0, sizeof d); d.size = sizeof d; d.base = fb; d.pitch = SCR_W; d.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; d.width = SCR_W; d.height = SCR_H;
    sceDisplaySetFrameBuf(&d, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

static void push_key(int vk, bool down) {
    if (down) { if (vk == 0x14 || vk == 0x90 || vk == 0x91) g_keystate[vk] ^= 1; g_keystate[vk] |= 0x80; }
    else g_keystate[vk] &= ~0x80;
    InputEv e{}; e.type = EV_KEY; e.key = vk; e.down = down; g_ev.push_back(e);
}
static void tap_key(int vk) { push_key(vk, true); push_key(vk, false); }
static void key_combo(int v, bool down) {   // v = vk | MK_SHIFT 0x100 | MK_CTRL 0x200 | MK_ALT 0x400
    int vk = v & 0xFF;
    if (down) { if (v & 0x200) push_key(0x11, true); if (v & 0x100) push_key(0x10, true); if (v & 0x400) push_key(0x12, true); push_key(vk, true); }
    else { push_key(vk, false); if (v & 0x400) push_key(0x12, false); if (v & 0x100) push_key(0x10, false); if (v & 0x200) push_key(0x11, false); }
}
// Tastendruck mit verzoegertem Loslassen: das Spiel fragt Strg/Shift (GetKeyState, Kurzbefehle) erst bei der
// Verarbeitung der Nachricht ab - die Tasten muessen dann noch gedrueckt sein.
static std::vector<std::pair<uint32_t, int>> g_pend_up;
static void key_tap_later(int v) { key_combo(v, true); g_pend_up.push_back({now_ms() + 150, v}); }
static void pend_poll() {
    uint32_t t = now_ms();
    for (size_t i = 0; i < g_pend_up.size();) { if ((int32_t)(t - g_pend_up[i].first) >= 0) { key_combo(g_pend_up[i].second, false); g_pend_up.erase(g_pend_up.begin() + i); } else i++; }
}
static void push_mouse(int type) {
    InputEv e{}; e.type = type; e.x = g_mouse_x; e.y = g_mouse_y;
    if (type == EV_LDOWN) g_lbutton = true; if (type == EV_LUP) g_lbutton = false;
    if (type == EV_RDOWN) g_rbutton = true; if (type == EV_RUP) g_rbutton = false;
    g_ev.push_back(e);
}
bool plat_next_event(InputEv& e) { if (g_ev.empty()) return false; e = g_ev.front(); g_ev.pop_front(); return true; }


extern const unsigned g_native_count;
// ------------------------------------------------------------ Steuerung: Belegung (zweisprachig)
enum { A_NONE, A_LMB, A_RMB, A_OSK, A_MENU, A_KEY, A_CMD };
#define MK_SHIFT 0x100
#define MK_CTRL 0x200
struct Opt { const char* de; const char* en; int act; int vk; const char* lab() const { return TR(de, en); } };
static std::vector<Opt> g_opts;
static void build_opts() {
    if (!g_opts.empty()) return;
    g_opts = { {"Nichts", "Nothing", A_NONE, 0}, {"Linksklick", "Left click", A_LMB, 0}, {"Rechtsklick", "Right click", A_RMB, 0},
        {"Bildschirmtastatur", "On-screen keyboard", A_OSK, 0}, {"Einstellungen", "Settings", A_MENU, 0}, {"Schnellbefehle", "Quick commands", A_CMD, 0},
        {"Runde beenden (Strg+N)", "End turn (Ctrl+N)", A_KEY, MK_CTRL | 'N'}, {"Runde ueberspringen", "Skip turn (Space)", A_KEY, 0x20},
        {"Warten (W)", "Wait (W)", A_KEY, 'W'}, {"Befestigen (F)", "Fortify (F)", A_KEY, 'F'}, {"Schlafen (S)", "Sleep (S)", A_KEY, 'S'},
        {"Stadt bauen (B)", "Build city (B)", A_KEY, 'B'}, {"Strasse bauen (R)", "Build road (R)", A_KEY, 'R'}, {"Bewaesserung (I)", "Irrigate (I)", A_KEY, 'I'},
        {"Bergwerk (M)", "Mine (M)", A_KEY, 'M'}, {"Ansicht zentrieren (C)", "Center view (C)", A_KEY, 'C'}, {"Gehen zu (G)", "Go to (G)", A_KEY, 'G'},
        {"Vergroessern (Z)", "Zoom in (Z)", A_KEY, 'Z'}, {"Verkleinern (X)", "Zoom out (X)", A_KEY, 'X'},
        {"Spiel speichern (Strg+S)", "Save game (Ctrl+S)", A_KEY, MK_CTRL | 'S'},
        {"Enter", "Enter", A_KEY, 0x0D}, {"Esc", "Esc", A_KEY, 0x1B}, {"Tab", "Tab", A_KEY, 0x09}, {"Ruecktaste", "Backspace", A_KEY, 0x08}, {"Entf", "Delete", A_KEY, 0x2E},
        {"Shift (halten)", "Shift (hold)", A_KEY, 0x10}, {"Strg (halten)", "Ctrl (hold)", A_KEY, 0x11}, {"Alt (halten)", "Alt (hold)", A_KEY, 0x12},
        {"Pos1", "Home", A_KEY, 0x24}, {"Ende", "End", A_KEY, 0x23}, {"Bild auf", "Page up", A_KEY, 0x21}, {"Bild ab", "Page down", A_KEY, 0x22},
        {"Pfeil hoch", "Arrow up", A_KEY, 0x26}, {"Pfeil runter", "Arrow down", A_KEY, 0x28}, {"Pfeil links", "Arrow left", A_KEY, 0x25}, {"Pfeil rechts", "Arrow right", A_KEY, 0x27} };
    static char fk[12][4]; for (int i = 0; i < 12; i++) { snprintf(fk[i], 4, "F%d", i + 1); g_opts.push_back({fk[i], fk[i], A_KEY, 0x70 + i}); }
    static char lt[36][2]; for (int i = 0; i < 26; i++) { lt[i][0] = 'A' + i; lt[i][1] = 0; g_opts.push_back({lt[i], lt[i], A_KEY, 'A' + i}); }
    for (int i = 0; i < 10; i++) { lt[26 + i][0] = '0' + i; lt[26 + i][1] = 0; g_opts.push_back({lt[26 + i], lt[26 + i], A_KEY, '0' + i}); }
}
static const char* kBtnDe[8] = { "Kreuz (X)", "Kreis (O)", "Quadrat", "Dreieck", "Start", "Select", "L-Taste", "R-Taste" };
static const char* kBtnEn[8] = { "Cross (X)", "Circle (O)", "Square", "Triangle", "Start", "Select", "L button", "R button" };
static const uint32_t kBtnMask[8] = { SCE_CTRL_CROSS, SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE, SCE_CTRL_TRIANGLE, SCE_CTRL_START, SCE_CTRL_SELECT, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER };
struct Cfg { int btn[8]; int dpad, lstick, rstick, speed, touch, volume, nativ, lang; };
static Cfg g_cfg;
int g_volume = 70;
static int g_sys_lang = 0;
static int opt_find(int act, int vk) { build_opts(); for (size_t i = 0; i < g_opts.size(); i++) if (g_opts[i].act == act && g_opts[i].vk == vk) return i; return 0; }
static void cfg_default() {
    // X Bewaesserung, O Befestigen, Quadrat Strasse, Dreieck Stadt bauen, Start Enter, Select Schnellbefehle, L/R Maustasten
    g_cfg.btn[0] = opt_find(A_KEY, 'I'); g_cfg.btn[1] = opt_find(A_KEY, 'F'); g_cfg.btn[2] = opt_find(A_KEY, 'R'); g_cfg.btn[3] = opt_find(A_KEY, 'B');
    g_cfg.btn[4] = opt_find(A_KEY, 0x0D); g_cfg.btn[5] = opt_find(A_CMD, 0); g_cfg.btn[6] = opt_find(A_LMB, 0); g_cfg.btn[7] = opt_find(A_RMB, 0);
    g_cfg.dpad = 0; g_cfg.lstick = 0; g_cfg.rstick = 1; g_cfg.speed = 5; g_cfg.touch = 0; g_cfg.volume = 70; g_cfg.nativ = 3; g_cfg.lang = g_sys_lang;
}
static void cfg_save() {
    FILE* f = fopen((g_root + "/steuerung.ini").c_str(), "w"); if (!f) return;
    fprintf(f, "version=4\n");
    for (int i = 0; i < 8; i++) fprintf(f, "btn%d=%d:%d\n", i, g_opts[g_cfg.btn[i]].act, g_opts[g_cfg.btn[i]].vk);
    fprintf(f, "dpad=%d\nlstick=%d\nrstick=%d\nspeed=%d\ntouch=%d\nvolume=%d\nnativ=%d\nlang=%d\n", g_cfg.dpad, g_cfg.lstick, g_cfg.rstick, g_cfg.speed, g_cfg.touch, g_cfg.volume, g_cfg.nativ, g_cfg.lang);
    fclose(f);
}
static void cfg_load() {
    g_sys_lang = g_lang < 0 ? 0 : g_lang;
    build_opts(); cfg_default();
    FILE* f = fopen((g_root + "/steuerung.ini").c_str(), "r");
    int ver = 1, btns[8]; bool have[8] = {};
    if (f) { char line[128]; while (fgets(line, sizeof line, f)) {
        int i, a, v;
        if (sscanf(line, "version=%d", &v) == 1) ver = v;
        if (sscanf(line, "btn%d=%d:%d", &i, &a, &v) == 3 && i >= 0 && i < 8) { btns[i] = opt_find(a, v); have[i] = true; }
        sscanf(line, "dpad=%d", &g_cfg.dpad); sscanf(line, "lstick=%d", &g_cfg.lstick); sscanf(line, "rstick=%d", &g_cfg.rstick);
        sscanf(line, "speed=%d", &g_cfg.speed); sscanf(line, "touch=%d", &g_cfg.touch); sscanf(line, "volume=%d", &g_cfg.volume);
        sscanf(line, "nativ=%d", &g_cfg.nativ); sscanf(line, "lang=%d", &g_cfg.lang); }
        fclose(f); }
    if (ver >= 3) for (int i = 0; i < 8; i++) if (have[i]) g_cfg.btn[i] = btns[i];
    if (ver < 3) { cfg_default(); }   // alte Belegungen -> neue Standardbelegung
    if (g_cfg.lang < 0 || g_cfg.lang > 1) g_cfg.lang = g_sys_lang;
    g_lang = g_cfg.lang;
    g_volume = g_cfg.volume;
}

// ------------------------------------------------------------ Mauszeiger (Sticks, zeitbasiert) — im Spiel und in allen Menues
static bool pointer_sticks(const SceCtrlData& pad, bool game) {
    static uint64_t last_us = 0; uint64_t now = now_us(); float dt = last_us ? (now - last_us) / 1e6f : 0; last_us = now;
    if (dt > 0.05f) dt = 0.05f;
    float maxv = 60.0f + g_cfg.speed * 60.0f;   // Stufe 1 = 120 px/s ... Stufe 10 = 660 px/s
    bool moved = false;
    auto move = [&](float dx, float dy) {
        g_fx += dx; g_fy += dy;
        g_fx = std::max(0.0f, std::min((float)SCR_W - 1, g_fx)); g_fy = std::max(0.0f, std::min((float)SCR_H - 1, g_fy));
        if ((int)g_fx != g_mouse_x || (int)g_fy != g_mouse_y) { g_mouse_x = g_fx; g_mouse_y = g_fy; moved = true; if (game) push_mouse(EV_MOUSEMOVE); } };
    auto stick = [&](int sx, int sy, float scale) {
        float ax = (sx - 128) / 127.0f, ay = (sy - 128) / 127.0f;
        float mag = sqrtf(ax * ax + ay * ay); const float dz = 0.22f;
        if (mag <= dz) return;
        float m = std::min(1.0f, (mag - dz) / (1.0f - dz)); m = m * m * m * 0.7f + m * 0.3f;   // feine Kontrolle bei kleinem Ausschlag
        float v = m * maxv * scale * dt;
        move(ax / mag * v, ay / mag * v); };
    // im Spiel nach Belegung; in Menues bewegen beide Sticks immer den Zeiger (ausser "Aus")
    if (game ? g_cfg.lstick == 0 : g_cfg.lstick != 2) stick(pad.lx, pad.ly, 1.0f);
    if (game ? g_cfg.rstick == 1 : g_cfg.rstick != 2) stick(pad.rx, pad.ry, 0.45f);   // rechter Stick: langsamer fuer genaues Zielen
    if (game && g_cfg.dpad == 2) { uint32_t b = pad.buttons; float dx = ((b & SCE_CTRL_RIGHT) ? 1 : 0) - ((b & SCE_CTRL_LEFT) ? 1 : 0), dy = ((b & SCE_CTRL_DOWN) ? 1 : 0) - ((b & SCE_CTRL_UP) ? 1 : 0); if (dx || dy) move(dx * maxv * 0.6f * dt, dy * maxv * 0.6f * dt); }
    return moved;
}
// Menue-Eingabe: Zeiger bewegen, Klick (Linksklick-Taste) oder Antippen melden.  hover = Zeiger wurde bewegt
static bool menu_pointer(const SceCtrlData& pad, uint32_t nb, int& prevTouch, int& x, int& y, bool& hover) {
    g_cursor_on = true;
    hover = pointer_sticks(pad, false);
    x = g_mouse_x; y = g_mouse_y;
    bool tap = false;
    for (int i = 0; i < 8; i++) if ((nb & kBtnMask[i]) && g_opts[g_cfg.btn[i]].act == A_LMB) tap = true;
    SceTouchData t; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
    if (t.reportNum > 0 && !prevTouch) { x = t.report[0].x / 2; y = t.report[0].y / 2; g_mouse_x = x; g_mouse_y = y; g_fx = x; g_fy = y; tap = true; }
    prevTouch = t.reportNum;
    return tap;
}
// Zeiger-Klicktasten im Menue (fuer "nicht als Menuetaste werten")
static uint32_t lmb_mask() { uint32_t m = 0; for (int i = 0; i < 8; i++) if (g_opts[g_cfg.btn[i]].act == A_LMB) m |= kBtnMask[i]; return m; }
struct MenuGuard { bool cur; MenuGuard() : cur(g_cursor_on) {} ~MenuGuard() { g_cursor_on = cur; g_screen_dirty = true; push_mouse(EV_MOUSEMOVE); } };

// ------------------------------------------------------------ Bildschirmtastatur (eine Taste senden)
struct Key { const char* de; const char* en; int vk; int w; };
static const Key kRows[6][13] = {
 { {"Esc","Esc",0x1B,1},{"F1","F1",0x70,1},{"F2","F2",0x71,1},{"F3","F3",0x72,1},{"F4","F4",0x73,1},{"F5","F5",0x74,1},{"F6","F6",0x75,1},{"F7","F7",0x76,1},{"F8","F8",0x77,1},{"F9","F9",0x78,1},{"F10","F10",0x79,1},{"F11","F11",0x7A,1},{"F12","F12",0x7B,1} },
 { {"1","1",'1',1},{"2","2",'2',1},{"3","3",'3',1},{"4","4",'4',1},{"5","5",'5',1},{"6","6",'6',1},{"7","7",'7',1},{"8","8",'8',1},{"9","9",'9',1},{"0","0",'0',1},{"-","-",0xBD,1},{"=","=",0xBB,1},{"<-","<-",0x08,1} },
 { {"Tab","Tab",0x09,1},{"Q","Q",'Q',1},{"W","W",'W',1},{"E","E",'E',1},{"R","R",'R',1},{"T","T",'T',1},{"Z","Z",'Z',1},{"U","U",'U',1},{"I","I",'I',1},{"O","O",'O',1},{"P","P",'P',1},{"Entf","Del",0x2E,1},{"Einf","Ins",0x2D,1} },
 { {"Shift","Shift",0x10,1},{"A","A",'A',1},{"S","S",'S',1},{"D","D",'D',1},{"F","F",'F',1},{"G","G",'G',1},{"H","H",'H',1},{"J","J",'J',1},{"K","K",'K',1},{"L","L",'L',1},{"Enter","Enter",0x0D,2},{nullptr,nullptr,0,0} },
 { {"Strg","Ctrl",0x11,1},{"Y","Y",'Y',1},{"X","X",'X',1},{"C","C",'C',1},{"V","V",'V',1},{"B","B",'B',1},{"N","N",'N',1},{"M","M",'M',1},{",",",",0xBC,1},{".",".",0xBE,1},{"Pos1","Home",0x24,1},{"Ende","End",0x23,1},{nullptr,nullptr,0,0} },
 { {"Alt","Alt",0x12,1},{"Leertaste","Space",0x20,4},{"Bild^","PgUp",0x21,1},{"Bildv","PgDn",0x22,1},{"<","<",0x25,1},{"^","^",0x26,1},{"v","v",0x28,1},{">",">",0x27,1},{"Num5","Num5",0x0C,1},{nullptr,nullptr,0,0} } };
static void osk_run() {
    MenuGuard mg;
    const int kw = 70, kh = 38, ox = (SCR_W - 13 * kw) / 2, oy = SCR_H - 6 * kh - 8;
    Rect area{ox - 4, oy - 26, ox + 13 * kw + 4, oy + 6 * kh + 4};
    ScreenSave sv; screen_save(sv, area);
    int sr = 3, sc = 1; bool mods[3] = {false, false, false};   // shift ctrl alt
    struct Cell { int r, c; Rect rc; };
    std::vector<Cell> cells;
    for (int r = 0; r < 6; r++) { int x = ox; for (int c = 0; c < 13 && kRows[r][c].de; c++) { cells.push_back({r, c, {x + 1, oy + r * kh + 1, x + kRows[r][c].w * kw - 1, oy + (r + 1) * kh - 1}}); x += kRows[r][c].w * kw; } }
    auto draw = [&]() {
        scr_fill(area, 0x303030); scr_text({area.l, area.t, area.r, area.t + 24}, TR("Tastatur: X / Klick = Taste senden, O / Dreieck = schliessen", "Keyboard: X / click = send key, O / Triangle = close"), 0xFFFFFF, 1);
        for (auto& ce : cells) { const Key& k = kRows[ce.r][ce.c]; bool sel = ce.r == sr && ce.c == sc;
            bool on = (k.vk == 0x10 && mods[0]) || (k.vk == 0x11 && mods[1]) || (k.vk == 0x12 && mods[2]);
            scr_fill(ce.rc, sel ? 0x800000 : on ? 0x008000 : 0xC0C0C0); scr_frame3d(ce.rc, true);
            scr_text(ce.rc, TR(k.de, k.en), sel || on ? 0xFFFFFF : 0, 1); } };
    draw(); plat_present(true);
    SceCtrlData pad; uint32_t prev = 0xFFFFFFFF; bool done = false; int prevTouch = 1;
    while (true) { sceCtrlPeekBufferPositive(0, &pad, 1); if (!(pad.buttons & (SCE_CTRL_TRIANGLE | SCE_CTRL_START | SCE_CTRL_SELECT))) break; sceKernelDelayThread(10000); }
    prev = pad.buttons;
    auto press = [&](const Key& k) {
        if (k.vk == 0x10) { mods[0] = !mods[0]; draw(); return; }
        if (k.vk == 0x11) { mods[1] = !mods[1]; draw(); return; }
        if (k.vk == 0x12) { mods[2] = !mods[2]; draw(); return; }
        key_tap_later(k.vk | (mods[0] ? 0x100 : 0) | (mods[1] ? 0x200 : 0) | (mods[2] ? 0x400 : 0));
        done = true; };
    uint32_t lm = lmb_mask();
    while (!done) {
        sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t b = pad.buttons, nb = b & ~prev; prev = b;
        int rowlen = 0; while (rowlen < 13 && kRows[sr][rowlen].de) rowlen++;
        int px, py; bool hover;
        bool tap = menu_pointer(pad, nb, prevTouch, px, py, hover);
        if (hover || tap) for (auto& ce : cells) if (ce.rc.has(px, py) && (ce.r != sr || ce.c != sc)) { sr = ce.r; sc = ce.c; draw(); break; }
        if (tap) { if (!area.has(px, py)) break; for (auto& ce : cells) if (ce.rc.has(px, py)) { press(kRows[ce.r][ce.c]); break; } continue; }
        nb &= ~lm;
        if (nb & SCE_CTRL_LEFT) { sc = (sc + rowlen - 1) % rowlen; draw(); }
        if (nb & SCE_CTRL_RIGHT) { sc = (sc + 1) % rowlen; draw(); }
        if (nb & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { sr = (sr + ((nb & SCE_CTRL_UP) ? 5 : 1)) % 6; int rl = 0; while (rl < 13 && kRows[sr][rl].de) rl++; if (sc >= rl) sc = rl - 1; draw(); }
        if (nb & SCE_CTRL_CROSS) press(kRows[sr][sc]);
        if (nb & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_START)) break;
        plat_present(); sceKernelDelayThread(16000);
    }
    screen_restore(sv); plat_present(true);
    for (int i = 0; i < 50; i++) { SceTouchData t; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1); sceCtrlPeekBufferPositive(0, &pad, 1); if (!t.reportNum && !(pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | lm))) break; sceKernelDelayThread(10000); }
}

// ------------------------------------------------------------ Texteingabe (eigene Tastatur, Codepage 1252)
int plat_ime(const char* title, const char* initial, char* out, int maxlen) {
    MenuGuard mg;
    static const char* rowsL[5] = { "1234567890-ß", "qwertzuiopü+", "asdfghjklöä#", "yxcvbnm,.-'!", "" };
    static const char* rowsU[5] = { "!\"§$%&/()=?_", "QWERTZUIOPÜ*", "ASDFGHJKLÖÄ'", "YXCVBNM;:_`?", "" };
    auto to1252 = [](const char* s) { std::string o; for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
        if (*p < 0x80) o += *p; else if ((*p & 0xE0) == 0xC0 && p[1]) { o += (char)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F)); p++; } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { o += '?'; p += 2; } }
        return o; };
    std::vector<std::string> L, U; for (int i = 0; i < 4; i++) { L.push_back(to1252(rowsL[i])); U.push_back(to1252(rowsU[i])); }
    std::string text = initial ? initial : "";
    bool shift = false;
    const int kw = 58, kh = 40, ncol = 12, ox = (SCR_W - ncol * kw) / 2, oy = 150;
    Rect area{ox - 10, 40, ox + ncol * kw + 10, oy + 5 * kh + 10};
    ScreenSave sv; screen_save(sv, area);
    int sr = 1, sc = 0;
    const char* specDe[5] = { "Shift", "Leer", "<-", "OK", "Abbr." };
    const char* specEn[5] = { "Shift", "Space", "<-", "OK", "Cancel" };
    auto cellrect = [&](int r, int c) -> Rect { if (r < 4) return {ox + c * kw + 1, oy + r * kh + 1, ox + (c + 1) * kw - 1, oy + (r + 1) * kh - 1};
        int w = ncol * kw / 5; return {ox + c * w + 1, oy + 4 * kh + 1, ox + (c + 1) * w - 1, oy + 5 * kh - 1}; };
    auto rowlen = [&](int r) { return r < 4 ? (int)L[r].size() : 5; };
    auto draw = [&]() {
        scr_fill(area, 0x404040); scr_frame3d(area, true);
        scr_text({area.l, 44, area.r, 66}, title, 0xFFFFFF, 1, true);
        Rect tf{ox, 76, ox + ncol * kw, 104}; scr_fill(tf, 0xFFFFFF); scr_frame3d(tf, false);
        std::string shown = text + "_"; scr_text({tf.l + 4, tf.t, tf.r - 4, tf.b}, shown.c_str(), 0, 0);
        scr_text({area.l, 110, area.r, 140}, TR("X/Klick=Zeichen  Quadrat=Loeschen  Dreieck=Shift  Start=OK  O=Abbrechen", "X/click=char  Square=delete  Triangle=shift  Start=OK  O=cancel"), 0xE0E0E0, 1);
        for (int r = 0; r < 5; r++) for (int c = 0; c < rowlen(r); c++) {
            Rect rc = cellrect(r, c); bool sel = r == sr && c == sc;
            scr_fill(rc, sel ? 0x800000 : (r == 4 && c == 0 && shift) ? 0x008000 : 0xC0C0C0); scr_frame3d(rc, true);
            char lab[8]; if (r < 4) { lab[0] = (shift ? U : L)[r][c]; lab[1] = 0; } else strcpy(lab, TR(specDe[c], specEn[c]));
            scr_text(rc, lab, sel ? 0xFFFFFF : 0, 1); } };
    draw(); plat_present(true);
    SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t prev = pad.buttons; int prevTouch = 1;
    int res = -1;
    auto act = [&](int r, int c) {
        if (r < 4) { if ((int)text.size() < maxlen - 1) text += (shift ? U : L)[r][c]; }
        else if (c == 0) shift = !shift; else if (c == 1) { if ((int)text.size() < maxlen - 1) text += ' '; }
        else if (c == 2) { if (!text.empty()) text.pop_back(); } else if (c == 3) res = 1; else res = 0;
        draw(); };
    uint32_t lm = lmb_mask();
    while (res < 0) {
        sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t b = pad.buttons, nb = b & ~prev; prev = b;
        int px, py; bool hover;
        bool tap = menu_pointer(pad, nb, prevTouch, px, py, hover);
        if (hover || tap) for (int r = 0; r < 5; r++) for (int c = 0; c < rowlen(r); c++) if (cellrect(r, c).has(px, py) && (r != sr || c != sc)) { sr = r; sc = c; draw(); }
        if (tap) { for (int r = 0; r < 5; r++) for (int c = 0; c < rowlen(r); c++) if (cellrect(r, c).has(px, py)) act(r, c); }
        nb &= ~lm;
        if (nb & SCE_CTRL_LEFT) { sc = (sc + rowlen(sr) - 1) % rowlen(sr); draw(); }
        if (nb & SCE_CTRL_RIGHT) { sc = (sc + 1) % rowlen(sr); draw(); }
        if (nb & (SCE_CTRL_UP | SCE_CTRL_DOWN)) { sr = (sr + ((nb & SCE_CTRL_UP) ? 4 : 1)) % 5; if (sc >= rowlen(sr)) sc = rowlen(sr) - 1; draw(); }
        if (nb & SCE_CTRL_CROSS) act(sr, sc);
        if (nb & SCE_CTRL_SQUARE) { if (!text.empty()) text.pop_back(); draw(); }
        if (nb & SCE_CTRL_TRIANGLE) { shift = !shift; draw(); }
        if (nb & SCE_CTRL_START) res = 1;
        if (nb & SCE_CTRL_CIRCLE) res = 0;
        plat_present(); sceKernelDelayThread(16000);
    }
    screen_restore(sv); plat_present(true);
    for (int i = 0; i < 50; i++) { SceTouchData t; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1); sceCtrlPeekBufferPositive(0, &pad, 1); if (!t.reportNum && !(pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START | lm))) break; sceKernelDelayThread(10000); }
    if (res == 1) { strncpy(out, text.c_str(), maxlen); out[maxlen - 1] = 0; return 1; }
    return 0;
}

// ------------------------------------------------------------ Einstellungsmenue
static void settings_menu() {
    build_opts();
    MenuGuard mg;
    static const char* dpadDe[3] = { "Richtungen (8 Wege)", "Pfeiltasten (4 Wege)", "Mauszeiger" };
    static const char* dpadEn[3] = { "Directions (8-way)", "Arrow keys (4-way)", "Mouse pointer" };
    static const char* stickDe[3] = { "Mauszeiger", "Richtungen", "Aus" };
    static const char* stickEn[3] = { "Mouse pointer", "Directions", "Off" };
    static const char* rstickDe[3] = { "Richtungen", "Mauszeiger", "Aus" };
    static const char* rstickEn[3] = { "Directions", "Mouse pointer", "Off" };
    for (int k = 0; k < 256; k++) if (g_keystate[k] & 0x80) push_key(k, false);
    if (g_lbutton) push_mouse(EV_LUP); if (g_rbutton) push_mouse(EV_RUP);
    SceCtrlData pad; do { sceCtrlPeekBufferPositive(0, &pad, 1); sceKernelDelayThread(10000); } while (pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT));
    Rect box{(SCR_W - 640) / 2, 8, (SCR_W + 640) / 2, SCR_H - 8};
    ScreenSave sv; screen_save(sv, {0, 0, SCR_W, SCR_H});
    const int NR = 18, rh = 26; int sel = 0;
    const int RDEF = 16, RSAVE = 17;
    auto rowtext = [&](int r, char* lab, char* val) {
        if (r < 8) { strcpy(lab, TR(kBtnDe[r], kBtnEn[r])); strcpy(val, g_opts[g_cfg.btn[r]].lab()); return; }
        switch (r) {
            case 8: strcpy(lab, TR("Steuerkreuz", "D-pad")); strcpy(val, TR(dpadDe[g_cfg.dpad], dpadEn[g_cfg.dpad])); break;
            case 9: strcpy(lab, TR("Linker Stick", "Left stick")); strcpy(val, TR(stickDe[g_cfg.lstick], stickEn[g_cfg.lstick])); break;
            case 10: strcpy(lab, TR("Rechter Stick", "Right stick")); strcpy(val, TR(rstickDe[g_cfg.rstick], rstickEn[g_cfg.rstick])); break;
            case 11: strcpy(lab, TR("Zeigertempo", "Pointer speed")); sprintf(val, "%d", g_cfg.speed); break;
            case 12: strcpy(lab, TR("Touchscreen", "Touchscreen")); strcpy(val, g_cfg.touch ? TR("Nur Zeiger bewegen", "Move pointer only") : TR("Antippen = Klick", "Tap = click")); break;
            case 13: strcpy(lab, TR("Lautstaerke", "Volume")); sprintf(val, "%d %%", g_cfg.volume); break;
            case 14: strcpy(lab, TR("Nativer Code (Neustart)", "Native code (restart)"));
                if (g_native_count == 0) { strcpy(val, TR("Nicht vorhanden - nur Emulation", "Not installed - emulation only")); break; }
                strcpy(val, g_cfg.nativ == 3 ? TR("Voll nativ", "Fully native") : g_cfg.nativ == 1 ? TR("Nur gepruefte", "Verified only") : TR("Aus", "Off")); break;
            case 15: strcpy(lab, "Sprache / Language"); strcpy(val, g_cfg.lang ? "English" : "Deutsch"); break;
            case RDEF: strcpy(lab, TR("Standard wiederherstellen", "Restore defaults")); val[0] = 0; break;
            case RSAVE: strcpy(lab, TR("Speichern und schliessen", "Save and close")); val[0] = 0; break;
        } };
    auto change = [&](int r, int d) {
        int no = g_opts.size();
        if (r < 8) g_cfg.btn[r] = (g_cfg.btn[r] + d + no) % no;
        else if (r == 8) g_cfg.dpad = (g_cfg.dpad + d + 3) % 3;
        else if (r == 9) g_cfg.lstick = (g_cfg.lstick + d + 3) % 3;
        else if (r == 10) g_cfg.rstick = (g_cfg.rstick + d + 3) % 3;
        else if (r == 11) g_cfg.speed = std::max(1, std::min(10, g_cfg.speed + d));
        else if (r == 12) g_cfg.touch ^= 1;
        else if (r == 13) { g_cfg.volume = std::max(0, std::min(100, g_cfg.volume + 10 * d)); g_volume = g_cfg.volume; }
        else if (r == 14 && g_native_count) { static const int nv[3] = {3, 1, 0}; int k = g_cfg.nativ == 3 ? 0 : g_cfg.nativ == 1 ? 1 : 2; k = (k + d + 3) % 3; g_cfg.nativ = nv[k]; }
        else if (r == 15) { g_cfg.lang ^= 1; g_lang = g_cfg.lang; } };
    auto rowrect = [&](int r) -> Rect { return {box.l + 12, box.t + 36 + r * rh, box.r - 12, box.t + 36 + (r + 1) * rh - 2}; };
    auto draw = [&]() {
        scr_fill(box, 0x302010); scr_frame3d(box, true);
        scr_text({box.l, box.t + 4, box.r, box.t + 30}, TR("Civilization II - Einstellungen", "Civilization II - Settings"), 0x80D8F0, 1, true);
        for (int r = 0; r < NR; r++) {
            Rect rr = rowrect(r);
            scr_fill(rr, r == sel ? 0x804000 : 0x403020);
            char lab[64], val[64]; rowtext(r, lab, val);
            scr_text({rr.l + 8, rr.t, rr.l + 260, rr.b}, lab, 0xFFFFFF, 0);
            if (val[0]) { char v2[80]; snprintf(v2, sizeof v2, "<  %s  >", val); scr_text({rr.l + 260, rr.t, rr.r - 8, rr.b}, v2, 0x80FFFF, 1); }
        }
        scr_text({box.l, box.b - 28, box.r, box.b - 4}, TR("Hoch/Runter: Zeile  Links/Rechts: aendern  X/Klick: ausfuehren  O: speichern+zurueck",
                                                         "Up/Down: row  Left/Right: change  X/click: select  O: save+back"), 0xC0C0C0, 1); };
    draw(); plat_present(true);
    sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t prev = pad.buttons; int prevTouch = 1; bool done = false;
    uint32_t held_t = 0;
    while (!done) {
        sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t b = pad.buttons, nb = b & ~prev; prev = b;
        uint32_t lm = lmb_mask();
        int px, py; bool hover;
        bool tap = menu_pointer(pad, nb, prevTouch, px, py, hover);
        int pr = -1; for (int r = 0; r < NR; r++) if (rowrect(r).has(px, py)) pr = r;
        if ((hover || tap) && pr >= 0 && pr != sel) { sel = pr; draw(); }
        if (tap) {
            if (pr < 0) { if (!box.has(px, py)) done = true; }
            else if (pr == RDEF) { cfg_default(); g_lang = g_cfg.lang; g_volume = g_cfg.volume; draw(); }
            else if (pr == RSAVE) done = true;
            else { Rect rr = rowrect(pr); change(pr, px < rr.l + 260 + (rr.r - 8 - rr.l - 260) / 2 ? -1 : 1); draw(); }
            plat_present(); sceKernelDelayThread(16000); continue;
        }
        nb &= ~lm;
        uint32_t t = now_ms();
        if (b & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) { if (!held_t) held_t = t; else if (t - held_t > 400) { change(sel, (b & SCE_CTRL_LEFT) ? -1 : 1); draw(); sceKernelDelayThread(70000); } } else held_t = 0;
        if (nb & SCE_CTRL_UP) { sel = (sel + NR - 1) % NR; draw(); }
        if (nb & SCE_CTRL_DOWN) { sel = (sel + 1) % NR; draw(); }
        if (nb & SCE_CTRL_LEFT) { change(sel, -1); draw(); }
        if (nb & SCE_CTRL_RIGHT) { change(sel, 1); draw(); }
        if (nb & SCE_CTRL_CROSS) { if (sel == RDEF) { cfg_default(); g_lang = g_cfg.lang; g_volume = g_cfg.volume; draw(); } else if (sel == RSAVE) done = true; else { change(sel, 1); draw(); } }
        if (nb & SCE_CTRL_CIRCLE) done = true;
        plat_present(); sceKernelDelayThread(16000);
    }
    cfg_save();
    screen_restore(sv); plat_present(true);
    do { sceCtrlPeekBufferPositive(0, &pad, 1); sceKernelDelayThread(10000); } while (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | lmb_mask()));
}

// ------------------------------------------------------------ Schnellbefehle (Einheitenbefehle ohne Tastatur)
enum { CK_OSK = -1, CK_MENU = -2, CK_SHOT = -3 };
// Bildschirmfoto als BMP nach ux0:data/civ2/bildschirmfoto_N.bmp
static void save_screenshot() {
    char path[256]; int n = 1; FILE* f = nullptr;
    for (; n < 1000; n++) { snprintf(path, sizeof path, "%s/bildschirmfoto_%d.bmp", g_root.c_str(), n); FILE* t = fopen(path, "rb"); if (!t) break; fclose(t); }
    f = fopen(path, "wb"); if (!f) return;
    uint32_t rowb = SCR_W * 3, sz = 54 + rowb * SCR_H;
    uint8_t h[54] = {'B', 'M'}; memcpy(h + 2, &sz, 4); uint32_t v = 54; memcpy(h + 10, &v, 4); v = 40; memcpy(h + 14, &v, 4);
    int32_t w = SCR_W, hh = SCR_H; memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4); h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    std::vector<uint8_t> row(rowb);
    for (int y = SCR_H - 1; y >= 0; y--) { for (int x = 0; x < SCR_W; x++) { uint32_t c = g_screen[y * SCR_W + x]; row[x * 3] = (c >> 16) & 0xFF; row[x * 3 + 1] = (c >> 8) & 0xFF; row[x * 3 + 2] = c & 0xFF; } fwrite(row.data(), 1, rowb, f); }
    fclose(f); rlog("Bildschirmfoto: %s", path);
}
struct CmdItem { const char* de; const char* en; int key; };
static const CmdItem kCmds[] = {
    {"Runde beenden", "End turn", MK_CTRL | 'N'}, {"Runde ueberspringen", "Skip turn", 0x20}, {"Warten", "Wait", 'W'}, {"Befestigen", "Fortify", 'F'},
    {"Schlafen", "Sleep", 'S'}, {"Gehen zu", "Go to", 'G'}, {"Neue Stadt bauen", "Build new city", 'B'}, {"Strasse bauen", "Build road", 'R'},
    {"Bewaesserung", "Irrigate", 'I'}, {"Bergwerk", "Mine", 'M'}, {"Umwandeln", "Transform", 'O'}, {"Luftstuetzpunkt", "Airbase", 'E'},
    {"Auto-Siedler", "Auto settler", 'K'}, {"Entladen", "Unload", 'U'}, {"Heimatstadt", "Home city", 'H'}, {"Pluendern", "Pillage", MK_SHIFT | 'P'},
    {"Aufloesen", "Disband", MK_SHIFT | 'D'}, {"Einheit aktivieren", "Activate unit", 'A'}, {"Ansicht zentrieren", "Center view", 'C'}, {"Einheiten ansehen", "View pieces", 'V'},
    {"Vergroessern", "Zoom in", 'Z'}, {"Verkleinern", "Zoom out", 'X'}, {"Stadt finden", "Find city", MK_SHIFT | 'C'}, {"Steuerrate", "Tax rate", MK_SHIFT | 'T'},
    {"Stadtstatus (F1)", "City status (F1)", 0x70}, {"Verteidigung (F2)", "Defense (F2)", 0x71}, {"Aussenminister (F3)", "Foreign (F3)", 0x72}, {"Handel (F5)", "Trade (F5)", 0x74},
    {"Wissenschaft (F6)", "Science (F6)", 0x75}, {"Mit Koenigen reden", "Talk to kings", MK_CTRL | 'C'}, {"Spiel speichern", "Save game", MK_CTRL | 'S'}, {"Spiel laden", "Load game", MK_CTRL | 'L'},
    {"Enter", "Enter", 0x0D}, {"Esc", "Esc", 0x1B}, {"Bildschirmtastatur", "On-screen keyboard", CK_OSK}, {"Einstellungen", "Settings", CK_MENU}, {"Bildschirmfoto", "Screenshot", CK_SHOT},
};
static void cmd_menu() {
    MenuGuard mg;
    const int N = sizeof kCmds / sizeof kCmds[0], COLS = 2, ROWS = (N + COLS - 1) / COLS, rh = 24, cw = 300;
    for (int k = 0; k < 256; k++) if (g_keystate[k] & 0x80) push_key(k, false);
    Rect box{(SCR_W - COLS * cw - 24) / 2, 16, (SCR_W + COLS * cw + 24) / 2, 16 + 38 + ROWS * rh + 32};
    ScreenSave sv; screen_save(sv, box);
    static int sel = 0;
    auto itemrect = [&](int i) -> Rect { int c = i / ROWS, r = i % ROWS; return {box.l + 12 + c * cw, box.t + 34 + r * rh, box.l + 12 + (c + 1) * cw - 6, box.t + 34 + (r + 1) * rh - 2}; };
    auto draw = [&]() {
        scr_fill(box, 0x302010); scr_frame3d(box, true);
        scr_text({box.l, box.t + 4, box.r, box.t + 30}, TR("Schnellbefehle", "Quick commands"), 0x80D8F0, 1, true);
        for (int i = 0; i < N; i++) { Rect rr = itemrect(i); scr_fill(rr, i == sel ? 0x804000 : 0x403020); scr_text({rr.l + 8, rr.t, rr.r, rr.b}, TR(kCmds[i].de, kCmds[i].en), 0xFFFFFF, 0); }
        scr_text({box.l, box.b - 28, box.r, box.b - 4}, TR("Steuerkreuz/Stick: waehlen   X/Klick: ausfuehren   O: zurueck", "D-pad/stick: choose   X/click: run   O: back"), 0xC0C0C0, 1); };
    SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t prev = pad.buttons;
    draw(); plat_present(true);
    int run = -1; bool done = false; int prevTouch = 1;
    uint32_t lm = lmb_mask();
    while (!done) {
        sceCtrlPeekBufferPositive(0, &pad, 1); uint32_t b = pad.buttons, nb = b & ~prev; prev = b;
        int px, py; bool hover;
        bool tap = menu_pointer(pad, nb, prevTouch, px, py, hover);
        int pi = -1; for (int i = 0; i < N; i++) if (itemrect(i).has(px, py)) pi = i;
        if ((hover || tap) && pi >= 0 && pi != sel) { sel = pi; draw(); }
        if (tap) { if (pi >= 0) { run = pi; done = true; } else if (!box.has(px, py)) done = true; continue; }
        nb &= ~lm;
        if (nb & SCE_CTRL_UP) { sel = (sel + N - 1) % N; draw(); }
        if (nb & SCE_CTRL_DOWN) { sel = (sel + 1) % N; draw(); }
        if (nb & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) { sel = (sel + ROWS) % (ROWS * COLS); if (sel >= N) sel = N - 1; draw(); }
        if (nb & SCE_CTRL_CROSS) { run = sel; done = true; }
        if (nb & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_SELECT)) done = true;
        plat_present(); sceKernelDelayThread(16000);
    }
    screen_restore(sv); plat_present(true);
    do { sceCtrlPeekBufferPositive(0, &pad, 1); sceKernelDelayThread(10000); } while (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE | SCE_CTRL_SELECT | lm));
    if (run >= 0) {
        int k = kCmds[run].key;
        if (k == CK_OSK) { extern bool edit_ime_focused(); if (!edit_ime_focused()) osk_run(); }
        else if (k == CK_MENU) settings_menu();
        else if (k == CK_SHOT) save_screenshot();
        else if (k) key_tap_later(k);
    }
}

static int dir_vk(int u, int d, int l, int r, bool eight) {
    if (eight) { if (u && l) return 0x24; if (u && r) return 0x21; if (d && l) return 0x23; if (d && r) return 0x22; }
    if (u) return 0x26; if (d) return 0x28; if (l) return 0x25; if (r) return 0x27; return 0;
}
// ------------------------------------------------------------ Eingabe abfragen
void plat_poll() {
    static bool inited = false; if (!inited) { inited = true; cfg_load(); }
    pend_poll();
    static uint32_t prev = 0; static int touching = 0;
    static int dirkey = 0; static uint32_t dir_t0 = 0, dir_last = 0;
    SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t b = pad.buttons, nb = b & ~prev, rb = prev & ~b; prev = b;
    // Start+Select: Einstellungen (immer)
    if ((b & SCE_CTRL_START) && (b & SCE_CTRL_SELECT) && (nb & (SCE_CTRL_START | SCE_CTRL_SELECT))) { settings_menu(); sceCtrlPeekBufferPositive(0, &pad, 1); prev = pad.buttons; return; }
    pointer_sticks(pad, true);
    // Touch
    SceTouchData t; sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
    if (t.reportNum > 0) {
        int x = t.report[0].x / 2, y = t.report[0].y / 2;
        if (x != g_mouse_x || y != g_mouse_y || !touching) { g_mouse_x = x; g_mouse_y = y; g_fx = x; g_fy = y; push_mouse(EV_MOUSEMOVE); }
        if (!touching && g_cfg.touch == 0) push_mouse(EV_LDOWN);
        touching = 1;
    } else if (touching) { touching = 0; if (g_cfg.touch == 0) push_mouse(EV_LUP); }
    // Tasten nach Belegung (Start+Select-Kombination nicht einzeln ausloesen)
    bool combo = (b & SCE_CTRL_START) && (b & SCE_CTRL_SELECT);
    for (int i = 0; i < 8; i++) {
        const Opt& o = g_opts[g_cfg.btn[i]];
        bool dn = nb & kBtnMask[i], up = rb & kBtnMask[i];
        if (!dn && !up) continue;
        switch (o.act) {
            case A_LMB: push_mouse(dn ? EV_LDOWN : EV_LUP); break;
            case A_RMB: push_mouse(dn ? EV_RDOWN : EV_RUP); break;
            case A_KEY: key_combo(o.vk, dn); break;
            case A_CMD: if (dn && !combo) { cmd_menu(); sceCtrlPeekBufferPositive(0, &pad, 1); prev = pad.buttons; return; } break;
            case A_OSK: if (dn && !combo) { extern bool edit_ime_focused(); if (!edit_ime_focused()) osk_run(); } break;
            case A_MENU: if (dn && !combo) settings_menu(); break;
        }
    }
    // Richtungen aus Steuerkreuz / Sticks
    int u = 0, d = 0, l = 0, r = 0;
    if (g_cfg.dpad != 2) { u |= (b & SCE_CTRL_UP) ? 1 : 0; d |= (b & SCE_CTRL_DOWN) ? 1 : 0; l |= (b & SCE_CTRL_LEFT) ? 1 : 0; r |= (b & SCE_CTRL_RIGHT) ? 1 : 0; }
    auto sdir = [&](int sx, int sy) { if (sy < 60) u = 1; if (sy > 196) d = 1; if (sx < 60) l = 1; if (sx > 196) r = 1; };
    if (g_cfg.lstick == 1) sdir(pad.lx, pad.ly);
    if (g_cfg.rstick == 0) sdir(pad.rx, pad.ry);
    int vk = dir_vk(u, d, l, r, g_cfg.dpad != 1);
    uint32_t tn = now_ms();
    if (vk != dirkey) {
        static uint32_t settle = 0; static int pending = -1;
        if (pending != vk) { pending = vk; settle = tn; }
        if (tn - settle >= 40 || vk == 0) {
            if (dirkey) push_key(dirkey, false);
            dirkey = vk; pending = -1;
            if (vk) { push_key(vk, true); dir_t0 = tn; dir_last = tn; }
        }
    } else if (vk && tn - dir_t0 > 450 && tn - dir_last > 160) { push_key(vk, true); dir_last = tn; }
}
