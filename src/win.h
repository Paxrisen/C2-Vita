// civ2vita — gemeinsame Strukturen fuer GDI/USER
#pragma once
#include "rt.h"
#include <map>
#include <memory>
#include <algorithm>

struct Rect { int l = 0, t = 0, r = 0, b = 0;
    int w() const { return r - l; } int h() const { return b - t; }
    bool empty() const { return r <= l || b <= t; }
    bool has(int x, int y) const { return x >= l && x < r && y >= t && y < b; } };
static inline Rect rI(const Rect& a, const Rect& b){ Rect o{std::max(a.l,b.l),std::max(a.t,b.t),std::min(a.r,b.r),std::min(a.b,b.b)}; if (o.empty()) o = Rect{}; return o; }
static inline Rect rU(const Rect& a, const Rect& b){ if (a.empty()) return b; if (b.empty()) return a; return {std::min(a.l,b.l),std::min(a.t,b.t),std::max(a.r,b.r),std::max(a.b,b.b)}; }
static inline Rect rOff(Rect a, int dx, int dy){ a.l+=dx; a.r+=dx; a.t+=dy; a.b+=dy; return a; }
static inline Rect rd_rect(uint32_t va){ Rect r; r.l=(int)rd32(va); r.t=(int)rd32(va+4); r.r=(int)rd32(va+8); r.b=(int)rd32(va+12); return r; }
static inline void wr_rect(uint32_t va, const Rect& r){ wr32(va,r.l); wr32(va+4,r.t); wr32(va+8,r.r); wr32(va+12,r.b); }
void rsub(std::vector<Rect>& v, const Rect& s);       // v := v \ s

// ---------------- GDI-Objekte
enum { GO_BITMAP = 1, GO_PEN, GO_BRUSH, GO_FONT, GO_PALETTE, GO_DC };
enum { PF_8 = 8, PF_16 = 16, PF_24 = 24, PF_32 = 32, PF_SCR = 99, PF_1 = 1 };
struct GBitmap { int w = 0, h = 0, fmt = PF_SCR; int pitch = 0; uint8_t* bits = nullptr; uint32_t bitsva = 0;
    bool topdown = true; uint32_t pal[256]; int npal = 0; std::vector<uint8_t> store; bool dibsec = false;
    bool pal_is_idx = false; uint16_t palidx[256]; };
struct GFont { int h = 16; int weight = 400; bool italic = false, underline = false; std::string face; int fidx = 0; float scale = 1; int asc = 12, desc = 4, ileading = 0, avgw = 7, maxw = 14; int height = 16; };
struct GObj { int type = 0; bool stock = false;
    uint32_t color = 0; int style = 0, width = 1;        // pen/brush
    std::shared_ptr<GBitmap> bmp;                        // bitmap
    GFont font;                                          // font
    uint32_t pal[256]; int npal = 0; };                 // palette (COLORREF)
struct GDC { bool mem = false; uint32_t hwnd = 0; bool client = true; bool paint = false; bool screen = false;
    uint32_t bmp = 0, pen = 0, brush = 0, font = 0, pal = 0;
    uint32_t text = 0, bk = 0xFFFFFF; int bkmode = 2; uint32_t align = 0; int curx = 0, cury = 0;
    Rect paintclip; bool haspaintclip = false; };

extern std::map<uint32_t, GObj> g_obj;
extern std::map<uint32_t, GDC>  g_dc;
uint32_t gdi_new(GObj o);
GObj* gdi_get(uint32_t h);
GDC*  dc_get(uint32_t h);
uint32_t dc_create_window(uint32_t hwnd, bool client, bool paint, const Rect* pclip);
void dc_release(uint32_t h);
uint32_t bitmap_from_dib(uint32_t bih, uint32_t bits, uint32_t usage);  // packed DIB -> HBITMAP

// Zeichenflaeche (aus einem DC aufgeloest)
struct Surf { uint8_t* base = nullptr; int pitch = 0; int w = 0, h = 0, fmt = PF_SCR; const uint32_t* pal = nullptr; int npal = 0;
    int ox = 0, oy = 0; std::vector<Rect> clip; GBitmap* bm = nullptr; };
