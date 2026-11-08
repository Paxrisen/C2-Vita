// civ2vita — USER32: Fenster, Klassen, Nachrichten, Zeichnen, Nicht-Clientbereich, Menues, Eingabe
#include "win.h"
#include <deque>
#include <cstdarg>

std::map<uint32_t, Wnd*> g_wnds;
std::vector<uint32_t> g_top;
std::map<uint32_t, Menu> g_menus;
static std::map<std::string, WClass*> g_classes;   // Name (gross) -> Klasse
static std::map<uint16_t, WClass*> g_atoms;
static uint32_t g_wnext = 0x00010010, g_mnext = 0x00600010;
static uint32_t g_focus = 0, g_active = 0;
uint32_t g_capture = 0;
static int g_cursor_show = 0;
extern uint8_t cp_upper(uint8_t), cp_lower(uint8_t);
extern uint32_t res_find_any(uint32_t hinst, uint32_t name, uint32_t type, uint32_t* base_out);
extern uint32_t res_data(uint32_t base, uint32_t hrsrc, uint32_t* size);
// ---- Mauszeiger aus Programmressourcen (RT_GROUP_CURSOR 12 / RT_CURSOR 1)
static std::map<uint32_t, CursorImg> g_cursors;
static inline uint32_t rd8(uint32_t a) { return *(uint8_t*)G(a); }
static uint32_t load_cursor_res(uint32_t hinst, uint32_t name) {
    uint32_t base = 0, hr = res_find_any(hinst, name, 12, &base); if (!hr) return 0;
    uint32_t gsz = 0, g = res_data(base, hr, &gsz); if (!g || gsz < 20) return 0;
    uint16_t cnt = rd16(g + 4); if (!cnt) return 0;
    uint16_t id = rd16(g + 6 + 12);              // erster Eintrag: nID am Ende der 14 Bytes
    uint32_t b2 = 0, hc = res_find_any(base, id, 1, &b2); if (!hc) return 0;
    uint32_t csz = 0, d = res_data(b2, hc, &csz); if (!d || csz < 48) return 0;
    CursorImg ci; ci.hx = rd16(d); ci.hy = rd16(d + 2);
    uint32_t bi = d + 4; uint32_t bsz = rd32(bi); int w = (int)rd32(bi + 4), h2 = (int)rd32(bi + 8), bpp = rd16(bi + 14);
    int h = h2 / 2; if (w <= 0 || h <= 0 || w > 64 || h > 64) return 0;
    int ncol = rd32(bi + 32); if (!ncol && bpp <= 8) ncol = 1 << bpp;
    uint32_t pal = bi + bsz, xorp = pal + ncol * 4;
    int xpitch = ((w * bpp + 31) / 32) * 4, apitch = ((w + 31) / 32) * 4;
    uint32_t andp = xorp + xpitch * h;
    ci.w = w; ci.h = h; ci.px.assign(w * h, 0);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int sy = h - 1 - y;
        int a = (rd8(andp + sy * apitch + x / 8) >> (7 - x % 8)) & 1;
        uint32_t col;
        if (bpp == 1) { int v = (rd8(xorp + sy * xpitch + x / 8) >> (7 - x % 8)) & 1; col = rd32(pal + v * 4) & 0xFFFFFF; }
        else if (bpp == 4) { int v = (rd8(xorp + sy * xpitch + x / 2) >> ((x & 1) ? 0 : 4)) & 15; col = rd32(pal + v * 4) & 0xFFFFFF; }
        else if (bpp == 8) col = rd32(pal + rd8(xorp + sy * xpitch + x) * 4) & 0xFFFFFF;
        else col = rd32(xorp + sy * xpitch + x * (bpp / 8)) & 0xFFFFFF;
        uint32_t rgb = 0xFF000000u | ((col & 0xFF) << 16) | (col & 0xFF00) | ((col >> 16) & 0xFF);   // BGR -> RGBA (ABGR im Speicher)
        if (!a) ci.px[y * w + x] = rgb;
        else if (col) ci.px[y * w + x] = 1;   // invertieren
    }
    static uint32_t next = 0x00720000; uint32_t hcur = next; next += 4;
    g_cursors[hcur] = std::move(ci);
    return hcur;
}

enum { WM_CREATE = 1, WM_DESTROY = 2, WM_MOVE = 3, WM_SIZE = 5, WM_ACTIVATE = 6, WM_SETFOCUS = 7, WM_KILLFOCUS = 8, WM_ENABLE = 0x0A,
    WM_SETTEXT = 0x0C, WM_GETTEXT = 0x0D, WM_GETTEXTLENGTH = 0x0E, WM_PAINT = 0x0F, WM_CLOSE = 0x10, WM_ERASEBKGND = 0x14,
    WM_SHOWWINDOW = 0x18, WM_ACTIVATEAPP = 0x1C, WM_CANCELMODE = 0x1F, WM_SETCURSOR = 0x20, WM_MOUSEACTIVATE = 0x21, WM_GETMINMAXINFO = 0x24,
    WM_SETFONT = 0x30, WM_GETFONT = 0x31, WM_WINDOWPOSCHANGED = 0x47, WM_NCCREATE = 0x81, WM_NCDESTROY = 0x82, WM_NCHITTEST = 0x84,
    WM_NCPAINT = 0x85, WM_NCACTIVATE = 0x86, WM_KEYDOWN = 0x100, WM_KEYUP = 0x101, WM_CHAR = 0x102, WM_SYSKEYDOWN = 0x104, WM_SYSKEYUP = 0x105,
    WM_SYSCHAR = 0x106, WM_COMMAND = 0x111, WM_SYSCOMMAND = 0x112, WM_TIMER = 0x113, WM_HSCROLL = 0x114, WM_VSCROLL = 0x115,
    WM_INITMENU = 0x116, WM_INITMENUPOPUP = 0x117, WM_MENUSELECT = 0x11F, WM_MOUSEMOVE = 0x200, WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202,
    WM_LBUTTONDBLCLK = 0x203, WM_RBUTTONDOWN = 0x204, WM_RBUTTONUP = 0x205, WM_RBUTTONDBLCLK = 0x206, WM_CAPTURECHANGED = 0x215 };
enum : uint32_t { WS_CHILD = 0x40000000, WS_VISIBLE = 0x10000000, WS_DISABLED = 0x08000000, WS_CAPTION = 0x00C00000, WS_BORDER = 0x00800000,
    WS_DLGFRAME = 0x00400000, WS_VSCROLL = 0x00200000, WS_HSCROLL = 0x00100000, WS_SYSMENU = 0x00080000, WS_THICKFRAME = 0x00040000,
    WS_POPUP = 0x80000000, WS_MAXIMIZE = 0x01000000, WS_MINIMIZE = 0x20000000 };

Wnd* W(uint32_t h) { auto it = g_wnds.find(h); return (it == g_wnds.end() || it->second->destroyed) ? nullptr : it->second; }
static bool is_child(Wnd* w) { return (w->style & WS_CHILD) != 0; }
int sb_size() { return 16; }
int caption_h() { return 18; }
int menubar_h() { return 19; }

// ============================================================ Geometrie
static void frame_w(Wnd* w, int& b) {
    b = 0;
    if (w->style & WS_THICKFRAME) b = 4;
    else if ((w->style & WS_DLGFRAME) || (w->exstyle & 1)) b = 3;
    else if (w->style & WS_BORDER) b = 1;
    if (w->exstyle & 0x200) b += 2;          // WS_EX_CLIENTEDGE
}
void nc_metrics(Wnd* w, int& bl, int& bt, int& br, int& bb) {
    int b; frame_w(w, b);
    bl = bt = br = bb = b;
    if ((w->style & WS_CAPTION) == WS_CAPTION) bt += caption_h() + 1;
    if (!is_child(w) && w->menu && g_menus.count(w->menu)) bt += menubar_h();
    if (w->ctl == 0 || w->ctl == 3 || w->ctl == 1) {
        if (w->style & WS_VSCROLL) br += sb_size();
        if (w->style & WS_HSCROLL) bb += sb_size();
    }
}
Rect client_rect_screen(Wnd* w) {
    int bl, bt, br, bb; nc_metrics(w, bl, bt, br, bb);
    Rect r{w->wr.l + bl, w->wr.t + bt, w->wr.r - br, w->wr.b - bb};
    if (r.r < r.l) r.r = r.l; if (r.b < r.t) r.b = r.t;
    return r;
}
Rect client_rect_local(Wnd* w) { Rect r = client_rect_screen(w); return {0, 0, r.w(), r.h()}; }
bool wnd_visible(Wnd* w) {
    while (w) { if (!(w->style & WS_VISIBLE) || w->destroyed) return false; if (!is_child(w)) return true; w = W(w->parent); }
    return false;
}
static std::vector<uint32_t>& siblings(Wnd* w) { if (is_child(w)) { Wnd* p = W(w->parent); if (p) return p->kids; } return g_top; }
void vis_region(Wnd* w, bool client, std::vector<Rect>& out) {
    out.clear();
    if (!wnd_visible(w)) return;
    Rect base = client ? client_rect_screen(w) : w->wr;
    out.push_back(rI(base, {0, 0, SCR_W, SCR_H}));
    // durch Vorfahren begrenzen
    for (Wnd* p = is_child(w) ? W(w->parent) : nullptr; p; p = is_child(p) ? W(p->parent) : nullptr) {
        Rect pc = client_rect_screen(p); for (auto& r : out) r = rI(r, pc);
    }
    // Geschwister darueber abziehen (auf jeder Ebene)
    for (Wnd* cur = w; cur; cur = is_child(cur) ? W(cur->parent) : nullptr) {
        auto& sib = siblings(cur);
        auto it = std::find(sib.begin(), sib.end(), cur->h);
        if (it != sib.end()) for (++it; it != sib.end(); ++it) { Wnd* s = W(*it); if (s && (s->style & WS_VISIBLE)) rsub(out, s->wr); }
    }
    if (client) for (uint32_t k : w->kids) { Wnd* c = W(k); if (c && (c->style & WS_VISIBLE)) rsub(out, c->wr); }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Rect& r) { return r.empty(); }), out.end());
}
static void offset_tree(Wnd* w, int dx, int dy) {
    w->wr = rOff(w->wr, dx, dy);
    for (uint32_t k : w->kids) if (Wnd* c = W(k)) offset_tree(c, dx, dy);
}
void layout_changed() {}

// ============================================================ Nachrichten
struct QMsg { uint32_t hwnd, msg, wp, lp, time; int x, y; };
static std::deque<QMsg> g_q;
struct Timer { uint32_t hwnd, id, ms, proc; uint32_t next; };
static std::vector<Timer> g_timers;
struct Hook { int id; uint32_t proc; uint32_t h; };
static std::vector<Hook> g_hooks;

uint32_t ctl_trap_va();
static uint32_t g_defproc_va = 0;
uint32_t send_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    Wnd* w = W(hwnd); if (!w) return 0;
    if (w->proc == ctl_trap_va() && w->ctl) return ctl_proc(w, msg, wp, lp);
    if (!w->proc || w->proc == g_defproc_va) return def_window_proc(hwnd, msg, wp, lp);
    return gcall(w->proc, { hwnd, msg, wp, lp });
}
void post_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    g_q.push_back({hwnd, msg, wp, lp, now_ms(), g_mouse_x, g_mouse_y});
}
static uint32_t call_proc(uint32_t proc, uint32_t h, uint32_t m, uint32_t wp, uint32_t lp) {
    Wnd* w = W(h);
    if (proc == ctl_trap_va() && w && w->ctl) return ctl_proc(w, m, wp, lp);
    if (!proc || proc == g_defproc_va) return def_window_proc(h, m, wp, lp);
    return gcall(proc, { h, m, wp, lp });
}

// ============================================================ Invalidierung
void invalidate(Wnd* w, const Rect* r, bool erase) {
    if (!w) return;
    Rect cr = client_rect_local(w);
    Rect a = r ? rI(*r, cr) : cr;
    if (a.empty()) return;
    w->upd = rU(w->upd, a);
    if (erase) w->erase = true;
}
static void invalidate_tree(Wnd* w, const Rect& scr) {
    if (!(w->style & WS_VISIBLE)) return;
    Rect i = rI(scr, w->wr);
    if (i.empty()) return;
    w->nc_dirty = true;
    Rect cs = client_rect_screen(w);
    Rect ci = rI(scr, cs);
    if (!ci.empty()) { Rect loc = rOff(ci, -cs.l, -cs.t); invalidate(w, &loc, true); }
    for (uint32_t k : w->kids) if (Wnd* c = W(k)) invalidate_tree(c, scr);
}
void expose_screen(Rect r, uint32_t except) {
    r = rI(r, {0, 0, SCR_W, SCR_H}); if (r.empty()) return;
    std::vector<Rect> desk{r};
    for (uint32_t h : g_top) { Wnd* w = W(h); if (!w || h == except || !(w->style & WS_VISIBLE)) continue; rsub(desk, w->wr); invalidate_tree(w, r); }
    for (auto& d : desk) scr_fill(d, 0x000000);
}

// ============================================================ Zeichnen Nicht-Clientbereich
static void fill_clip(const std::vector<Rect>& clip, Rect r, uint32_t cr) { for (auto& c : clip) { Rect i = rI(r, c); if (!i.empty()) scr_fill(i, cr); } }
static void frame3d_clip(const std::vector<Rect>& clip, Rect r, bool raised) {
    uint32_t a = raised ? 0xFFFFFF : 0x808080, b = raised ? 0x404040 : 0xFFFFFF;
    fill_clip(clip, {r.l, r.t, r.r, r.t + 1}, a); fill_clip(clip, {r.l, r.t, r.l + 1, r.b}, a);
    fill_clip(clip, {r.l, r.b - 1, r.r, r.b}, b); fill_clip(clip, {r.r - 1, r.t, r.r, r.b}, b);
}
int scr_text_clipped(Rect r, Rect clip, const char* s, uint32_t cr, int align, bool bold);
static void text_clip(const std::vector<Rect>& clip, Rect r, const char* s, uint32_t cr, int align, bool bold) {
    for (auto& c : clip) { Rect i = rI(r, c); if (!i.empty()) scr_text_clipped(r, i, s, cr, align, bold); }
}
static std::vector<Rect> menubar_items(Wnd* w, Rect bar) {
    std::vector<Rect> out; Menu* m = g_menus.count(w->menu) ? &g_menus[w->menu] : nullptr; if (!m) return out;
    int x = bar.l + 4;
    extern int ui_text_w(const char* s, bool bold);
    for (auto& it : m->items) {
        std::string t; for (size_t i = 0; i < it.text.size(); i++) { if (it.text[i] == '&') continue; if (it.text[i] == '\t') break; t += it.text[i]; }
        int tw = ui_text_w(t.c_str(), false) + 14;
        out.push_back({x, bar.t, x + tw, bar.b}); x += tw;
    }
    return out;
}
static Rect menubar_rect(Wnd* w) {
    int b; frame_w(w, b);
    int top = w->wr.t + b + (((w->style & WS_CAPTION) == WS_CAPTION) ? caption_h() + 1 : 0);
    return {w->wr.l + b, top, w->wr.r - b, top + menubar_h()};
}
static Rect caption_rect(Wnd* w) { int b; frame_w(w, b); return {w->wr.l + b, w->wr.t + b, w->wr.r - b, w->wr.t + b + caption_h()}; }
static Rect closebox_rect(Wnd* w) { Rect c = caption_rect(w); return {c.r - 18, c.t + 2, c.r - 2, c.b - 2}; }
static Rect sb_rect(Wnd* w, int bar) {
    Rect cs = client_rect_screen(w);
    if (bar == 1) return {cs.r, cs.t, cs.r + sb_size(), cs.b};
    return {cs.l, cs.b, cs.r, cs.b + sb_size()};
}
static std::string strip_amp(const std::string& s) { std::string t; for (char ch : s) if (ch != '&') t += ch; return t; }
static bool is_active_chain(Wnd* w) { while (w && is_child(w)) w = W(w->parent); return w && (w->h == g_active); }

void draw_scrollbar_clip(Wnd* w, int bar, const Rect& r, const std::vector<Rect>& clip) {
    ScrollInfo& si = w->sb[bar == 2 ? 2 : bar];
    bool vert = (bar == 1) || (bar == 2 && (w->style & 1));
    int len = vert ? r.h() : r.w(), th = vert ? r.w() : r.h();
    fill_clip(clip, r, 0xE0E0E0);
    Rect a1 = vert ? Rect{r.l, r.t, r.r, r.t + th} : Rect{r.l, r.t, r.l + th, r.b};
    Rect a2 = vert ? Rect{r.l, r.b - th, r.r, r.b} : Rect{r.r - th, r.t, r.r, r.b};
    for (Rect a : {a1, a2}) { fill_clip(clip, a, 0xC0C0C0); frame3d_clip(clip, a, true); }
    // Pfeile
    auto tri = [&](Rect a, int dir) { int cx = (a.l + a.r) / 2, cy = (a.t + a.b) / 2;
        for (int i = 0; i < 4; i++) {
            if (dir == 0) fill_clip(clip, {cx - i, cy - 2 + i, cx + i + 1, cy - 1 + i}, 0);
            if (dir == 1) fill_clip(clip, {cx - 3 + i, cy - 1 + i, cx + 4 - i, cy + i}, 0);
            if (dir == 2) fill_clip(clip, {cx - 2 + i, cy - i, cx - 1 + i, cy + i + 1}, 0);
            if (dir == 3) fill_clip(clip, {cx + 1 - i, cy - 3 + i, cx + 2 - i, cy + 4 - i}, 0); } };
    tri(a1, vert ? 0 : 2); tri(a2, vert ? 1 : 3);
    int range = si.mx - si.mn;
    if (range > 0 && len > 3 * th) {
        int track = len - 3 * th;
        int pos = (int)((int64_t)(si.pos - si.mn) * track / range);
        Rect t = vert ? Rect{r.l, r.t + th + pos, r.r, r.t + 2 * th + pos} : Rect{r.l + th + pos, r.t, r.l + 2 * th + pos, r.b};
        fill_clip(clip, t, 0xC0C0C0); frame3d_clip(clip, t, true);
    }
}
void draw_scrollbar(Wnd* w, int bar, const Rect& r) { std::vector<Rect> clip; vis_region(w, bar == 2, clip); draw_scrollbar_clip(w, bar, r, clip); }

void draw_nc(Wnd* w) {
    w->nc_dirty = false;
    if (!wnd_visible(w)) return;
    std::vector<Rect> clip; vis_region(w, false, clip);
    rsub(clip, client_rect_screen(w));
    if (clip.empty()) return;
    int b; frame_w(w, b);
    Rect r = w->wr;
    if (b >= 3) { fill_clip(clip, r, 0xC0C0C0); frame3d_clip(clip, r, true); frame3d_clip(clip, {r.l + 1, r.t + 1, r.r - 1, r.b - 1}, true); }
    else if (b == 1) { fill_clip(clip, {r.l, r.t, r.r, r.t + 1}, 0); fill_clip(clip, {r.l, r.b - 1, r.r, r.b}, 0); fill_clip(clip, {r.l, r.t, r.l + 1, r.b}, 0); fill_clip(clip, {r.r - 1, r.t, r.r, r.b}, 0); }
    if (w->exstyle & 0x200) { Rect e{r.l + b - 2, r.t + b - 2, r.r - b + 2, r.b - b + 2}; (void)e; }
    if ((w->style & WS_CAPTION) == WS_CAPTION) {
        Rect c = caption_rect(w);
        bool act = is_active_chain(w);
        fill_clip(clip, c, act ? 0x800000 : 0x808080);
        fill_clip(clip, {c.l, c.b, c.r, c.b + 1}, 0xC0C0C0);
        text_clip(clip, {c.l + 4, c.t, c.r - 22, c.b}, w->text.c_str(), act ? 0xFFFFFF : 0xC0C0C0, 0, true);
        if (w->style & WS_SYSMENU) { Rect x = closebox_rect(w); fill_clip(clip, x, 0xC0C0C0); frame3d_clip(clip, x, true);
            for (int i = 0; i < 7; i++) { fill_clip(clip, {x.l + 4 + i, x.t + 3 + i, x.l + 6 + i, x.t + 4 + i}, 0); fill_clip(clip, {x.r - 6 - i, x.t + 3 + i, x.r - 4 - i, x.t + 4 + i}, 0); } }
    }
    if (!is_child(w) && w->menu && g_menus.count(w->menu)) {
        Rect mb = menubar_rect(w);
        fill_clip(clip, mb, 0xC0C0C0);
        auto items = menubar_items(w, mb);
        Menu& m = g_menus[w->menu];
        for (size_t i = 0; i < items.size() && i < m.items.size(); i++) {
            std::string t = m.items[i].text; size_t tab = t.find('\t'); if (tab != std::string::npos) t = t.substr(0, tab);
            text_clip(clip, items[i], strip_amp(t).c_str(), (m.items[i].flags & 3) ? 0x808080 : 0x000000, 1, false);
        }
    }
    if (w->ctl == 0 || w->ctl == 3 || w->ctl == 1) {
        if (w->style & WS_VSCROLL) draw_scrollbar_clip(w, 1, sb_rect(w, 1), clip);
        if (w->style & WS_HSCROLL) draw_scrollbar_clip(w, 0, sb_rect(w, 0), clip);
        if ((w->style & WS_VSCROLL) && (w->style & WS_HSCROLL)) { Rect cs = client_rect_screen(w); fill_clip(clip, {cs.r, cs.b, cs.r + sb_size(), cs.b + sb_size()}, 0xC0C0C0); }
    }
    g_screen_dirty = true;
}

// ============================================================ Fokus / Aktivierung / z-Ordnung
uint32_t focus_hwnd() { return W(g_focus) ? g_focus : 0; }
void set_focus(uint32_t h) {
    if (h == g_focus) return;
    uint32_t old = g_focus; g_focus = h;
    if (W(old)) send_msg(old, WM_KILLFOCUS, h, 0);
    if (W(h)) send_msg(h, WM_SETFOCUS, old, 0);
}
static Wnd* toplevel_of(Wnd* w) { while (w && is_child(w)) w = W(w->parent); return w; }
static void raise_top(uint32_t h) {
    auto it = std::find(g_top.begin(), g_top.end(), h); if (it == g_top.end()) return;
    g_top.erase(it); g_top.push_back(h);
    // eigene Popups darueber halten
    std::vector<uint32_t> owned;
    for (uint32_t t : g_top) if (Wnd* o = W(t)) if (o->owner == h && t != h) owned.push_back(t);
    for (uint32_t o : owned) { g_top.erase(std::find(g_top.begin(), g_top.end(), o)); g_top.push_back(o); }
}
static void activate(Wnd* top) {
    if (!top || top->h == g_active) return;
    uint32_t old = g_active; g_active = top->h;
    if (Wnd* o = W(old)) { o->nc_dirty = true; send_msg(old, 0x86, 0, 0); send_msg(old, WM_ACTIVATE, 0, top->h); }
    top->nc_dirty = true;
    send_msg(top->h, 0x86, 1, 0);
    send_msg(top->h, WM_ACTIVATE, 1, old);
    // Fokus in das aktivierte Fenster holen, falls er noch woanders liegt
    if (W(top->h)) { Wnd* f = W(g_focus); bool inside = false;
        for (Wnd* x = f; x; x = is_child(x) ? W(x->parent) : nullptr) if (x == top) { inside = true; break; }
        if (!inside) set_focus(top->h); }
}
static void bring_to_top(Wnd* w) {
    if (!w) return;
    auto& sib = siblings(w);
    if (!sib.empty() && sib.back() == w->h && is_child(w)) return;
    if (is_child(w)) { auto it = std::find(sib.begin(), sib.end(), w->h); if (it != sib.end()) { sib.erase(it); sib.push_back(w->h); } }
    else raise_top(w->h);
    invalidate_tree(w, w->wr);
}

// ============================================================ Fenster erzeugen / zerstoeren
static WClass* find_class(uint32_t name) {
    if (is_intres(name)) { auto it = g_atoms.find((uint16_t)name); return it == g_atoms.end() ? nullptr : it->second; }
    std::string n = GS(name); for (auto& ch : n) ch = toupper(ch);
    auto it = g_classes.find(n); return it == g_classes.end() ? nullptr : it->second;
}
static WClass* builtin_class(const char* name, int kind) {
    std::string n = name; for (auto& ch : n) ch = toupper(ch);
    auto it = g_classes.find(n); if (it != g_classes.end()) return it->second;
    WClass* c = new WClass; c->name = n; c->proc = ctl_trap_va(); c->style = 0x8; c->brush = 6;
    static uint16_t at = 0xC100; c->atom = at++; g_classes[n] = c; g_atoms[c->atom] = c; (void)kind;
    return c;
}
static void send_size_move(Wnd* w) {
    Rect cr = client_rect_local(w);
    uint32_t type = w->maximized ? 2 : w->minimized ? 1 : 0;
    send_msg(w->h, WM_SIZE, type, (cr.w() & 0xFFFF) | (cr.h() << 16));
    Rect cs = client_rect_screen(w);
    int x = cs.l, y = cs.t;
    if (is_child(w)) if (Wnd* p = W(w->parent)) { Rect pc = client_rect_screen(p); x -= pc.l; y -= pc.t; }
    send_msg(w->h, WM_MOVE, 0, (x & 0xFFFF) | ((y & 0xFFFF) << 16));
}
static void maximize_rect(Wnd* w) {
    int b; frame_w(w, b);
    int cap = ((w->style & WS_CAPTION) == WS_CAPTION) ? caption_h() + 1 : 0;
    if (is_child(w)) { Wnd* p = W(w->parent); Rect pc = p ? client_rect_screen(p) : Rect{0, 0, SCR_W, SCR_H};
        w->wr = {pc.l - b, pc.t - b, pc.r + b, pc.b + b}; return; }
    // Vollbild: Rahmen und Titelleiste ausserhalb des Bildschirms
    w->wr = {-b, -b - cap, SCR_W + b, SCR_H + b};
}
static void show_window(Wnd* w, int cmd) {
    bool was = (w->style & WS_VISIBLE) != 0;
    Rect old = w->wr;
    if (cmd == 0) {
        if (!was) return;
        send_msg(w->h, WM_SHOWWINDOW, 0, 0);
        w->style &= ~WS_VISIBLE;
        expose_screen(old, w->h);
        if (g_focus && (g_focus == w->h)) g_focus = 0;
        if (g_active == w->h) { g_active = 0; for (auto it = g_top.rbegin(); it != g_top.rend(); ++it) { Wnd* t = W(*it); if (t && (t->style & WS_VISIBLE)) { activate(t); break; } } }
        return;
    }
    bool resized = false;
    if (cmd == 3 && !w->maximized) { w->maximized = true; w->minimized = false; maximize_rect(w); resized = true; }
    if ((cmd == 1 || cmd == 9) && (w->maximized || w->minimized)) { w->maximized = w->minimized = false; resized = true; }
    if (!was) { send_msg(w->h, WM_SHOWWINDOW, 1, 0); w->style |= WS_VISIBLE; }
    if (resized && old.l != w->wr.l) { /* Kinder relativ mitverschieben */ int dx = w->wr.l - old.l, dy = w->wr.t - old.t; for (uint32_t k : w->kids) if (Wnd* c = W(k)) offset_tree(c, dx, dy); }
    if (!is_child(w) && cmd != 4 && cmd != 8 && cmd != 7) { raise_top(w->h); activate(w); }
    invalidate_tree(w, rU(w->wr, old));
    if (resized) { expose_screen(old, w->h); send_size_move(w); }
}
static void move_window(Wnd* w, Rect nr, bool repaint) {
    Rect old = w->wr;
    if (old.l == nr.l && old.t == nr.t && old.r == nr.r && old.b == nr.b) return;
    int dx = nr.l - old.l, dy = nr.t - old.t;
    w->wr = nr;
    for (uint32_t k : w->kids) if (Wnd* c = W(k)) offset_tree(c, dx, dy);
    if (w->style & WS_VISIBLE) { expose_screen(old, 0); invalidate_tree(w, w->wr); }
    (void)repaint;
    send_size_move(w);
}
static void destroy_window(uint32_t h) {
    Wnd* w = W(h); if (!w) return;
    Rect old = w->wr; bool vis = wnd_visible(w);
    send_msg(h, WM_DESTROY, 0, 0);
    std::vector<uint32_t> kids = w->kids;
    for (uint32_t k : kids) destroy_window(k);
    // Popups im Besitz ebenfalls zerstoeren
    std::vector<uint32_t> owned; for (uint32_t t : g_top) if (Wnd* o = W(t)) if (o->owner == h) owned.push_back(t);
    for (uint32_t o : owned) destroy_window(o);
    send_msg(h, WM_NCDESTROY, 0, 0);
    w->destroyed = true;
    auto& sib = siblings(w); auto it = std::find(sib.begin(), sib.end(), h); if (it != sib.end()) sib.erase(it);
    if (g_focus == h) g_focus = 0;
    if (g_capture == h) g_capture = 0;
    for (auto& t : g_timers) if (t.hwnd == h) t.hwnd = 0xFFFFFFFF;
    g_timers.erase(std::remove_if(g_timers.begin(), g_timers.end(), [](const Timer& t) { return t.hwnd == 0xFFFFFFFF; }), g_timers.end());
    if (vis) expose_screen(old, h);
    if (g_active == h) { g_active = 0; for (auto it2 = g_top.rbegin(); it2 != g_top.rend(); ++it2) { Wnd* t = W(*it2); if (t && (t->style & WS_VISIBLE)) { activate(t); break; } } }
}