bool dc_surf(uint32_t hdc, Surf& s);
uint32_t sysclr(int idx);                    // COLOR_xxx -> COLORREF
uint32_t resolve_color(GDC* dc, uint32_t cr);
void surf_fill(Surf& s, Rect r, uint32_t cr);           // r in DC-Koordinaten
void surf_frame(Surf& s, Rect r, uint32_t cr);
void surf_line(Surf& s, int x0, int y0, int x1, int y1, uint32_t cr);
void surf_pixel(Surf& s, int x, int y, uint32_t cr);
// Text
int  text_draw(Surf& s, GFont& f, int x, int y, const char* str, int n, uint32_t cr, bool underline_idx_mask = false, int ulpos = -1);
int  text_width(GFont& f, const char* str, int n);
GFont& dc_font(GDC* dc);
GFont& sys_font();
void font_setup(GFont& f);
void fonts_init();
uint32_t draw_text_impl(uint32_t hdc, const char* s, int n, Rect& r, uint32_t fmt);
// Bildschirm-Hilfen fuer modale UI
struct ScreenSave { Rect r; std::vector<uint32_t> px; };
void screen_save(ScreenSave& s, Rect r);
void screen_restore(const ScreenSave& s);
void scr_fill(Rect r, uint32_t cr);
void scr_frame3d(Rect r, bool raised);
int  scr_text(Rect r, const char* s, uint32_t cr, int align /*0 left 1 center 2 right*/, bool bold = false);

// ---------------- Fenster
struct Wnd;
struct ScrollInfo { int mn = 0, mx = 100, pos = 0; bool vis = false; bool enabled = true; };
struct MenuItem { uint32_t id = 0; uint32_t flags = 0; std::string text; uint32_t sub = 0; };
struct Menu { std::vector<MenuItem> items; bool popup = false; };
extern std::map<uint32_t, Menu> g_menus;
struct WClass { std::string name; uint32_t proc = 0; uint32_t style = 0; int cbCls = 0, cbWnd = 0; uint32_t hinst = 0, icon = 0, cursor = 0, brush = 0; uint32_t menuName = 0; std::vector<uint8_t> clsExtra; uint16_t atom = 0; };
struct Wnd {
    uint32_t h = 0; WClass* cls = nullptr; uint32_t proc = 0; uint32_t style = 0, exstyle = 0;
    uint32_t parent = 0, owner = 0, id = 0, menu = 0, hinst = 0, userdata = 0;
    Rect wr;            // Fensterrechteck in Bildschirmkoordinaten
    std::string text; std::vector<uint8_t> extra;
    std::vector<uint32_t> kids;   // z-Ordnung: hinten -> vorne
    Rect upd; bool erase = false; bool nc_dirty = true;
    ScrollInfo sb[3];  // H, V, CTL
    bool destroyed = false; bool maximized = false, minimized = false;
    uint32_t font = 0;
    // native Steuerelemente
    int ctl = 0;                  // 0=keins 1=edit 2=button 3=listbox 4=combobox 5=scrollbar 6=static
    std::vector<std::string> items; std::vector<uint32_t> itemdata; int cursel = -1, top = 0; int itemh = 16;
    int check = 0; bool pushed = false; int limit = 0; int selA = 0, selB = 0; bool dropped = false;
};
extern std::map<uint32_t, Wnd*> g_wnds;
extern std::vector<uint32_t> g_top;      // Top-Level z-Ordnung
Wnd* W(uint32_t h);
Rect client_rect_screen(Wnd* w);           // Clientbereich in Bildschirmkoordinaten
Rect client_rect_local(Wnd* w);            // (0,0,w,h)
void vis_region(Wnd* w, bool client, std::vector<Rect>& out);
bool wnd_visible(Wnd* w);
uint32_t send_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
void post_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
void invalidate(Wnd* w, const Rect* r, bool erase);
void expose_screen(Rect r, uint32_t except = 0);
void draw_nc(Wnd* w);
uint32_t def_window_proc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
uint32_t ctl_proc(Wnd* w, uint32_t msg, uint32_t wp, uint32_t lp);   // controls.cpp
void ctl_init_class(Wnd* w);
bool ctl_class(const std::string& name, int& kind);
uint32_t focus_hwnd();
void set_focus(uint32_t h);
extern uint32_t g_capture;
int  menu_track(uint32_t hmenu, int x, int y, uint32_t owner, bool bar, int startIdx);   // -> id oder 0
void draw_scrollbar(Wnd* w, int bar, const Rect& r);
void scroll_click(Wnd* w, int bar, const Rect& r, int x, int y);
int  sb_size();
int  caption_h();
int  menubar_h();
void nc_metrics(Wnd* w, int& bl, int& bt, int& br, int& bb);  // Rahmen inkl. Titel/Menue/Scrollbars
void layout_changed();
uint32_t ui_font();   // HFONT fuer Steuerelemente