static uint32_t create_window(uint32_t ex, uint32_t clsname, uint32_t title, uint32_t style, int x, int y, int cw, int ch,
                              uint32_t parent, uint32_t menu, uint32_t hinst, uint32_t param) {
    WClass* cls = find_class(clsname);
    int kind = 0;
    if (!cls && !is_intres(clsname) && ctl_class(GS(clsname), kind)) cls = builtin_class(GS(clsname), kind);
    if (!cls) { rlog("CreateWindowEx: Klasse %s unbekannt", is_intres(clsname) ? "#atom" : GS(clsname)); return 0; }
    ctl_class(cls->name.c_str(), kind);
    if (!kind && cls->proc == ctl_trap_va()) {   // Unterklasse eines Standard-Steuerelements (z. B. MSEditBoxClass)
        static const char* bn[6] = {"EDIT", "BUTTON", "LISTBOX", "COMBOBOX", "SCROLLBAR", "STATIC"};
        for (const char* b : bn) if (cls->name.find(b) != std::string::npos) { ctl_class(b, kind); break; }
    }
    Wnd* w = new Wnd; w->h = g_wnext; g_wnext += 0x10;
    w->cls = cls; w->proc = cls->proc; w->style = style; w->exstyle = ex; w->hinst = hinst;
    w->text = title ? GS(title) : "";
    w->extra.assign(cls->cbWnd, 0);
    w->ctl = kind;
    bool child = (style & WS_CHILD) != 0;
    if (child) { w->parent = parent; w->id = menu; }
    else { w->owner = parent; w->menu = menu; if (!menu && cls->menuName) {} }
    if ((style & WS_CAPTION) == WS_CAPTION || !child) {}
    // Position
    Rect pc{0, 0, SCR_W, SCR_H};
    Wnd* pw = W(parent);
    if (child && pw) pc = client_rect_screen(pw);
    if ((uint32_t)x == 0x80000000u) { x = child ? 0 : 20; y = child ? 0 : 20; }
    if ((uint32_t)cw == 0x80000000u) { cw = child ? pc.w() : SCR_W - 40; ch = child ? pc.h() : SCR_H - 40; }
    w->wr = {pc.l + x, pc.t + y, pc.l + x + cw, pc.t + y + ch};
    g_wnds[w->h] = w;
    if (child && pw) pw->kids.push_back(w->h); else g_top.push_back(w->h);
    w->style &= ~WS_VISIBLE;
    if (w->ctl) ctl_init_class(w);
    // CREATESTRUCT
    uint32_t cs = galloc(48);
    wr32(cs, param); wr32(cs + 4, hinst); wr32(cs + 8, menu); wr32(cs + 12, parent); wr32(cs + 16, ch); wr32(cs + 20, cw);
    wr32(cs + 24, y); wr32(cs + 28, x); wr32(cs + 32, style); wr32(cs + 36, title); wr32(cs + 40, clsname); wr32(cs + 44, ex);
    uint32_t h = w->h;
    if (!send_msg(h, WM_NCCREATE, 0, cs)) { rlog("WM_NCCREATE abgelehnt"); destroy_window(h); gfree(cs); return 0; }
    if (!W(h)) { gfree(cs); return 0; }
    if ((int32_t)send_msg(h, WM_CREATE, 0, cs) == -1) { destroy_window(h); gfree(cs); return 0; }
    gfree(cs);
    if (!W(h)) return 0;
    send_size_move(w);
    if (style & WS_VISIBLE) show_window(w, (style & WS_MAXIMIZE) ? 3 : 5);
    if (!child && (style & WS_VISIBLE)) activate(w);
    return h;
}

// ============================================================ Standard-Fensterprozedur
uint32_t def_window_proc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    Wnd* w = W(hwnd); if (!w) return 0;
    switch (msg) {
        case WM_NCCREATE: return 1;
        case WM_SETTEXT: w->text = lp ? GS(lp) : ""; w->nc_dirty = true; return 1;
        case WM_GETTEXT: { if (!wp) return 0; uint32_t n = std::min<uint32_t>(wp - 1, w->text.size()); memcpy(G(lp), w->text.data(), n); wr8(lp + n, 0); return n; }
        case WM_GETTEXTLENGTH: return w->text.size();
        case WM_PAINT: { w->upd = Rect{}; w->erase = false; return 0; }
        case WM_ERASEBKGND: {
            uint32_t br = w->cls ? w->cls->brush : 0; if (!br) return 0;
            Surf s; if (!dc_surf(wp, s)) return 0;
            uint32_t cr = 0xFFFFFF;
            if (br <= 31) cr = sysclr(br - 1); else if (GObj* o = gdi_get(br)) { if (o->style == 1) return 1; cr = resolve_color(dc_get(wp), o->color); }
            surf_fill(s, client_rect_local(w), cr); return 1; }
        case WM_CLOSE: destroy_window(hwnd); return 0;
        case WM_SYSCOMMAND: {
            uint32_t sc = wp & 0xFFF0;
            if (sc == 0xF060) send_msg(hwnd, WM_CLOSE, 0, 0);
            else if (sc == 0xF030) show_window(w, 3); else if (sc == 0xF120) show_window(w, 9); else if (sc == 0xF020) {}
            return 0; }
        case WM_MOUSEACTIVATE: return 1;
        case WM_NCHITTEST: return 1;
        case WM_NCPAINT: draw_nc(w); return 0;
        case WM_NCACTIVATE: w->nc_dirty = true; return 1;
        case WM_ACTIVATE: return 0;
        case WM_SETFONT: w->font = wp; return 0;
        case WM_GETFONT: return w->font;
        case 0x132: case 0x133: case 0x134: case 0x135: case 0x136: case 0x137: case 0x138: return 0;
        case WM_CANCELMODE: if (g_capture == hwnd) g_capture = 0; return 0;
        case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR: return 0;
    }
    return 0;
}

// ============================================================ Menues
static Menu* MN(uint32_t h) { auto it = g_menus.find(h); return it == g_menus.end() ? nullptr : &it->second; }
static uint32_t menu_new(bool popup) { uint32_t h = g_mnext; g_mnext += 4; g_menus[h].popup = popup; return h; }
static int parse_menu(uint32_t& p, uint32_t hm) {
    for (int guard = 0; guard < 500; guard++) {
        uint16_t opt = rd16(p); p += 2;
        MenuItem it; it.flags = opt & ~0x80;
        if (!(opt & 0x10)) { it.id = rd16(p); p += 2; }
        std::string t; for (;;) { uint16_t u = rd16(p); p += 2; if (!u) break; extern uint8_t u_to_cp1252(uint16_t); t += (char)u_to_cp1252(u); }
        it.text = t;
        if (!(opt & 0x10) && it.id == 0 && t.empty()) it.flags |= 0x800;
        if (opt & 0x10) { it.sub = menu_new(true); parse_menu(p, it.sub); }
        g_menus[hm].items.push_back(it);
        if (opt & 0x80) break;
    }
    return 0;
}
static MenuItem* menu_find(uint32_t hm, uint32_t id, uint32_t flags, uint32_t* owner = nullptr, int* idx = nullptr) {
    Menu* m = MN(hm); if (!m) return nullptr;
    if (flags & 0x400) { if (id < m->items.size()) { if (owner) *owner = hm; if (idx) *idx = id; return &m->items[id]; } return nullptr; }
    for (size_t i = 0; i < m->items.size(); i++) {
        if (!(m->items[i].flags & 0x10) && m->items[i].id == id) { if (owner) *owner = hm; if (idx) *idx = i; return &m->items[i]; }
        if (m->items[i].sub) if (MenuItem* r = menu_find(m->items[i].sub, id, flags, owner, idx)) return r;
    }
    return nullptr;
}
static void menu_dirty_all() { for (uint32_t h : g_top) if (Wnd* w = W(h)) if (w->menu) w->nc_dirty = true; }

// Popup-Darstellung
struct PopLevel { uint32_t hm; Rect r; std::vector<Rect> items; int hi = -1; ScreenSave save; };
int ui_text_w(const char* s, bool bold);
static void pop_layout(PopLevel& L, int x, int y) {
    Menu* m = MN(L.hm); int wmax = 80, h = 3;
    L.items.clear();
    for (auto& it : m->items) {
        int ih = (it.flags & 0x800) ? 8 : 20;
        std::string t = strip_amp(it.text); size_t tab = t.find('\t');
        int tw = (tab == std::string::npos) ? ui_text_w(t.c_str(), false) : ui_text_w(t.substr(0, tab).c_str(), false) + 24 + ui_text_w(t.substr(tab + 1).c_str(), false);
        wmax = std::max(wmax, tw + 44);
        L.items.push_back({0, h, 0, h + ih}); h += ih;
    }
    h += 3;
    if (x + wmax > SCR_W) x = SCR_W - wmax; if (y + h > SCR_H) y = std::max(0, SCR_H - h); if (x < 0) x = 0;
    L.r = {x, y, x + wmax, y + h};
    for (auto& r : L.items) { r = rOff(r, x, y); r.l = x + 3; r.r = x + wmax - 3; }
}
static void pop_draw(PopLevel& L) {
    Menu* m = MN(L.hm);
    scr_fill(L.r, 0xC0C0C0); scr_frame3d(L.r, true); scr_frame3d({L.r.l + 1, L.r.t + 1, L.r.r - 1, L.r.b - 1}, true);
    for (size_t i = 0; i < m->items.size() && i < L.items.size(); i++) {
        auto& it = m->items[i]; Rect r = L.items[i];
        if (it.flags & 0x800) { scr_fill({r.l + 2, r.t + 3, r.r - 2, r.t + 4}, 0x808080); scr_fill({r.l + 2, r.t + 4, r.r - 2, r.t + 5}, 0xFFFFFF); continue; }
        bool hi = (int)i == L.hi;
        if (hi) scr_fill(r, 0x800000);
        uint32_t col = (it.flags & 3) ? 0x808080 : hi ? 0xFFFFFF : 0x000000;
        std::string t = strip_amp(it.text); size_t tab = t.find('\t');
        scr_text({r.l + 18, r.t, r.r - 16, r.b}, tab == std::string::npos ? t.c_str() : t.substr(0, tab).c_str(), col, 0);
        if (tab != std::string::npos) scr_text({r.l + 18, r.t, r.r - 14, r.b}, t.substr(tab + 1).c_str(), col, 2);
        if (it.flags & 8) { int cx = r.l + 8, cy = (r.t + r.b) / 2; for (int k = 0; k < 3; k++) scr_fill({cx - 3 + k, cy - 1 + k, cx - 2 + k, cy + 1 + k}, col), scr_fill({cx + k, cy + 1 - k * 2, cx + 1 + k, cy + 3 - k * 2}, col); }
        if (it.flags & 0x10) { int cx = r.r - 9, cy = (r.t + r.b) / 2; for (int k = 0; k < 4; k++) scr_fill({cx + k, cy - 3 + k, cx + k + 1, cy + 4 - k}, col); }
    }
}
int menu_track(uint32_t hmenu, int x, int y, uint32_t owner, bool bar, int startIdx) {
    Wnd* ow = W(owner);
    std::vector<PopLevel> st;
    std::vector<Rect> baritems; Rect barr{};
    int barIdx = startIdx;
    if (bar && ow) { barr = menubar_rect(ow); baritems = menubar_items(ow, barr); send_msg(owner, WM_INITMENU, hmenu, 0); }
    ScreenSave barSave; if (bar) screen_save(barSave, barr);
    auto open_pop = [&](uint32_t hm, int px, int py, int index) {
        if (ow) send_msg(owner, WM_INITMENUPOPUP, hm, index & 0xFFFF);
        PopLevel L; L.hm = hm; pop_layout(L, px, py); screen_save(L.save, L.r); pop_draw(L); st.push_back(std::move(L)); };
    auto close_to = [&](size_t n) { while (st.size() > n) { screen_restore(st.back().save); st.pop_back(); } };
    auto draw_bar = [&]() {
        if (!bar) return; screen_restore(barSave);
        Menu* m = MN(hmenu);
        for (size_t i = 0; i < baritems.size() && m && i < m->items.size(); i++) if ((int)i == barIdx) {
            scr_fill(baritems[i], 0x800000);
            scr_text(baritems[i], strip_amp(m->items[i].text).c_str(), 0xFFFFFF, 1); }
    };
    auto open_bar = [&](int i) {
        close_to(0); barIdx = i; draw_bar();
        Menu* m = MN(hmenu); if (!m || i < 0 || i >= (int)m->items.size()) return;
        if (m->items[i].sub) open_pop(m->items[i].sub, baritems[i].l, baritems[i].b, i); };
    if (bar) open_bar(startIdx); else open_pop(hmenu, x, y, 0);
    int result = 0; bool done = false; bool downInside = false;
    plat_present(true);
    while (!done) {
        plat_poll();
        InputEv e;
        while (plat_next_event(e)) {
            if (e.type == EV_MOUSEMOVE || e.type == EV_LDOWN || e.type == EV_LUP) {
                int hitL = -1, hitI = -1;
                for (int l = (int)st.size() - 1; l >= 0 && hitL < 0; l--) if (st[l].r.has(e.x, e.y)) { hitL = l;
                    for (size_t i = 0; i < st[l].items.size(); i++) if (st[l].items[i].has(e.x, e.y)) hitI = i; }
                int hitBar = -1; if (bar) for (size_t i = 0; i < baritems.size(); i++) if (baritems[i].has(e.x, e.y)) hitBar = i;
                if (hitL >= 0) {
                    PopLevel& L = st[hitL];
                    if (hitI != L.hi) { close_to(hitL + 1); L.hi = hitI; pop_draw(L);
                        Menu* m = MN(L.hm);
                        if (hitI >= 0 && m->items[hitI].sub && !(m->items[hitI].flags & 3)) open_pop(m->items[hitI].sub, L.r.r - 3, L.items[hitI].t - 3, hitI); }
                    if (e.type == EV_LDOWN) downInside = true;
                    if (e.type == EV_LUP && hitI >= 0) {
                        Menu* m = MN(L.hm); auto& it = m->items[hitI];
                        if (!(it.flags & (3 | 0x10 | 0x800))) { result = it.id; done = true; break; }
                    }
                } else if (hitBar >= 0) {
                    if (hitBar != barIdx && (e.type == EV_MOUSEMOVE ? true : true)) { open_bar(hitBar); }
                    else if (e.type == EV_LDOWN && downInside) { done = true; break; }
                    if (e.type == EV_LDOWN) downInside = true;
                } else if (e.type == EV_LDOWN) { done = true; break; }
            } else if (e.type == EV_KEY && e.down) {
                PopLevel* L = st.empty() ? nullptr : &st.back();
                Menu* m = L ? MN(L->hm) : nullptr;
                auto step = [&](int d) { if (!m || m->items.empty()) return; int n = m->items.size(), i = L->hi;
                    for (int k = 0; k < n; k++) { i = (i + d + n) % n; if (!(m->items[i].flags & 0x800)) break; }
                    L->hi = i; pop_draw(*L); };
                switch (e.key) {
                    case 0x26: step(-1); break;
                    case 0x28: step(1); break;
                    case 0x27: if (L && L->hi >= 0 && m->items[L->hi].sub) open_pop(m->items[L->hi].sub, L->r.r - 3, L->items[L->hi].t - 3, L->hi);
                               else if (bar) { Menu* bm = MN(hmenu); int n = bm ? bm->items.size() : 1; open_bar((barIdx + 1) % n); } break;
                    case 0x25: if (st.size() > 1) close_to(st.size() - 1); else if (bar) { Menu* bm = MN(hmenu); int n = bm ? bm->items.size() : 1; open_bar((barIdx + n - 1) % n); } break;
                    case 0x0D: if (L && L->hi >= 0) { auto& it = m->items[L->hi];
                        if (it.sub) open_pop(it.sub, L->r.r - 3, L->items[L->hi].t - 3, L->hi);
                        else if (!(it.flags & 3)) { result = it.id; done = true; } } break;
                    case 0x1B: if (st.size() > 1) close_to(st.size() - 1); else done = true; break;
                }
            }
            if (done) break;
        }
        timers_poll();
        plat_present();
        plat_sleep_ms(8);
    }
    close_to(0);
    if (bar) { screen_restore(barSave); if (ow) ow->nc_dirty = true; }
    plat_present(true);
    return result;
}

// ============================================================ MessageBox (modal)
static uint32_t message_box(uint32_t owner, const char* text, const char* cap, uint32_t type) {
    rlog("MessageBox: [%s] %s", cap ? cap : "", text ? text : "");
    static const char* namesDe[6][3] = { {"OK"}, {"OK", "Abbrechen"}, {"Abbrechen", "Wiederholen", "Ignorieren"}, {"Ja", "Nein", "Abbrechen"}, {"Ja", "Nein"}, {"Wiederholen", "Abbrechen"} };
    static const char* namesEn[6][3] = { {"OK"}, {"OK", "Cancel"}, {"Abort", "Retry", "Ignore"}, {"Yes", "No", "Cancel"}, {"Yes", "No"}, {"Retry", "Cancel"} };
    const char* (*names)[3] = g_lang == 1 ? namesEn : namesDe;
    static const uint32_t ids[6][3] = { {1}, {1, 2}, {3, 4, 5}, {6, 7, 2}, {6, 7}, {4, 2} };
    int t = type & 0xF; if (t > 5) t = 0;
    int nb = t == 0 ? 1 : (t == 2 || t == 3) ? 3 : 2;
    // Text umbrechen
    GDC tmp; (void)tmp;
    std::vector<std::string> lines; { std::string cur, word; std::string s = text ? text : "";
        auto flush_word = [&]() { if (ui_text_w((cur + word).c_str(), false) > 440 && !cur.empty()) { lines.push_back(cur); cur.clear(); } cur += word; word.clear(); };
        for (char ch : s) { if (ch == '\n') { flush_word(); lines.push_back(cur); cur.clear(); } else if (ch == '\r') {} else { word += ch; if (ch == ' ') flush_word(); } }
        flush_word(); if (!cur.empty()) lines.push_back(cur); }
    int tw = 200; for (auto& l : lines) tw = std::max(tw, ui_text_w(l.c_str(), false));
    int w = std::min(SCR_W - 20, tw + 40), h = 40 + 18 * (int)lines.size() + 44;
    Rect r{(SCR_W - w) / 2, (SCR_H - h) / 2, (SCR_W + w) / 2, (SCR_H + h) / 2};
    ScreenSave sv; screen_save(sv, r);
    scr_fill(r, 0xC0C0C0); scr_frame3d(r, true);
    Rect c{r.l + 3, r.t + 3, r.r - 3, r.t + 21}; scr_fill(c, 0x800000); scr_text(c, cap ? cap : "", 0xFFFFFF, 0, true);
    int y = r.t + 30; for (auto& l : lines) { scr_text({r.l + 16, y, r.r - 16, y + 18}, l.c_str(), 0, 0); y += 18; }
    std::vector<Rect> btn; int bw = 90, gap = 10, tot = nb * bw + (nb - 1) * gap, bx = (r.l + r.r - tot) / 2;
    for (int i = 0; i < nb; i++) { Rect b{bx + i * (bw + gap), r.b - 36, bx + i * (bw + gap) + bw, r.b - 12}; btn.push_back(b); scr_fill(b, 0xC0C0C0); scr_frame3d(b, true); scr_text(b, names[t][i], 0, 1); }
    uint32_t res = 0; int sel = 0;
    auto hil = [&]() { for (int i = 0; i < nb; i++) { Rect b = btn[i]; scr_fill({b.l - 2, b.t - 2, b.r + 2, b.b + 2}, 0xC0C0C0); scr_fill(b, 0xC0C0C0); scr_frame3d(b, true); if (i == sel) scr_frame3d({b.l - 2, b.t - 2, b.r + 2, b.b + 2}, false); scr_text(b, names[t][i], 0, 1); } };
    hil();
    plat_present(true);
    while (!res) {
        plat_poll(); InputEv e;
        while (plat_next_event(e)) {
            if (e.type == EV_LUP) for (int i = 0; i < nb; i++) if (btn[i].has(e.x, e.y)) res = ids[t][i];
            if (e.type == EV_KEY && e.down) {
                if (e.key == 0x0D) res = ids[t][sel];
                else if (e.key == 0x1B) res = ids[t][nb - 1];
                else if (e.key == 0x25 || e.key == 0x26) { sel = (sel + nb - 1) % nb; hil(); }
                else if (e.key == 0x27 || e.key == 0x28 || e.key == 0x09) { sel = (sel + 1) % nb; hil(); }
            }
        }
        plat_present(); plat_sleep_ms(10);
    }
    screen_restore(sv); plat_present(true);
    (void)owner; return res;
}

// ============================================================ Eingabe-Verteilung
static Wnd* child_at(Wnd* w, int x, int y) {
    for (auto it = w->kids.rbegin(); it != w->kids.rend(); ++it) {
        Wnd* c = W(*it); if (!c || !(c->style & WS_VISIBLE) || !c->wr.has(x, y)) continue;
        if (!client_rect_screen(c).has(x, y)) return c;   // NC des Kindes
        Wnd* d = child_at(c, x, y); return d ? d : c;
    }
    return nullptr;
}
static Wnd* window_at(int x, int y) {
    for (auto it = g_top.rbegin(); it != g_top.rend(); ++it) {
        Wnd* w = W(*it); if (!w || !(w->style & WS_VISIBLE) || !w->wr.has(x, y)) continue;
        if (!client_rect_screen(w).has(x, y)) return w;
        Wnd* c = child_at(w, x, y); return c ? c : w;
    }
    return nullptr;
}
static bool disabled_chain(Wnd* w) { for (; w; w = is_child(w) ? W(w->parent) : nullptr) if (w->style & WS_DISABLED) return true; return false; }
static uint32_t mk_flags() { uint32_t f = 0; if (g_lbutton) f |= 1; if (g_rbutton) f |= 2; if (g_keystate[0x10] & 0x80) f |= 4; if (g_keystate[0x11] & 0x80) f |= 8; return f; }
static uint32_t pt_lparam(Wnd* w, int x, int y) { Rect cs = client_rect_screen(w); return ((x - cs.l) & 0xFFFF) | (((y - cs.t) & 0xFFFF) << 16); }
static uint32_t g_lastclick_h = 0, g_lastclick_t = 0; static int g_lastclick_x = 0, g_lastclick_y = 0;
// Ziehen der Titelleiste
static Wnd* g_drag = nullptr; static int g_dragdx = 0, g_dragdy = 0;
// modaler Scrollbalken-Klick
void scroll_click(Wnd* w, int bar, const Rect& r, int x, int y);

static void route_event(const InputEv& e) {
    if (e.type == EV_KEY) rlog("Taste vk=%02x %s -> fokus=%08x aktiv=%08x", e.key, e.down ? "ab" : "auf", g_focus, g_active);
    else if (e.type == EV_LDOWN || e.type == EV_RDOWN) { Wnd* t = window_at(e.x, e.y); rlog("Klick %s (%d,%d) -> %08x", e.type == EV_LDOWN ? "L" : "R", e.x, e.y, t ? t->h : 0); }
    if (e.type == EV_KEY) {
        uint32_t vk = e.key & 0xFF;
        for (auto& hk : g_hooks) if (hk.id == 2) { uint32_t lp = 1 | ((vk & 0xFF) << 16) | (e.down ? 0 : 0xC0000000u); if (gcall(hk.proc, { 0, vk, lp })) return; }
        uint32_t tgt = focus_hwnd(); if (!tgt) tgt = W(g_active) ? g_active : 0;
        if (!tgt) return;
        bool alt = (g_keystate[0x12] & 0x80) != 0;
        uint32_t msg = e.down ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP);
        uint32_t lp = 1 | ((vk & 0xFF) << 16) | (alt ? 0x20000000u : 0) | (e.down ? 0 : 0xC0000000u);
        post_msg(tgt, msg, vk, lp);
        return;
    }
    int x = e.x, y = e.y;
    if (g_drag) {
        if (e.type == EV_LUP) { Wnd* w = g_drag; g_drag = nullptr;
            int nx = x - g_dragdx, ny = y - g_dragdy;
            Rect nr = rOff(w->wr, nx - w->wr.l, ny - w->wr.t);
            if (nr.t < 0) nr = rOff(nr, 0, -nr.t);
            move_window(w, nr, true); }
        return;
    }
    Wnd* tgt = W(g_capture) ? W(g_capture) : window_at(x, y);
    if (e.type == EV_MOUSEMOVE) { if (tgt && (W(g_capture) || client_rect_screen(tgt).has(x, y))) post_msg(tgt->h, WM_MOUSEMOVE, mk_flags(), pt_lparam(tgt, x, y)); return; }
    if (!tgt) return;
    if (!W(g_capture) && disabled_chain(tgt)) { if (e.type == EV_LDOWN) rlog("Klick auf gesperrtes Fenster"); return; }
    bool down = (e.type == EV_LDOWN || e.type == EV_RDOWN);
    if (down && !W(g_capture)) {
        Wnd* top = toplevel_of(tgt);
        if (top && top->h != g_active) { bring_to_top(top); activate(top); }
        else if (top && g_top.back() != top->h) bring_to_top(top);
        // Kindfenster mit Titelleiste (MDI-artig) nach vorn
        for (Wnd* c = tgt; c && is_child(c); c = W(c->parent)) if ((c->style & WS_CAPTION) == WS_CAPTION) { bring_to_top(c); break; }
    }
    // Nicht-Clientbereich
    if (!W(g_capture) && !client_rect_screen(tgt).has(x, y)) {
        if (e.type != EV_LDOWN) return;
        if (!is_child(tgt) && tgt->menu && MN(tgt->menu) && menubar_rect(tgt).has(x, y)) {
            auto items = menubar_items(tgt, menubar_rect(tgt));
            for (size_t i = 0; i < items.size(); i++) if (items[i].has(x, y)) {
                int id = menu_track(tgt->menu, 0, 0, tgt->h, true, i);
                if (id) post_msg(tgt->h, WM_COMMAND, id & 0xFFFF, 0);
                break; }
            return;
        }
        if ((tgt->style & WS_VSCROLL) && sb_rect(tgt, 1).has(x, y)) { scroll_click(tgt, 1, sb_rect(tgt, 1), x, y); return; }
        if ((tgt->style & WS_HSCROLL) && sb_rect(tgt, 0).has(x, y)) { scroll_click(tgt, 0, sb_rect(tgt, 0), x, y); return; }
        if ((tgt->style & WS_CAPTION) == WS_CAPTION && caption_rect(tgt).has(x, y)) {
            if ((tgt->style & WS_SYSMENU) && closebox_rect(tgt).has(x, y)) { post_msg(tgt->h, WM_SYSCOMMAND, 0xF060, 0); return; }
            if (!tgt->maximized) { g_drag = tgt; g_dragdx = x - tgt->wr.l; g_dragdy = y - tgt->wr.t; }
        }
        return;
    }
    uint32_t msg = 0;
    switch (e.type) {
        case EV_LDOWN: msg = WM_LBUTTONDOWN;
            if ((tgt->cls && (tgt->cls->style & 8)) && g_lastclick_h == tgt->h && now_ms() - g_lastclick_t < 500 && abs(x - g_lastclick_x) < 8 && abs(y - g_lastclick_y) < 8) { msg = WM_LBUTTONDBLCLK; g_lastclick_h = 0; }
            else { g_lastclick_h = tgt->h; g_lastclick_t = now_ms(); g_lastclick_x = x; g_lastclick_y = y; }
            break;
        case EV_LUP: msg = WM_LBUTTONUP; break;
        case EV_RDOWN: msg = WM_RBUTTONDOWN; break;
        case EV_RUP: msg = WM_RBUTTONUP; break;
    }
    if (msg) post_msg(tgt->h, msg, mk_flags(), pt_lparam(tgt, x, y));
}

// ============================================================ Paint-Suche / Nachrichtenschleife
static Wnd* find_paint(Wnd* w) {
    if (!(w->style & WS_VISIBLE)) return nullptr;
    if (!w->upd.empty()) return w;
    for (uint32_t k : w->kids) if (Wnd* c = W(k)) if (Wnd* r = find_paint(c)) return r;
    return nullptr;
}
static void nc_pass(Wnd* w) { if (!(w->style & WS_VISIBLE)) return; if (w->nc_dirty) draw_nc(w); for (uint32_t k : w->kids) if (Wnd* c = W(k)) nc_pass(c); }
static uint32_t g_lastpresent = 0; static int g_idle = 0;
static void wr_msg(uint32_t p, const QMsg& m) { wr32(p, m.hwnd); wr32(p + 4, m.msg); wr32(p + 8, m.wp); wr32(p + 12, m.lp); wr32(p + 16, m.time); wr32(p + 20, m.x); wr32(p + 24, m.y); }
static bool msg_match(const QMsg& m, uint32_t hwnd, uint32_t mn, uint32_t mx) {
    if (hwnd && m.hwnd != hwnd) { Wnd* w = W(m.hwnd); bool ch = false; while (w && is_child(w)) { if (w->parent == hwnd) { ch = true; break; } w = W(w->parent); } if (!ch) return false; }
    if (mn || mx) { if (m.msg < mn || m.msg > mx) return false; }
    return true;
}
void pump_internal() {
    // gedrosselt: Eingabe/Timer/Rahmen hoechstens alle 3 ms, Bild alle 16 ms (PeekMessage muss billig sein)
    static uint64_t last = 0; uint64_t tu = now_us();
    if (tu - last < 3000) return;
    last = tu;
    timers_poll();
    plat_poll();
    InputEv e; while (plat_next_event(e)) route_event(e);
    for (uint32_t h : std::vector<uint32_t>(g_top)) if (Wnd* w = W(h)) nc_pass(w);
    uint32_t t = now_ms();
    if (t - g_lastpresent >= 16) { plat_present(); g_lastpresent = t; }
}
static bool peek(uint32_t pmsg, uint32_t hwnd, uint32_t mn, uint32_t mx, uint32_t flags) {
    pump_internal();
    for (auto it = g_q.begin(); it != g_q.end(); ++it) {
        if (!msg_match(*it, hwnd, mn, mx)) continue;
        QMsg m = *it; if (flags & 1) g_q.erase(it);
        wr_msg(pmsg, m);
        for (auto& hk : g_hooks) if (hk.id == 3) gcall(hk.proc, { 0, flags & 1, pmsg });
        g_idle = 0; return true;
    }
    // WM_PAINT
    if (!mn || (mn <= WM_PAINT && mx >= WM_PAINT)) {
        for (uint32_t h : g_top) if (Wnd* w = W(h)) if (Wnd* p = find_paint(w)) {
            if (hwnd && p->h != hwnd) continue;
            QMsg m{p->h, WM_PAINT, 0, 0, now_ms(), g_mouse_x, g_mouse_y}; wr_msg(pmsg, m); g_idle = 0; return true; }
    }
    // WM_TIMER
    if (!mn || (mn <= WM_TIMER && mx >= WM_TIMER)) {
        uint32_t t = now_ms();
        for (auto& tm : g_timers) if ((int32_t)(t - tm.next) >= 0 && (!hwnd || tm.hwnd == hwnd)) {
            if (flags & 1) tm.next = t + (tm.ms ? tm.ms : 1);
            QMsg m{tm.hwnd, WM_TIMER, tm.id, tm.proc, t, g_mouse_x, g_mouse_y}; wr_msg(pmsg, m); g_idle = 0; return true; }
    }
    // Leerlauf: nur gelegentlich kurz schlafen (max. ~6 % der Zeit), damit Rechenschleifen mit PeekMessage schnell bleiben
    if (++g_idle > 200) { static uint64_t lastSleep = 0; uint64_t tu = now_us();
        if (tu - lastSleep > 16000) { plat_sleep_ms(1); lastSleep = now_us(); } }
    return false;
}

// ============================================================ wsprintfA
static std::string wsprintf_impl(const char* f, Cpu& c, int argi) {
    std::string out; char buf[256];
    while (*f) {
        if (*f != '%') { out += *f++; continue; }
        f++;
        if (*f == '%') { out += '%'; f++; continue; }
        std::string spec = "%";
        while (*f == '-' || *f == '#' || *f == '0' || *f == ' ' || *f == '+') spec += *f++;
        while (*f >= '0' && *f <= '9') spec += *f++;
        if (*f == '.') { spec += *f++; while (*f >= '0' && *f <= '9') spec += *f++; }
        bool hs = false;
        if (*f == 'l' || *f == 'h') { if (*f == 'h' && f[1] == 's') hs = true; f++; }
        char t = *f++;
        uint32_t v = c.arg(argi++);
        switch (t) {
            case 'd': case 'i': snprintf(buf, sizeof buf, (spec + "d").c_str(), (int)v); out += buf; break;
            case 'u': snprintf(buf, sizeof buf, (spec + "u").c_str(), v); out += buf; break;
            case 'x': snprintf(buf, sizeof buf, (spec + "x").c_str(), v); out += buf; break;
            case 'X': snprintf(buf, sizeof buf, (spec + "X").c_str(), v); out += buf; break;
            case 'c': out += (char)v; break;
            case 's': case 'S': { const char* s = v ? GS(v) : "(null)"; snprintf(buf, sizeof buf, (spec + "s").c_str(), s); out += buf; (void)hs; break; }
            default: out += t; break;
        }
    }
    return out;
}

// ============================================================ Install
uint32_t g_ctl_va = 0;
uint32_t ctl_trap_va() { return g_ctl_va; }
int ui_text_w(const char* s, bool bold) { GFont f = sys_font(); f.weight = bold ? 700 : 400; f.h = -12; font_setup(f); return text_width(f, s, strlen(s)); }
int scr_text_clipped(Rect r, Rect clip, const char* s, uint32_t cr, int align, bool bold) {
    Surf S; S.base = (uint8_t*)g_screen; S.pitch = SCR_W * 4; S.w = SCR_W; S.h = SCR_H; S.fmt = PF_SCR; S.clip.push_back(rI(clip, {0, 0, SCR_W, SCR_H}));
    static GFont fn, fb; static bool init = false;
    if (!init) { fn.face = "Arial"; fn.h = -12; fn.weight = 400; font_setup(fn); fb = fn; fb.weight = 700; font_setup(fb); init = true; }
    GFont& f = bold ? fb : fn; int n = strlen(s); int w = text_width(f, s, n);
    int x = align == 1 ? r.l + (r.w() - w) / 2 : align == 2 ? r.r - w - 2 : r.l + 2;
    int y = r.t + (r.h() - f.height) / 2;
    text_draw(S, f, x, y, s, n, cr, false, -1); g_screen_dirty = true; return w;
}

void install_user32() {
    const char* U = "USER32.dll";
    reg(U, "__CtlProc", 4, [](Cpu& c) { Wnd* w = W(A(0)); return w ? ctl_proc(w, A(1), A(2), A(3)) : 0u; });
    // ---- Klassen
    reg(U, "RegisterClassA", 1, [](Cpu& c) -> uint32_t {
        uint32_t wc = A(0); WClass* k = new WClass;
        k->style = rd32(wc); k->proc = rd32(wc + 4); k->cbCls = rd32(wc + 8); k->cbWnd = rd32(wc + 12); k->hinst = rd32(wc + 16);
        k->icon = rd32(wc + 20); k->cursor = rd32(wc + 24); k->brush = rd32(wc + 28); k->menuName = rd32(wc + 32);
        uint32_t nm = rd32(wc + 36); k->name = is_intres(nm) ? ("#" + std::to_string(nm)) : GS(nm);
        for (auto& ch : k->name) ch = toupper(ch);
        k->clsExtra.assign(k->cbCls, 0);
        static uint16_t atom = 0xC000; k->atom = atom++;
        g_classes[k->name] = k; g_atoms[k->atom] = k;
        rlog("RegisterClass %s proc=%08x", k->name.c_str(), k->proc);
        return k->atom; });
    reg(U, "UnregisterClassA", 2, [](Cpu&) { return 1u; });
    reg(U, "GetClassNameA", 3, [](Cpu& c) { Wnd* w = W(A(0)); if (!w || !A(2)) return 0u; strncpy(GS(A(1)), w->cls->name.c_str(), A(2)); GS(A(1))[A(2) - 1] = 0; return (uint32_t)strlen(GS(A(1))); });
    reg(U, "GetClassLongA", 2, [](Cpu& c) -> uint32_t {
        Wnd* w = W(A(0)); if (!w) return 0; WClass* k = w->cls; int i = (int)A(1);
        switch (i) { case -8: return k->menuName; case -10: return k->brush; case -12: return k->cursor; case -14: return k->icon; case -16: return k->hinst;
            case -18: return k->cbWnd; case -20: return k->cbCls; case -24: return k->proc; case -26: return k->style; case -32: return k->atom; }
        if (i >= 0 && i + 4 <= (int)k->clsExtra.size()) { uint32_t v; memcpy(&v, &k->clsExtra[i], 4); return v; }
        return 0; });
    reg(U, "SetClassLongA", 3, [](Cpu& c) -> uint32_t {
        Wnd* w = W(A(0)); if (!w) return 0; WClass* k = w->cls; int i = (int)A(1); uint32_t v = A(2), o = 0;
        switch (i) { case -8: o = k->menuName; k->menuName = v; return o; case -10: o = k->brush; k->brush = v; return o; case -12: o = k->cursor; k->cursor = v; return o;
            case -14: o = k->icon; k->icon = v; return o; case -24: o = k->proc; k->proc = v; return o; case -26: o = k->style; k->style = v; return o; }
        if (i >= 0 && i + 4 <= (int)k->clsExtra.size()) { memcpy(&o, &k->clsExtra[i], 4); memcpy(&k->clsExtra[i], &v, 4); }
        return o; });
    // ---- Fenster
    reg(U, "CreateWindowExA", 12, [](Cpu& c) {
        uint32_t h = create_window(A(0), A(1), A(2), A(3), (int)A(4), (int)A(5), (int)A(6), (int)A(7), A(8), A(9), A(10), A(11));
        rlog("CreateWindowEx(%s, \"%s\", style=%08x, %d,%d %dx%d, parent=%08x) -> %08x", is_intres(A(1)) ? "#" : GS(A(1)), A(2) ? GS(A(2)) : "", A(3), (int)A(4), (int)A(5), (int)A(6), (int)A(7), A(8), h);
        return h; });
    reg(U, "DestroyWindow", 1, [](Cpu& c) { rlog("DestroyWindow(%08x)", A(0)); destroy_window(A(0)); return 1u; });
    reg(U, "ShowWindow", 2, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) return 0u; rlog("ShowWindow(%08x \"%s\", %u)", A(0), w->text.c_str(), A(1)); bool was = (w->style & WS_VISIBLE) != 0; show_window(w, A(1)); return was ? 1u : 0u; });
    reg(U, "UpdateWindow", 1, [](Cpu& c) {
        Wnd* w = W(A(0)); if (!w) return 0u;
        if (w->nc_dirty) draw_nc(w);
        if (!w->upd.empty() && wnd_visible(w)) send_msg(w->h, WM_PAINT, 0, 0);
        return 1u; });
    reg(U, "IsWindow", 1, [](Cpu& c) { return W(A(0)) ? 1u : 0u; });
    reg(U, "IsWindowVisible", 1, [](Cpu& c) { Wnd* w = W(A(0)); return (w && wnd_visible(w)) ? 1u : 0u; });
    reg(U, "IsIconic", 1, [](Cpu& c) { Wnd* w = W(A(0)); return (w && w->minimized) ? 1u : 0u; });
    reg(U, "IsZoomed", 1, [](Cpu& c) { Wnd* w = W(A(0)); return (w && w->maximized) ? 1u : 0u; });
    reg(U, "IsChild", 2, [](Cpu& c) { Wnd* w = W(A(1)); while (w && is_child(w)) { if (w->parent == A(0)) return 1u; w = W(w->parent); } return 0u; });
    reg(U, "GetParent", 1, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) return 0u; return is_child(w) ? w->parent : w->owner; });
    reg(U, "GetWindow", 2, [](Cpu& c) -> uint32_t {
        Wnd* w = W(A(0)); if (!w) return 0;
        uint32_t cmd = A(1);
        if (cmd == 4) return w->owner;
        if (cmd == 5) return w->kids.empty() ? 0 : w->kids.back();
        auto& sib = siblings(w);
        if (sib.empty()) return 0;
        if (cmd == 0) return sib.back(); if (cmd == 1) return sib.front();
        auto it = std::find(sib.begin(), sib.end(), w->h); if (it == sib.end()) return 0;
        if (cmd == 2) return it == sib.begin() ? 0 : *(it - 1);
        if (cmd == 3) return (it + 1) == sib.end() ? 0 : *(it + 1);
        return 0; });
    reg(U, "FindWindowA", 2, [](Cpu& c) -> uint32_t {
        for (uint32_t h : g_top) { Wnd* w = W(h); if (!w) continue;
            if (A(0) && !is_intres(A(0)) && strcasecmp(w->cls->name.c_str(), GS(A(0)))) continue;
            if (A(1) && w->text != GS(A(1))) continue;
            return h; }
        return 0; });
    reg(U, "GetWindowLongA", 2, [](Cpu& c) -> uint32_t {
        Wnd* w = W(A(0)); if (!w) return 0; int i = (int)A(1);
        switch (i) { case -4: return w->proc; case -6: return w->hinst; case -8: return w->owner; case -12: return w->id; case -16: return w->style; case -20: return w->exstyle; case -21: return w->userdata; }
        if (i >= 0 && i + 4 <= (int)w->extra.size()) { uint32_t v; memcpy(&v, &w->extra[i], 4); return v; }
        return 0; });
    reg(U, "SetWindowLongA", 3, [](Cpu& c) -> uint32_t {
        Wnd* w = W(A(0)); if (!w) return 0; int i = (int)A(1); uint32_t v = A(2), o = 0;
        switch (i) {
            case -4: o = w->proc; w->proc = v; return o;
            case -6: o = w->hinst; w->hinst = v; return o;
            case -8: o = w->owner; w->owner = v; return o;
            case -12: o = w->id; w->id = v; return o;
            case -16: o = w->style; w->style = v; w->nc_dirty = true; return o;
            case -20: o = w->exstyle; w->exstyle = v; return o;
            case -21: o = w->userdata; w->userdata = v; return o; }
        if (i >= 0 && i + 4 <= (int)w->extra.size()) { memcpy(&o, &w->extra[i], 4); memcpy(&w->extra[i], &v, 4); }
        return o; });
    reg(U, "SetWindowTextA", 2, [](Cpu& c) { return send_msg(A(0), WM_SETTEXT, 0, A(1)) ? 1u : 1u; });
    reg(U, "GetWindowTextA", 3, [](Cpu& c) { return send_msg(A(0), WM_GETTEXT, A(2), A(1)); });
    reg(U, "GetWindowTextLengthA", 1, [](Cpu& c) { return send_msg(A(0), WM_GETTEXTLENGTH, 0, 0); });
    reg(U, "EnableWindow", 2, [](Cpu& c) {
        Wnd* w = W(A(0)); if (!w) return 0u; bool was = (w->style & WS_DISABLED) != 0;
        if (A(1)) w->style &= ~WS_DISABLED; else w->style |= WS_DISABLED;
        if (was != !A(1)) { send_msg(w->h, WM_ENABLE, A(1) ? 1 : 0, 0); if (w->ctl) invalidate(w, nullptr, true); }
        return was ? 1u : 0u; });
    reg(U, "IsWindowEnabled", 1, [](Cpu& c) { Wnd* w = W(A(0)); return (w && !(w->style & WS_DISABLED)) ? 1u : 0u; });
    reg(U, "MoveWindow", 6, [](Cpu& c) {
        Wnd* w = W(A(0)); if (!w) return 0u;
        Rect pc{0, 0, SCR_W, SCR_H}; if (is_child(w)) if (Wnd* p = W(w->parent)) pc = client_rect_screen(p);
        int x = (int)A(1), y = (int)A(2);
        w->maximized = false;
        move_window(w, {pc.l + x, pc.t + y, pc.l + x + (int)A(3), pc.t + y + (int)A(4)}, A(5)); return 1u; });
    reg(U, "SetWindowPos", 7, [](Cpu& c) {
        Wnd* w = W(A(0)); if (!w) return 0u; uint32_t fl = A(6);
        if (!(fl & 4)) { uint32_t after = A(1);
            if (after == 0 || after == 0xFFFFFFFFu) bring_to_top(w);
            else if (after == 1) { auto& sib = siblings(w); auto it = std::find(sib.begin(), sib.end(), w->h); if (it != sib.end()) { sib.erase(it); sib.insert(sib.begin(), w->h); expose_screen(w->wr, 0); } } }
        Rect pc{0, 0, SCR_W, SCR_H}; if (is_child(w)) if (Wnd* p = W(w->parent)) pc = client_rect_screen(p);
        Rect nr = w->wr;
        if (!(fl & 2)) nr = rOff(nr, pc.l + (int)A(2) - nr.l, pc.t + (int)A(3) - nr.t);
        if (!(fl & 1)) { nr.r = nr.l + (int)A(4); nr.b = nr.t + (int)A(5); }
        if (!(fl & 3)) w->maximized = false;
        move_window(w, nr, !(fl & 8));
        if (fl & 0x20) { w->nc_dirty = true; invalidate(w, nullptr, true); send_size_move(w); }
        if (fl & 0x40) show_window(w, (fl & 0x10) ? 8 : 5);
        if (fl & 0x80) show_window(w, 0);
        return 1u; });
    reg(U, "BringWindowToTop", 1, [](Cpu& c) { Wnd* w = W(A(0)); if (w) { bring_to_top(w); if (!is_child(w)) activate(w); } return 1u; });
    reg(U, "SetActiveWindow", 1, [](Cpu& c) { uint32_t o = g_active; Wnd* w = W(A(0)); if (w) activate(toplevel_of(w)); return o; });
    reg(U, "GetActiveWindow", 0, [](Cpu&) { return g_active; });
    reg(U, "SetForegroundWindow", 1, [](Cpu& c) { Wnd* w = W(A(0)); if (w) activate(toplevel_of(w)); return 1u; });
    reg(U, "GetForegroundWindow", 0, [](Cpu&) { return g_active; });
    reg(U, "GetDesktopWindow", 0, [](Cpu&) { return 0x0000FFF0u; });
    reg(U, "GetWindowRect", 2, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) { if (A(0) == 0xFFF0) { wr_rect(A(1), {0, 0, SCR_W, SCR_H}); return 1u; } return 0u; } wr_rect(A(1), w->wr); return 1u; });
    reg(U, "GetClientRect", 2, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) { wr_rect(A(1), {0, 0, SCR_W, SCR_H}); return A(0) == 0xFFF0 ? 1u : 0u; } wr_rect(A(1), client_rect_local(w)); return 1u; });
    reg(U, "ClientToScreen", 2, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) return 0u; Rect cs = client_rect_screen(w); wr32(A(1), rd32(A(1)) + cs.l); wr32(A(1) + 4, rd32(A(1) + 4) + cs.t); return 1u; });
    reg(U, "ScreenToClient", 2, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) return 0u; Rect cs = client_rect_screen(w); wr32(A(1), rd32(A(1)) - cs.l); wr32(A(1) + 4, rd32(A(1) + 4) - cs.t); return 1u; });
    reg(U, "MapWindowPoints", 4, [](Cpu& c) {
        Wnd* a = W(A(0)); Wnd* b = W(A(1));
        int ax = a ? client_rect_screen(a).l : 0, ay = a ? client_rect_screen(a).t : 0, bx = b ? client_rect_screen(b).l : 0, by = b ? client_rect_screen(b).t : 0;
        int dx = ax - bx, dy = ay - by;
        for (uint32_t i = 0; i < A(3); i++) { uint32_t p = A(2) + 8 * i; wr32(p, rd32(p) + dx); wr32(p + 4, rd32(p + 4) + dy); }
        return (uint32_t)((dx & 0xFFFF) | ((dy & 0xFFFF) << 16)); });
    reg(U, "WindowFromPoint", 2, [](Cpu& c) { Wnd* w = window_at((int)A(0), (int)A(1)); return w ? w->h : 0u; });
    // ---- Fokus / Capture / Cursor / Tastatur
    reg(U, "SetFocus", 1, [](Cpu& c) { uint32_t o = g_focus; set_focus(A(0)); return o; });
    reg(U, "GetFocus", 0, [](Cpu&) { return focus_hwnd(); });
    reg(U, "SetCapture", 1, [](Cpu& c) { uint32_t o = g_capture; g_capture = A(0); return o; });
    reg(U, "ReleaseCapture", 0, [](Cpu&) { g_capture = 0; return 1u; });
    reg(U, "GetCapture", 0, [](Cpu&) { return g_capture; });
    reg(U, "GetCursorPos", 1, [](Cpu& c) { wr32(A(0), g_mouse_x); wr32(A(0) + 4, g_mouse_y); return 1u; });
    reg(U, "SetCursorPos", 2, [](Cpu& c) { g_mouse_x = A(0); g_mouse_y = A(1); return 1u; });
    reg(U, "ShowCursor", 1, [](Cpu& c) { g_cursor_show += A(0) ? 1 : -1; plat_draw_cursor(g_cursor_show >= 0); return (uint32_t)g_cursor_show; });
    reg(U, "SetCursor", 1, [](Cpu& c) {
        static uint32_t cur = 0; uint32_t o = cur; cur = A(0);
        auto it = g_cursors.find(cur);
        plat_set_cursor(it != g_cursors.end() ? &it->second : nullptr);
        return o; });
    reg(U, "LoadCursorA", 2, [](Cpu& c) -> uint32_t {
        if (A(0)) { uint32_t h = load_cursor_res(A(0), A(1)); if (h) return h; }
        return 0x00700000u + (is_intres(A(1)) ? A(1) : 1); });
    reg(U, "LoadIconA", 2, [](Cpu& c) { return 0x00710000u + (is_intres(A(1)) ? A(1) : 1); });
    reg(U, "DestroyIcon", 1, [](Cpu&) { return 1u; });
    reg(U, "DestroyCursor", 1, [](Cpu&) { return 1u; });
    reg(U, "DrawIcon", 4, [](Cpu&) { return 1u; });
    reg(U, "MessageBeep", 1, [](Cpu&) { return 1u; });
    reg(U, "GetAsyncKeyState", 1, [](Cpu& c) -> uint32_t {
        uint32_t vk = A(0) & 0xFF;
        if (vk == 1) return g_lbutton ? 0x8000 : 0; if (vk == 2) return g_rbutton ? 0x8000 : 0;
        return (g_keystate[vk] & 0x80) ? 0x8000 : 0; });
    reg(U, "GetKeyState", 1, [](Cpu& c) -> uint32_t {
        uint32_t vk = A(0) & 0xFF;
        if (vk == 1) return g_lbutton ? 0xFFFF8000u : 0; if (vk == 2) return g_rbutton ? 0xFFFF8000u : 0;
        uint32_t r = (g_keystate[vk] & 0x80) ? 0xFFFF8000u : 0; if (g_keystate[vk] & 1) r |= 1; return r; });
    reg(U, "GetSystemMetrics", 1, [](Cpu& c) -> uint32_t {
        switch (A(0)) {
            case 0: case 16: case 78: return SCR_W; case 1: case 79: return SCR_H; case 17: return SCR_H - 19;
            case 2: case 3: case 9: case 10: case 20: case 21: return 16;
            case 4: return 19; case 5: case 6: return 1; case 7: case 8: return 3;
            case 11: case 12: case 13: case 14: return 32; case 15: return 19; case 19: return 1;
            case 28: return 112; case 29: return 27; case 30: case 31: return 18; case 32: case 33: return 4;
            case 34: return 112; case 35: return 27; case 36: case 37: return 4; case 38: case 39: return 75;
            case 49: case 50: return 16; case 61: return SCR_W + 8; case 62: return SCR_H + 8; case 80: return 1;
        }
        return 0; });
    // ---- Rechtecke
    reg(U, "SetRect", 5, [](Cpu& c) { wr_rect(A(0), {(int)A(1), (int)A(2), (int)A(3), (int)A(4)}); return 1u; });
    reg(U, "SetRectEmpty", 1, [](Cpu& c) { wr_rect(A(0), {}); return 1u; });
    reg(U, "CopyRect", 2, [](Cpu& c) { memcpy(G(A(0)), G(A(1)), 16); return 1u; });
    reg(U, "InflateRect", 3, [](Cpu& c) { Rect r = rd_rect(A(0)); r.l -= (int)A(1); r.r += (int)A(1); r.t -= (int)A(2); r.b += (int)A(2); wr_rect(A(0), r); return 1u; });
    reg(U, "OffsetRect", 3, [](Cpu& c) { wr_rect(A(0), rOff(rd_rect(A(0)), (int)A(1), (int)A(2))); return 1u; });
    reg(U, "IntersectRect", 3, [](Cpu& c) { Rect r = rI(rd_rect(A(1)), rd_rect(A(2))); wr_rect(A(0), r); return r.empty() ? 0u : 1u; });
    reg(U, "UnionRect", 3, [](Cpu& c) { Rect r = rU(rd_rect(A(1)), rd_rect(A(2))); wr_rect(A(0), r); return r.empty() ? 0u : 1u; });
    reg(U, "PtInRect", 3, [](Cpu& c) { return rd_rect(A(0)).has((int)A(1), (int)A(2)) ? 1u : 0u; });
    reg(U, "IsRectEmpty", 1, [](Cpu& c) { return rd_rect(A(0)).empty() ? 1u : 0u; });
    reg(U, "EqualRect", 2, [](Cpu& c) { return memcmp(G(A(0)), G(A(1)), 16) == 0 ? 1u : 0u; });
    // ---- Zeichenketten
    reg(U, "wsprintfA", 2, [](Cpu& c) { std::string s = wsprintf_impl(GS(A(1)), c, 2); strcpy(GS(A(0)), s.c_str()); return (uint32_t)s.size(); }, true);
    reg(U, "wvsprintfA", 3, [](Cpu& c) {
        // va_list: Zeiger auf Argumente im Gaststapel -> eigener Leser
        std::string out; const char* f = GS(A(1)); uint32_t ap = A(2); char buf[256];
        while (*f) { if (*f != '%') { out += *f++; continue; } f++; if (*f == '%') { out += '%'; f++; continue; }
            std::string spec = "%"; while (strchr("-#0 +", *f) && *f) spec += *f++; while (*f >= '0' && *f <= '9') spec += *f++;
            if (*f == '.') { spec += *f++; while (*f >= '0' && *f <= '9') spec += *f++; } if (*f == 'l' || *f == 'h') f++;
            char t = *f++; uint32_t v = rd32(ap); ap += 4;
            if (t == 's') snprintf(buf, sizeof buf, (spec + "s").c_str(), v ? GS(v) : "(null)");
            else if (t == 'c') { buf[0] = (char)v; buf[1] = 0; }
            else snprintf(buf, sizeof buf, (spec + (t == 'i' ? 'd' : t)).c_str(), v);
            out += buf; }
        strcpy(GS(A(0)), out.c_str()); return (uint32_t)out.size(); });
    reg(U, "CharUpperA", 1, [](Cpu& c) { uint32_t p = A(0); if (p < 0x10000) return (uint32_t)cp_upper(p & 0xFF); for (char* s = GS(p); *s; s++) *s = cp_upper(*s); return p; });
    reg(U, "CharLowerA", 1, [](Cpu& c) { uint32_t p = A(0); if (p < 0x10000) return (uint32_t)cp_lower(p & 0xFF); for (char* s = GS(p); *s; s++) *s = cp_lower(*s); return p; });
    reg(U, "CharNextA", 1, [](Cpu& c) { return *GS(A(0)) ? A(0) + 1 : A(0); });
    reg(U, "CharPrevA", 2, [](Cpu& c) { return A(1) > A(0) ? A(1) - 1 : A(0); });
    reg(U, "IsCharAlphaA", 1, [](Cpu& c) { uint8_t ch = A(0); return (isalpha(ch) || ch >= 0xC0) ? 1u : 0u; });
    // ---- Nachrichten
    reg(U, "SendMessageA", 4, [](Cpu& c) { return send_msg(A(0), A(1), A(2), A(3)); });
    reg(U, "PostMessageA", 4, [](Cpu& c) { post_msg(A(0), A(1), A(2), A(3)); return 1u; });
    reg(U, "PostQuitMessage", 1, [](Cpu& c) { post_msg(0, 0x12, A(0), 0); return 0u; });
    reg(U, "PeekMessageA", 5, [](Cpu& c) { return peek(A(0), A(1), A(2), A(3), A(4)) ? 1u : 0u; });
    reg(U, "GetMessageA", 4, [](Cpu& c) { while (!peek(A(0), A(1), A(2), A(3), 1)) plat_sleep_ms(4); return rd32(A(0) + 4) == 0x12 ? 0u : 1u; });
    reg(U, "TranslateMessage", 1, [](Cpu& c) -> uint32_t {
        uint32_t m = rd32(A(0) + 4), vk = rd32(A(0) + 8), h = rd32(A(0));
        if (m != WM_KEYDOWN && m != WM_SYSKEYDOWN) return 0;
        bool sh = (g_keystate[0x10] & 0x80) != 0, ctl = (g_keystate[0x11] & 0x80) != 0, caps = (g_keystate[0x14] & 1) != 0;
        int ch = 0;
        if (vk >= 'A' && vk <= 'Z') { ch = (sh ^ caps) ? vk : vk + 32; if (ctl) ch = vk - 'A' + 1; }
        else if (vk >= '0' && vk <= '9') { static const char s[] = ")!@#$%^&*("; ch = sh ? s[vk - '0'] : vk; }
        else if (vk >= 0x60 && vk <= 0x69) ch = '0' + vk - 0x60;
        else if (vk == 0x20) ch = ' '; else if (vk == 0x0D) ch = '\r'; else if (vk == 0x08) ch = 8; else if (vk == 0x1B) ch = 27; else if (vk == 0x09) ch = 9;
        else if (vk == 0xBD) ch = sh ? '_' : '-'; else if (vk == 0xBB) ch = sh ? '+' : '='; else if (vk == 0xBE) ch = sh ? '>' : '.'; else if (vk == 0xBC) ch = sh ? '<' : ',';
        else if (vk == 0x6A) ch = '*'; else if (vk == 0x6B) ch = '+'; else if (vk == 0x6D) ch = '-'; else if (vk == 0x6E) ch = '.'; else if (vk == 0x6F) ch = '/';
        if (!ch) return 0;
        g_q.push_front({h, m == WM_SYSKEYDOWN ? (uint32_t)WM_SYSCHAR : (uint32_t)WM_CHAR, (uint32_t)ch, rd32(A(0) + 12), now_ms(), g_mouse_x, g_mouse_y});
        return 1; });
    reg(U, "DispatchMessageA", 1, [](Cpu& c) -> uint32_t {
        uint32_t p = A(0); uint32_t h = rd32(p), m = rd32(p + 4), wp = rd32(p + 8), lp = rd32(p + 12);
        if (m == WM_TIMER && lp) return gcall(lp, { h, m, wp, now_ms() });
        if (!h) return 0;
        Wnd* w = W(h); if (!w) return 0;
        Rect before = w->upd;
        uint32_t r = send_msg(h, m, wp, lp);
        if (m == WM_PAINT) { static int spin = 0; Wnd* w2 = W(h);
            if (w2 && !w2->upd.empty() && w2->upd.l == before.l && w2->upd.r == before.r && w2->upd.t == before.t && w2->upd.b == before.b) { if (++spin > 2) { w2->upd = Rect{}; spin = 0; } } else spin = 0; }
        return r; });
    reg(U, "DefWindowProcA", 4, [](Cpu& c) { return def_window_proc(A(0), A(1), A(2), A(3)); });
    reg(U, "CallWindowProcA", 5, [](Cpu& c) { return call_proc(A(0), A(1), A(2), A(3), A(4)); });
    reg(U, "SetTimer", 4, [](Cpu& c) {
        uint32_t h = A(0), id = A(1); rlog("SetTimer(%08x, %u, %u ms, %08x)", h, id, A(2), A(3));
        if (!h) { static uint32_t nid = 0x100; id = nid++; }
        for (auto& t : g_timers) if (t.hwnd == h && t.id == id) { t.ms = A(2); t.proc = A(3); t.next = now_ms() + A(2); return id; }
        g_timers.push_back({h, id, A(2), A(3), now_ms() + A(2)});
        return id; });
    reg(U, "KillTimer", 2, [](Cpu& c) {
        auto it = std::remove_if(g_timers.begin(), g_timers.end(), [&](const Timer& t) { return t.hwnd == A(0) && t.id == A(1); });
        bool f = it != g_timers.end(); g_timers.erase(it, g_timers.end()); return f ? 1u : 0u; });
    reg(U, "SetWindowsHookExA", 4, [](Cpu& c) { static uint32_t hn = 0x00800010; Hook h{(int)A(0), A(1), hn}; hn += 4; g_hooks.push_back(h); rlog("SetWindowsHookEx(%d)", (int)A(0)); return h.h; });
    reg(U, "UnhookWindowsHookEx", 1, [](Cpu& c) { g_hooks.erase(std::remove_if(g_hooks.begin(), g_hooks.end(), [&](const Hook& h) { return h.h == A(0); }), g_hooks.end()); return 1u; });
    reg(U, "CallNextHookEx", 4, [](Cpu&) { return 0u; });
    reg(U, "MessageBoxA", 4, [](Cpu& c) { return message_box(A(0), A(1) ? GS(A(1)) : "", A(2) ? GS(A(2)) : "", A(3)); });
    // ---- Zeichnen
    reg(U, "GetDC", 1, [](Cpu& c) { return dc_create_window(A(0) == 0xFFF0 ? 0 : A(0), true, false, nullptr); });
    reg(U, "GetWindowDC", 1, [](Cpu& c) { return dc_create_window(A(0), false, false, nullptr); });
    reg(U, "ReleaseDC", 2, [](Cpu& c) { dc_release(A(1)); return 1u; });
    reg(U, "BeginPaint", 2, [](Cpu& c) -> uint32_t {
        Wnd* w = W(A(0)); uint32_t ps = A(1); memset(G(ps), 0, 64);
        if (!w) return 0;
        if (w->nc_dirty) draw_nc(w);
        Rect u = w->upd; bool er = w->erase; w->upd = Rect{}; w->erase = false;
        if (u.empty()) u = Rect{0, 0, 0, 0};
        uint32_t hdc = dc_create_window(A(0), true, true, &u);
        wr32(ps, hdc); wr_rect(ps + 8, u);
        if (er) { uint32_t r = send_msg(A(0), WM_ERASEBKGND, hdc, 0); wr32(ps + 4, r ? 0 : 1); }
        return hdc; });
    reg(U, "EndPaint", 2, [](Cpu& c) { dc_release(rd32(A(1))); return 1u; });
    reg(U, "InvalidateRect", 3, [](Cpu& c) {
        if (!A(0)) { for (auto& kv : g_wnds) if (!kv.second->destroyed) { invalidate(kv.second, nullptr, A(2)); kv.second->nc_dirty = true; } return 1u; }
        Wnd* w = W(A(0)); if (!w) return 0u;
        if (A(1)) { Rect r = rd_rect(A(1)); invalidate(w, &r, A(2)); } else invalidate(w, nullptr, A(2));
        return 1u; });
    reg(U, "InvalidateRgn", 3, [](Cpu& c) { Wnd* w = W(A(0)); if (w) invalidate(w, nullptr, A(2)); return 1u; });
    reg(U, "ValidateRect", 2, [](Cpu& c) {
        Wnd* w = W(A(0)); if (!w) return 0u;
        if (!A(1)) { w->upd = Rect{}; w->erase = false; return 1u; }
        Rect r = rd_rect(A(1)); if (r.l <= w->upd.l && r.t <= w->upd.t && r.r >= w->upd.r && r.b >= w->upd.b) { w->upd = Rect{}; w->erase = false; }
        return 1u; });
    reg(U, "GetUpdateRect", 3, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) return 0u; if (A(1)) wr_rect(A(1), w->upd); return w->upd.empty() ? 0u : 1u; });
    reg(U, "FillRect", 3, [](Cpu& c) {
        Surf s; if (!dc_surf(A(0), s)) return 0u;
        uint32_t br = A(2), cr = 0;
        if (br <= 31) cr = sysclr(br - 1); else if (GObj* o = gdi_get(br)) { if (o->style == 1) return 1u; cr = resolve_color(dc_get(A(0)), o->color); }
        surf_fill(s, rd_rect(A(1)), cr); return 1u; });
    reg(U, "FrameRect", 3, [](Cpu& c) {
        Surf s; if (!dc_surf(A(0), s)) return 0u;
        uint32_t br = A(2), cr = 0;
        if (br <= 31) cr = sysclr(br - 1); else if (GObj* o = gdi_get(br)) cr = resolve_color(dc_get(A(0)), o->color);
        surf_frame(s, rd_rect(A(1)), cr); return 1u; });
    reg(U, "DrawTextA", 5, [](Cpu& c) {
        Rect r = rd_rect(A(3)); uint32_t h = draw_text_impl(A(0), GS(A(1)), (int)A(2), r, A(4));
        if (A(4) & 0x400) wr_rect(A(3), r); return h; });
    reg(U, "GetSysColor", 1, [](Cpu& c) { return sysclr(A(0)); });
    reg(U, "GetSysColorBrush", 1, [](Cpu& c) { return A(0) + 1; });
    reg(U, "LoadBitmapA", 2, [](Cpu& c) -> uint32_t {
        uint32_t base = 0; uint32_t r = res_find_any(A(0), A(1), 2, &base);
        if (!r) { rlog("LoadBitmap(%s) fehlt", is_intres(A(1)) ? "#" : GS(A(1))); return 0; }
        uint32_t d = res_data(base, r, nullptr);
        return bitmap_from_dib(d, 0, 0); });
    reg(U, "LoadStringA", 4, [](Cpu& c) -> uint32_t {
        uint32_t id = A(1), base = 0; uint32_t r = res_find_any(A(0), (id >> 4) + 1, 6, &base);
        if (!r || !A(3)) return 0; uint32_t p = res_data(base, r, nullptr);
        for (uint32_t i = 0; i < (id & 15); i++) p += 2 + 2 * rd16(p);
        uint32_t n = rd16(p); uint32_t k = std::min(n, A(3) - 1);
        extern uint8_t u_to_cp1252(uint16_t);
        for (uint32_t i = 0; i < k; i++) wr8(A(2) + i, u_to_cp1252(rd16(p + 2 + 2 * i)));
        wr8(A(2) + k, 0); return k; });
    // ---- Scrollbalken
    auto sbi = [](Wnd* w, uint32_t bar) -> ScrollInfo* { if (!w) return nullptr; return &w->sb[bar == 2 ? 2 : bar == 1 ? 1 : 0]; };
    auto sb_redraw = [](Wnd* w, uint32_t bar) {
        if (bar == 2) invalidate(w, nullptr, true); else w->nc_dirty = true; };
    reg(U, "SetScrollRange", 5, [sbi, sb_redraw](Cpu& c) {
        Wnd* w = W(A(0)); ScrollInfo* s = sbi(w, A(1)); if (!s) return 0u;
        s->mn = (int)A(2); s->mx = (int)A(3); if (s->pos < s->mn) s->pos = s->mn; if (s->pos > s->mx) s->pos = s->mx;
        if (A(1) < 2) { uint32_t bit = A(1) == 1 ? WS_VSCROLL : WS_HSCROLL; uint32_t old = w->style;
            if (s->mn == s->mx) w->style &= ~bit; else w->style |= bit;
            if (old != w->style) { static int depth = 0; w->nc_dirty = true; invalidate(w, nullptr, true); if (depth < 2) { depth++; send_size_move(w); depth--; } } }
        sb_redraw(w, A(1)); return 1u; });
    reg(U, "GetScrollRange", 4, [sbi](Cpu& c) { ScrollInfo* s = sbi(W(A(0)), A(1)); if (!s) return 0u; wr32(A(2), s->mn); wr32(A(3), s->mx); return 1u; });
    reg(U, "SetScrollPos", 4, [sbi, sb_redraw](Cpu& c) {
        Wnd* w = W(A(0)); ScrollInfo* s = sbi(w, A(1)); if (!s) return 0u;
        int o = s->pos; int p = (int)A(2); if (p < s->mn) p = s->mn; if (p > s->mx) p = s->mx; s->pos = p;
        if (o != p) sb_redraw(w, A(1)); return (uint32_t)o; });
    reg(U, "GetScrollPos", 2, [sbi](Cpu& c) { ScrollInfo* s = sbi(W(A(0)), A(1)); return s ? (uint32_t)s->pos : 0u; });
    reg(U, "ShowScrollBar", 3, [](Cpu& c) {
        Wnd* w = W(A(0)); if (!w) return 0u; uint32_t bar = A(1), old = w->style;
        if (bar == 2) { if (A(2)) show_window(w, 5); else show_window(w, 0); return 1u; }
        uint32_t bits = bar == 0 ? WS_HSCROLL : bar == 1 ? WS_VSCROLL : (WS_HSCROLL | WS_VSCROLL);
        if (A(2)) w->style |= bits; else w->style &= ~bits;
        if (old != w->style) { w->nc_dirty = true; invalidate(w, nullptr, true); send_size_move(w); }
        return 1u; });
    reg(U, "EnableScrollBar", 3, [](Cpu&) { return 1u; });
    // ---- Menues
    reg(U, "CreateMenu", 0, [](Cpu&) { return menu_new(false); });
    reg(U, "CreatePopupMenu", 0, [](Cpu&) { return menu_new(true); });
    reg(U, "DestroyMenu", 1, [](Cpu& c) { g_menus.erase(A(0)); return 1u; });
    reg(U, "LoadMenuA", 2, [](Cpu& c) -> uint32_t {
        uint32_t base = 0; uint32_t r = res_find_any(A(0), A(1), 4, &base);
        if (!r) { rlog("LoadMenu fehlt"); return 0; }
        uint32_t p = res_data(base, r, nullptr) + 4;
        uint32_t hm = menu_new(false); parse_menu(p, hm);
        rlog("LoadMenu -> %08x (%zu Eintraege)", hm, g_menus[hm].items.size());
        return hm; });
    reg(U, "GetMenu", 1, [](Cpu& c) { Wnd* w = W(A(0)); return w ? w->menu : 0u; });
    reg(U, "SetMenu", 2, [](Cpu& c) { Wnd* w = W(A(0)); if (!w) return 0u; w->menu = A(1); w->nc_dirty = true; invalidate(w, nullptr, true); send_size_move(w); return 1u; });
    reg(U, "DrawMenuBar", 1, [](Cpu& c) { Wnd* w = W(A(0)); if (w) w->nc_dirty = true; return 1u; });
    reg(U, "GetSubMenu", 2, [](Cpu& c) { Menu* m = MN(A(0)); if (!m || A(1) >= m->items.size()) return 0u; return m->items[A(1)].sub; });
    reg(U, "GetMenuItemCount", 1, [](Cpu& c) { Menu* m = MN(A(0)); return m ? (uint32_t)m->items.size() : 0xFFFFFFFFu; });
    reg(U, "GetMenuItemID", 2, [](Cpu& c) { Menu* m = MN(A(0)); if (!m || A(1) >= m->items.size()) return 0xFFFFFFFFu; auto& it = m->items[A(1)]; return (it.flags & 0x10) ? 0xFFFFFFFFu : it.id; });
    reg(U, "GetMenuState", 3, [](Cpu& c) { MenuItem* it = menu_find(A(0), A(1), A(2)); return it ? (it->flags & 0xFFEF) : 0xFFFFFFFFu; });
    reg(U, "GetMenuStringA", 5, [](Cpu& c) { MenuItem* it = menu_find(A(0), A(1), A(4)); if (!it) return 0u; if (A(2) && A(3)) { strncpy(GS(A(2)), it->text.c_str(), A(3)); GS(A(2))[A(3) - 1] = 0; } return (uint32_t)it->text.size(); });
    auto mk_item = [](uint32_t flags, uint32_t id, uint32_t str) { MenuItem it; it.flags = flags & ~0x400u;
        if (flags & 0x10) it.sub = id; else it.id = id;
        if (!(flags & (0x800 | 0x4 | 0x100)) && str) it.text = GS(str);
        return it; };
    reg(U, "AppendMenuA", 4, [mk_item](Cpu& c) { Menu* m = MN(A(0)); if (!m) return 0u; m->items.push_back(mk_item(A(1), A(2), A(3))); menu_dirty_all(); return 1u; });
    reg(U, "InsertMenuA", 5, [mk_item](Cpu& c) {
        Menu* m = MN(A(0)); if (!m) return 0u; uint32_t pos = A(1), fl = A(2);
        MenuItem it = mk_item(fl, A(3), A(4));
        if (fl & 0x400) { if (pos >= m->items.size()) m->items.push_back(it); else m->items.insert(m->items.begin() + pos, it); }
        else { uint32_t owner = 0; int idx = -1; if (menu_find(A(0), pos, 0, &owner, &idx)) { Menu* om = MN(owner); om->items.insert(om->items.begin() + idx, it); } else m->items.push_back(it); }
        menu_dirty_all(); return 1u; });
    reg(U, "ModifyMenuA", 5, [mk_item](Cpu& c) {
        uint32_t owner = 0; int idx = -1; MenuItem* it = menu_find(A(0), A(1), A(2), &owner, &idx); if (!it) return 0u;
        *it = mk_item(A(2), A(3), A(4)); menu_dirty_all(); return 1u; });
    auto rm = [](Cpu& c) { uint32_t owner = 0; int idx = -1; if (!menu_find(A(0), A(1), A(2), &owner, &idx)) return 0u; Menu* m = MN(owner); m->items.erase(m->items.begin() + idx); menu_dirty_all(); return 1u; };
    reg(U, "RemoveMenu", 3, rm);
    reg(U, "DeleteMenu", 3, rm);
    reg(U, "EnableMenuItem", 3, [](Cpu& c) { MenuItem* it = menu_find(A(0), A(1), A(2)); if (!it) return 0xFFFFFFFFu; uint32_t o = it->flags & 3; it->flags = (it->flags & ~3u) | (A(2) & 3); menu_dirty_all(); return o; });
    reg(U, "CheckMenuItem", 3, [](Cpu& c) { MenuItem* it = menu_find(A(0), A(1), A(2)); if (!it) return 0xFFFFFFFFu; uint32_t o = it->flags & 8; it->flags = (it->flags & ~8u) | (A(2) & 8); return o; });
    reg(U, "TrackPopupMenu", 7, [](Cpu& c) {
        int id = menu_track(A(0), (int)A(2), (int)A(3), A(5), false, 0);
        if (A(1) & 0x100) return (uint32_t)id;
        if (id) post_msg(A(5), WM_COMMAND, id, 0);
        return 1u; });
    g_defproc_va = 0;
}
// nach dem Commit aufzurufen (Trap-Adressen erst dann verfuegbar)
void user32_late_init() {
    g_ctl_va = g_br->shim_trap("USER32.dll", "__CtlProc");
    g_defproc_va = g_br->shim_trap("USER32.dll", "DefWindowProcA");
}

// ============================================================ Scrollbalken-Klick (modal)
void scroll_click(Wnd* w, int bar, const Rect& r, int x, int y) {
    ScrollInfo& si = w->sb[bar];
    bool vert = (bar == 1) || (bar == 2 && (w->style & 1));
    int len = vert ? r.h() : r.w(), th = vert ? r.w() : r.h();
    int p = vert ? y - r.t : x - r.l;
    uint32_t tgt = bar == 2 ? w->parent : w->h, msg = vert ? WM_VSCROLL : WM_HSCROLL, ctl = bar == 2 ? w->h : 0;
    int range = si.mx - si.mn, track = len - 3 * th;
    int thumb = (range > 0 && track > 0) ? (int)((int64_t)(si.pos - si.mn) * track / range) + th : -1000;
    int code;
    if (p < th) code = 0; else if (p >= len - th) code = 1; else if (p < thumb) code = 2; else if (p >= thumb + th) code = 3; else code = 5;
    if (code == 5) {
        int grab = p - thumb, pos = si.pos;
        for (;;) {
            plat_poll(); InputEv e; bool up = false;
            while (plat_next_event(e)) { if (e.type == EV_LUP) up = true;
                if (e.type == EV_MOUSEMOVE || e.type == EV_LUP) { int q = (vert ? e.y - r.t : e.x - r.l) - grab - th;
                    int np = si.mn + (track > 0 ? (int)((int64_t)q * range / track) : 0); if (np < si.mn) np = si.mn; if (np > si.mx) np = si.mx;
                    if (np != pos) { pos = np; send_msg(tgt, msg, 5 | (pos << 16), ctl); } } }
            if (up) break;
            for (uint32_t h : std::vector<uint32_t>(g_top)) if (Wnd* t = W(h)) nc_pass(t);
            plat_present(); plat_sleep_ms(10);
        }
        send_msg(tgt, msg, 4 | (pos << 16), ctl);
        send_msg(tgt, msg, 8, ctl);
        return;
    }
    send_msg(tgt, msg, code, ctl);
    uint32_t t0 = now_ms(), last = t0;
    for (;;) {
        plat_poll(); InputEv e; bool up = false;
        while (plat_next_event(e)) if (e.type == EV_LUP) up = true;
        if (up || !g_lbutton) break;
        uint32_t t = now_ms();
        if (t - t0 > 350 && t - last > 60) { last = t; send_msg(tgt, msg, code, ctl); }
        for (uint32_t h : std::vector<uint32_t>(g_top)) if (Wnd* tw = W(h)) nc_pass(tw);
        plat_present(); plat_sleep_ms(10);
    }
    send_msg(tgt, msg, 8, ctl);
}
