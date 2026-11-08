// civ2vita — native Standard-Steuerelemente: EDIT, BUTTON, LISTBOX, COMBOBOX, SCROLLBAR, STATIC
#include "win.h"

enum { K_EDIT = 1, K_BUTTON, K_LIST, K_COMBO, K_SCROLL, K_STATIC };
bool ctl_class(const std::string& name, int& kind) {
    std::string n = name; for (auto& ch : n) ch = toupper(ch);
    kind = n == "EDIT" ? K_EDIT : n == "BUTTON" ? K_BUTTON : n == "LISTBOX" ? K_LIST : n == "COMBOBOX" ? K_COMBO : n == "SCROLLBAR" ? K_SCROLL : n == "STATIC" ? K_STATIC : 0;
    return kind != 0;
}
static void notify(Wnd* w, uint32_t code) { send_msg(w->parent, 0x111, (w->id & 0xFFFF) | (code << 16), w->h); }
static int vis_rows(Wnd* w) { Rect c = client_rect_local(w); return std::max(1, c.h() / std::max(1, w->itemh)); }
static void list_sync_sb(Wnd* w) {
    int n = w->items.size(), v = vis_rows(w);
    w->sb[1].mn = 0; w->sb[1].mx = std::max(0, n - v); w->sb[1].pos = w->top;
    if (w->top > w->sb[1].mx) w->top = w->sb[1].pos = w->sb[1].mx;
    if (w->top < 0) w->top = w->sb[1].pos = 0;
    w->nc_dirty = true;
}
void ctl_init_class(Wnd* w) {
    if (w->ctl == K_COMBO) {   // angezeigte Hoehe = Eingabezeile
        w->itemh = 16; w->limit = w->wr.h();  // Gesamtlaenge fuer die Liste merken
        w->wr.b = w->wr.t + 22;
    }
    if (w->ctl == K_LIST) w->itemh = 16;
    if (w->ctl == K_SCROLL) { w->sb[2].mn = 0; w->sb[2].mx = 100; w->sb[2].pos = 0; }
}
static GFont& wfont(Wnd* w) { GObj* o = gdi_get(w->font ? w->font : ui_font()); static GFont d; if (o && o->type == GO_FONT) return o->font; return d; }

// Zeichnen ueber einen Client-DC des Steuerelements
struct CDC { uint32_t hdc; Surf s; GDC* dc; };
static bool begin_draw(Wnd* w, CDC& d) {
    Rect u = client_rect_local(w);
    d.hdc = dc_create_window(w->h, true, true, &u);
    d.dc = dc_get(d.hdc); d.dc->font = w->font ? w->font : ui_font();
    w->upd = Rect{}; w->erase = false;
    return dc_surf(d.hdc, d.s);
}
static void end_draw(CDC& d) { dc_release(d.hdc); }
static void txt(CDC& d, Wnd* w, Rect r, const std::string& s, uint32_t col, uint32_t fmt) {
    d.dc->text = col; d.dc->bkmode = 1;
    Rect rr = r; draw_text_impl(d.hdc, s.c_str(), s.size(), rr, fmt);
    (void)w;
}
static void raised(Surf& s, Rect r, bool down) {
    uint32_t a = down ? 0x808080 : 0xFFFFFF, b = down ? 0xFFFFFF : 0x404040;
    surf_fill(s, {r.l, r.t, r.r, r.t + 1}, a); surf_fill(s, {r.l, r.t, r.l + 1, r.b}, a);
    surf_fill(s, {r.l, r.b - 1, r.r, r.b}, b); surf_fill(s, {r.r - 1, r.t, r.r, r.b}, b);
    if (!down) { surf_fill(s, {r.l + 1, r.b - 2, r.r - 1, r.b - 1}, 0x808080); surf_fill(s, {r.r - 2, r.t + 1, r.r - 1, r.b - 1}, 0x808080); }
}
static void sunken(Surf& s, Rect r) {
    surf_fill(s, {r.l, r.t, r.r, r.t + 1}, 0x808080); surf_fill(s, {r.l, r.t, r.l + 1, r.b}, 0x808080);
    surf_fill(s, {r.l, r.b - 1, r.r, r.b}, 0xFFFFFF); surf_fill(s, {r.r - 1, r.t, r.r, r.b}, 0xFFFFFF);
}
static void owner_draw(Wnd* w, int type, int item, uint32_t state, Rect r, uint32_t data) {
    Rect u = client_rect_local(w);
    uint32_t hdc = dc_create_window(w->h, true, true, &u);
    dc_get(hdc)->font = w->font ? w->font : ui_font();
    uint32_t dis = galloc(48);
    wr32(dis, type); wr32(dis + 4, w->id); wr32(dis + 8, item); wr32(dis + 12, 1); wr32(dis + 16, state);
    wr32(dis + 20, w->h); wr32(dis + 24, hdc); wr_rect(dis + 28, r); wr32(dis + 44, data);
    send_msg(w->parent, 0x2B, w->id, dis);
    gfree(dis); dc_release(hdc);
}

// ------------------------------------------------------------ Paint
static void paint(Wnd* w) {
    if (!wnd_visible(w)) { w->upd = Rect{}; return; }
    CDC d; if (!begin_draw(w, d)) { end_draw(d); return; }
    Surf& s = d.s; Rect c = client_rect_local(w);
    bool dis = (w->style & 0x08000000) != 0;
    switch (w->ctl) {
    case K_EDIT: {
        bool ro = (w->style & 0x800) != 0;
        surf_fill(s, c, (dis || ro) ? 0xC0C0C0 : 0xFFFFFF);
        std::string t = (w->style & 0x20) ? std::string(w->text.size(), '*') : w->text;
        uint32_t fmt = (w->style & 4) ? 0x10 : 0x20 | 4; if (w->style & 1) fmt |= 1; if (w->style & 2) fmt |= 2;
        txt(d, w, {c.l + 2, c.t + 1, c.r - 2, c.b - 1}, t, dis ? 0x808080 : 0, fmt | 0x800);
        if (focus_hwnd() == w->h) { GFont& f = wfont(w); int tw = text_width(f, t.c_str(), t.size()); int x = std::min(c.r - 2, c.l + 2 + tw); int ty = (w->style & 4) ? c.t + 1 : c.t + (c.h() - f.height) / 2;
            surf_fill(s, {x, ty, x + 1, ty + f.height}, 0); }
        break; }
    case K_BUTTON: {
        int bs = w->style & 0xF;
        if (bs == 0xB) { end_draw(d); owner_draw(w, 4, 0, (w->pushed ? 1 : 0) | (focus_hwnd() == w->h ? 0x10 : 0), c, 0); return; }
        if (bs == 7) {  // Gruppenrahmen
            GFont& f = wfont(w); int fy = c.t + f.height / 2;
            surf_frame(s, {c.l, fy, c.r, c.b}, 0x808080); surf_frame(s, {c.l + 1, fy + 1, c.r + 1, c.b + 1}, 0xFFFFFF);
            int tw = text_width(f, w->text.c_str(), w->text.size());
            surf_fill(s, {c.l + 6, c.t, c.l + 10 + tw, c.t + f.height}, 0xC0C0C0);
            txt(d, w, {c.l + 8, c.t, c.r, c.t + f.height}, w->text, 0, 0x20);
            break; }
        if (bs == 2 || bs == 3 || bs == 5 || bs == 6 || bs == 4 || bs == 9) {
            surf_fill(s, c, 0xC0C0C0);
            int cy = (c.t + c.b) / 2; Rect b{c.l + 1, cy - 6, c.l + 14, cy + 7};
            surf_fill(s, b, 0xFFFFFF); sunken(s, b);
            if (w->check) { if (bs == 4 || bs == 9) surf_fill(s, {b.l + 4, b.t + 4, b.r - 4, b.b - 4}, 0);
                else for (int k = 0; k < 3; k++) { surf_fill(s, {b.l + 3 + k, b.t + 5 + k, b.l + 4 + k, b.t + 8 + k}, 0); surf_fill(s, {b.l + 6 + k, b.t + 7 - k, b.l + 7 + k, b.t + 10 - k}, 0); } }
            txt(d, w, {c.l + 18, c.t, c.r, c.b}, w->text, dis ? 0x808080 : 0, 0x20 | 4);
            if (focus_hwnd() == w->h) surf_frame(s, {c.l + 16, c.t + 1, c.r - 1, c.b - 1}, 0x808080);
            break; }
        surf_fill(s, c, 0xC0C0C0);
        if (bs == 1 || focus_hwnd() == w->h) surf_frame(s, c, 0);
        Rect in = (bs == 1 || focus_hwnd() == w->h) ? Rect{c.l + 1, c.t + 1, c.r - 1, c.b - 1} : c;
        raised(s, in, w->pushed);
        Rect tr = w->pushed ? rOff(in, 1, 1) : in;
        txt(d, w, tr, w->text, dis ? 0x808080 : 0, 0x20 | 4 | 1);
        break; }
    case K_LIST: {
        surf_fill(s, c, 0xFFFFFF);
        bool od = (w->style & 0x30) != 0;
        int rows = vis_rows(w) + 1;
        for (int i = 0; i < rows && w->top + i < (int)w->items.size(); i++) {
            int idx = w->top + i; Rect r{c.l, c.t + i * w->itemh, c.r, c.t + (i + 1) * w->itemh};
            bool sel = idx == w->cursel;
            if (od) { owner_draw(w, 2, idx, sel ? 1 : 0, r, w->itemdata[idx]); continue; }
            if (sel) surf_fill(s, r, 0x800000);
            txt(d, w, {r.l + 2, r.t, r.r, r.b}, w->items[idx], sel ? 0xFFFFFF : 0, 0x20 | 4 | 0x800);
        }
        break; }
    case K_COMBO: {
        surf_fill(s, c, 0xFFFFFF); sunken(s, c);
        Rect b{c.r - 18, c.t + 2, c.r - 2, c.b - 2}; surf_fill(s, b, 0xC0C0C0); raised(s, b, false);
        int cx = (b.l + b.r) / 2, cy = (b.t + b.b) / 2; for (int k = 0; k < 4; k++) surf_fill(s, {cx - 3 + k, cy - 1 + k, cx + 4 - k, cy + k}, 0);
        std::string t = (w->cursel >= 0 && w->cursel < (int)w->items.size()) ? w->items[w->cursel] : w->text;
        txt(d, w, {c.l + 4, c.t, b.l - 2, c.b}, t, 0, 0x20 | 4 | 0x800);
        break; }
    case K_SCROLL: { end_draw(d); draw_scrollbar(w, 2, client_rect_screen(w)); return; }
    case K_STATIC: {
        int ss = w->style & 0x1F;
        if (ss >= 4 && ss <= 9) { uint32_t cols[] = { 0, 0x808080, 0xFFFFFF }; int k = (ss - 4) % 3; if (ss <= 6) surf_fill(s, c, cols[k]); else surf_frame(s, c, cols[k]); break; }
        surf_fill(s, c, 0xC0C0C0);
        txt(d, w, c, w->text, 0, (ss == 1 ? 1 : ss == 2 ? 2 : 0) | 0x10);
        break; }
    }
    end_draw(d);
}

// ------------------------------------------------------------ Listen-Hilfen
static int list_add(Wnd* w, uint32_t lp, int at, bool hasStr) {
    std::string t = hasStr && lp ? GS(lp) : "";
    uint32_t data = hasStr ? 0 : lp;
    if (at < 0 || at > (int)w->items.size()) {
        if ((w->style & 2) && hasStr && w->ctl == 3) { at = 0; while (at < (int)w->items.size() && strcasecmp(w->items[at].c_str(), t.c_str()) <= 0) at++; }
        else at = w->items.size();
    }
    w->items.insert(w->items.begin() + at, t); w->itemdata.insert(w->itemdata.begin() + at, data);
    if (w->cursel >= at) w->cursel++;
    if (w->ctl == 3) list_sync_sb(w);
    invalidate(w, nullptr, true);
    return at;
}
static int list_find(Wnd* w, int start, const char* s, bool exact) {
    int n = w->items.size(); size_t L = strlen(s);
    for (int k = 1; k <= n; k++) { int i = (start + k + n) % n; if (start < 0) i = k - 1;
        if (exact ? !strcasecmp(w->items[i].c_str(), s) : !strncasecmp(w->items[i].c_str(), s, L)) return i; }
    return -1;
}
static void list_ensure(Wnd* w) { int v = vis_rows(w); if (w->cursel < w->top) w->top = w->cursel; if (w->cursel >= w->top + v) w->top = w->cursel - v + 1; if (w->top < 0) w->top = 0; list_sync_sb(w); }
static uint32_t list_msgs(Wnd* w, uint32_t m, uint32_t wp, uint32_t lp, bool& handled) {
    handled = true;
    bool hasStr = !(w->style & 0x30) || (w->style & 0x40) || w->ctl == 4;
    // CB_ -> LB_ abbilden
    if (w->ctl == 4) {
        static const std::pair<uint32_t, uint32_t> map[] = { {0x143, 0x180}, {0x144, 0x182}, {0x146, 0x18B}, {0x147, 0x188}, {0x148, 0x189}, {0x149, 0x18A},
            {0x14A, 0x181}, {0x14B, 0x184}, {0x14C, 0x18F}, {0x14D, 0x18C}, {0x14E, 0x186}, {0x150, 0x199}, {0x151, 0x19A}, {0x153, 0x1A0}, {0x154, 0x1A1}, {0x158, 0x1A2} };
        bool f = false; for (auto& p : map) if (p.first == m) { m = p.second; f = true; break; }
        if (!f) { handled = false; return 0; }
    }
    switch (m) {
        case 0x180: return list_add(w, lp, -1, hasStr);
        case 0x181: return list_add(w, lp, (int)wp, hasStr);
        case 0x182: if (wp >= w->items.size()) return (uint32_t)-1; w->items.erase(w->items.begin() + wp); w->itemdata.erase(w->itemdata.begin() + wp);
                    if (w->cursel >= (int)w->items.size()) w->cursel = w->items.size() - 1; if (w->ctl == 3) list_sync_sb(w); invalidate(w, nullptr, true); return w->items.size();
        case 0x184: w->items.clear(); w->itemdata.clear(); w->cursel = -1; w->top = 0; if (w->ctl == 3) list_sync_sb(w); invalidate(w, nullptr, true); return 0;
        case 0x185: if ((int)lp == -1 || lp < w->items.size()) { w->cursel = wp ? (int)lp : -1; invalidate(w, nullptr, true); } return 0;
        case 0x186: if ((int)wp >= (int)w->items.size()) return (uint32_t)-1; w->cursel = (int)wp; if (w->ctl == 3 && w->cursel >= 0) list_ensure(w); invalidate(w, nullptr, true); return w->cursel;
        case 0x187: return (int)wp == w->cursel ? 1 : 0;
        case 0x188: return (uint32_t)w->cursel;
        case 0x189: if (wp >= w->items.size()) return (uint32_t)-1; if (!hasStr) { wr32(lp, w->itemdata[wp]); return 4; } strcpy(GS(lp), w->items[wp].c_str()); return w->items[wp].size();
        case 0x18A: if (wp >= w->items.size()) return (uint32_t)-1; return hasStr ? w->items[wp].size() : 4;
        case 0x18B: return w->items.size();
        case 0x18C: { int i = list_find(w, (int)wp, GS(lp), false); if (i >= 0) { w->cursel = i; if (w->ctl == 3) list_ensure(w); invalidate(w, nullptr, true); } return (uint32_t)i; }
        case 0x18E: return w->top;
        case 0x18F: return (uint32_t)list_find(w, (int)wp, GS(lp), false);
        case 0x1A2: return (uint32_t)list_find(w, (int)wp, GS(lp), true);
        case 0x197: w->top = (int)wp; if (w->ctl == 3) list_sync_sb(w); invalidate(w, nullptr, true); return 0;
        case 0x198: { Rect c = client_rect_local(w); int i = (int)wp - w->top; wr_rect(lp, {c.l, c.t + i * w->itemh, c.r, c.t + (i + 1) * w->itemh}); return 0; }
        case 0x199: if (wp >= w->items.size()) return (uint32_t)-1; return w->itemdata[wp];
        case 0x19A: if (wp >= w->items.size()) return (uint32_t)-1; w->itemdata[wp] = lp; return 0;
        case 0x1A0: w->itemh = std::max(1, (int)(lp & 0xFFFF)); if (w->ctl == 3) list_sync_sb(w); invalidate(w, nullptr, true); return 0;
        case 0x1A1: return w->itemh;
    }
    handled = false; return 0;
}

// ------------------------------------------------------------ Combobox-Ausklappliste (modal)
static void combo_drop(Wnd* w) {
    Rect c = client_rect_screen(w);
    int n = w->items.size(); if (!n) return;
    int ih = 18, rows = std::min(n, std::max(4, (w->limit - 22) / ih)); if (rows > 12) rows = 12;
    Rect r{c.l, c.b, c.r, c.b + rows * ih + 2};
    if (r.b > SCR_H) r = rOff(r, 0, -(r.h() + c.h()));
    ScreenSave sv; screen_save(sv, r);
    int top = std::max(0, std::min(w->cursel, n - rows)), sel = w->cursel;
    auto draw = [&]() { scr_fill(r, 0xFFFFFF); scr_frame3d(r, false);
        for (int i = 0; i < rows && top + i < n; i++) { Rect ir{r.l + 1, r.t + 1 + i * ih, r.r - 1, r.t + 1 + (i + 1) * ih};
            if (top + i == sel) scr_fill(ir, 0x800000); scr_text(ir, w->items[top + i].c_str(), top + i == sel ? 0xFFFFFF : 0, 0); } };
    draw(); plat_present(true);
    int result = -2;
    while (result == -2) {
        plat_poll(); InputEv e;
        while (plat_next_event(e)) {
            if (e.type == EV_MOUSEMOVE && r.has(e.x, e.y)) { int i = top + (e.y - r.t - 1) / ih; if (i != sel && i < n) { sel = i; draw(); } }
            if (e.type == EV_LUP && r.has(e.x, e.y)) { result = sel; }
            if (e.type == EV_LDOWN && !r.has(e.x, e.y)) result = -1;
            if (e.type == EV_KEY && e.down) {
                if (e.key == 0x26 && sel > 0) { sel--; if (sel < top) top = sel; draw(); }
                if (e.key == 0x28 && sel < n - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; draw(); }
                if (e.key == 0x0D) result = sel; if (e.key == 0x1B) result = -1;
            }
        }
        plat_present(); plat_sleep_ms(10);
    }
    screen_restore(sv); plat_present(true);
    if (result >= 0 && result != w->cursel) { w->cursel = result; invalidate(w, nullptr, true); notify(w, 1); }
    notify(w, 8);
}

// ------------------------------------------------------------ Steuerelement-Prozedur
uint32_t ctl_proc(Wnd* w, uint32_t m, uint32_t wp, uint32_t lp) {
    uint32_t h = w->h;
    if (w->ctl == 3 || w->ctl == 4) { bool hd; uint32_t r = list_msgs(w, m, wp, lp, hd); if (hd) return r; }
    switch (m) {
    case 0x01: // WM_CREATE
        if ((w->ctl == 3 && (w->style & 0x30)) || (w->ctl == 4 && (w->style & 0x30))) {
            uint32_t mis = galloc(24); wr32(mis, w->ctl == 3 ? 2 : 3); wr32(mis + 4, w->id); wr32(mis + 16, w->itemh);
            send_msg(w->parent, 0x2C, w->id, mis); int ih = rd32(mis + 16); if (ih > 0 && ih < 200) w->itemh = ih; gfree(mis); }
        if (w->ctl == 3) list_sync_sb(w);
        return 0;
    case 0x0F: paint(w); return 0;
    case 0x14: return 1;
    case 0x0C: w->text = lp ? GS(lp) : ""; if (w->ctl == K_EDIT) { w->selA = w->selB = w->text.size(); } invalidate(w, nullptr, true); return 1;
    case 0x30: w->font = wp; if (lp) invalidate(w, nullptr, true); return 0;
    case 0x31: return w->font;
    case 0x0A: invalidate(w, nullptr, true); return 0;
    case 0x07: invalidate(w, nullptr, true); if (w->ctl == K_EDIT) notify(w, 0x100); if (w->ctl == K_LIST) notify(w, 4); return 0;
    case 0x08: invalidate(w, nullptr, true); if (w->ctl == K_EDIT) notify(w, 0x200); if (w->ctl == K_LIST) notify(w, 5); return 0;
    case 0x87: return w->ctl == K_EDIT ? 0x89 : (w->ctl == K_LIST || w->ctl == K_COMBO) ? 0x85 : 0x81;  // WM_GETDLGCODE
    }
    switch (w->ctl) {
    case K_EDIT:
        switch (m) {
        case 0xB0: if (wp) wr32(wp, w->selA); if (lp) wr32(lp, w->selB); return (w->selA & 0xFFFF) | (w->selB << 16);
        case 0xB1: w->selA = (int)wp < 0 ? w->text.size() : std::min<int>(wp, w->text.size()); w->selB = (int)lp < 0 ? w->text.size() : std::min<int>(lp, w->text.size()); invalidate(w, nullptr, true); return 1;
        case 0xC5: w->limit = wp; return 0;
        case 0xBA: { int n = 1; for (char ch : w->text) if (ch == '\n') n++; return n; }
        case 0xC1: return w->text.size();
        case 0xC2: { std::string ins = lp ? GS(lp) : ""; w->text = w->text.substr(0, std::min<size_t>(w->selA, w->text.size())) + ins + w->text.substr(std::min<size_t>(w->selB, w->text.size())); w->selA = w->selB = w->selA + ins.size(); invalidate(w, nullptr, true); notify(w, 0x300); return 0; }
        case 0xC4: { uint16_t cap = rd16(lp); uint32_t n = std::min<uint32_t>(cap, w->text.size()); memcpy(G(lp), w->text.data(), n); return n; }
        case 0xCF: if (wp) w->style |= 0x800; else w->style &= ~0x800u; invalidate(w, nullptr, true); return 1;
        case 0xB8: return 0; case 0xB9: return 0; case 0xB7: return 1;
        case 0x201: {
            set_focus(h);
            if (w->style & 0x800) return 0;
            char out[512];
            std::string title = TR("Text eingeben", "Enter text"); if (Wnd* p = W(w->parent)) if (!p->text.empty()) title = p->text;
            if (plat_ime(title.c_str(), w->text.c_str(), out, w->limit > 0 && w->limit < 500 ? w->limit : 500)) {
                std::string t = out;
                if (w->style & 0x2000) { std::string d; for (char ch : t) if (isdigit((uint8_t)ch)) d += ch; t = d; }
                w->text = t; w->selA = w->selB = t.size(); invalidate(w, nullptr, true); notify(w, 0x300); notify(w, 0x400);
            }
            return 0; }
        case 0x102: {
            if (w->style & 0x800) return 0;
            char ch = (char)wp;
            if (ch == 8) { if (!w->text.empty()) w->text.pop_back(); }
            else if ((uint8_t)ch >= 32) { if (w->limit && (int)w->text.size() >= w->limit) return 0; if ((w->style & 0x2000) && !isdigit((uint8_t)ch)) return 0; w->text += ch; }
            else return 0;
            w->selA = w->selB = w->text.size(); invalidate(w, nullptr, true); notify(w, 0x300); return 0; }
        }
        break;
    case K_BUTTON: {
        int bs = w->style & 0xF;
        switch (m) {
        case 0xF0: return w->check;
        case 0xF1: w->check = wp; invalidate(w, nullptr, true); return 0;
        case 0xF2: return w->check | (w->pushed ? 4 : 0) | (focus_hwnd() == h ? 8 : 0);
        case 0xF3: w->pushed = wp != 0; invalidate(w, nullptr, true); return 0;
        case 0xF4: w->style = (w->style & ~0xFu) | (wp & 0xF); invalidate(w, nullptr, true); return 0;
        case 0xF5: notify(w, 0); return 0;
        case 0x201: case 0x203: if (bs == 7) return 0; w->pushed = true; g_capture = h; set_focus(h); invalidate(w, nullptr, true); return 0;
        case 0x202: {
            bool was = w->pushed; w->pushed = false; if (g_capture == h) g_capture = 0; invalidate(w, nullptr, true);
            int x = (int16_t)(lp & 0xFFFF), y = (int16_t)(lp >> 16); Rect c = client_rect_local(w);
            if (was && c.has(x, y)) {
                if (bs == 3) w->check = !w->check;
                if (bs == 6) w->check = (w->check + 1) % 3;
                if (bs == 9) { if (Wnd* p = W(w->parent)) for (uint32_t k : p->kids) if (Wnd* s = W(k)) if (s->ctl == K_BUTTON && (s->style & 0xF) == 9 && s != w && s->check) { s->check = 0; invalidate(s, nullptr, true); }
                    w->check = 1; }
                notify(w, 0);
            }
            return 0; }
        case 0x100: if (wp == 0x20 || wp == 0x0D) { if (bs == 3) w->check = !w->check; if (bs == 9) w->check = 1; invalidate(w, nullptr, true); notify(w, 0); } return 0;
        }
        break; }
    case K_LIST:
        switch (m) {
        case 0x201: case 0x203: {
            set_focus(h);
            int y = (int16_t)(lp >> 16); int i = w->top + y / std::max(1, w->itemh);
            if (i >= 0 && i < (int)w->items.size()) {
                bool ch = i != w->cursel; w->cursel = i; invalidate(w, nullptr, true);
                if ((w->style & 1) && (ch || m == 0x201)) notify(w, 1);
                if (m == 0x203 && (w->style & 1)) notify(w, 2);
            }
            return 0; }
        case 0x115: {
            int code = wp & 0xFFFF, v = vis_rows(w), t = w->top;
            if (code == 0) t--; else if (code == 1) t++; else if (code == 2) t -= v; else if (code == 3) t += v; else if (code == 4 || code == 5) t = (int)(wp >> 16);
            w->top = t; list_sync_sb(w); invalidate(w, nullptr, true); return 0; }
        case 0x100: {
            int n = w->items.size(); if (!n) return 0; int s = w->cursel;
            if (wp == 0x26) s = std::max(0, s - 1); else if (wp == 0x28) s = std::min(n - 1, s + 1);
            else if (wp == 0x21) s = std::max(0, s - vis_rows(w)); else if (wp == 0x22) s = std::min(n - 1, s + vis_rows(w));
            else if (wp == 0x24) s = 0; else if (wp == 0x23) s = n - 1; else return 0;
            if (s != w->cursel) { w->cursel = s; list_ensure(w); invalidate(w, nullptr, true); if (w->style & 1) notify(w, 1); }
            return 0; }
        }
        break;
    case K_COMBO:
        switch (m) {
        case 0x201: set_focus(h); combo_drop(w); return 0;
        case 0x14F: if (wp) combo_drop(w); return 1;
        case 0x157: return 0;
        case 0x100: { int n = w->items.size(); if (!n) return 0; int s = w->cursel;
            if (wp == 0x26) s = std::max(0, s - 1); else if (wp == 0x28) s = std::min(n - 1, s + 1); else return 0;
            if (s != w->cursel) { w->cursel = s; invalidate(w, nullptr, true); notify(w, 1); } return 0; }
        }
        break;
    case K_SCROLL:
        switch (m) {
        case 0xE0: { int o = w->sb[2].pos; w->sb[2].pos = std::max(w->sb[2].mn, std::min(w->sb[2].mx, (int)wp)); if (lp) invalidate(w, nullptr, true); return o; }
        case 0xE1: return w->sb[2].pos;
        case 0xE2: case 0xE6: w->sb[2].mn = wp; w->sb[2].mx = lp; invalidate(w, nullptr, true); return 0;
        case 0xE3: if (wp) wr32(wp, w->sb[2].mn); if (lp) wr32(lp, w->sb[2].mx); return 0;
        case 0x201: { Rect cs = client_rect_screen(w); scroll_click(w, 2, cs, cs.l + (int16_t)(lp & 0xFFFF), cs.t + (int16_t)(lp >> 16)); return 0; }
        }
        break;
    case K_STATIC:
        if (m == 0x201 && (w->style & 0x100)) { notify(w, 0); return 0; }
        break;
    }
    return def_window_proc(h, m, wp, lp);
}

// Dreieck/Tastatur-Taste bei Fokus in einem Eingabefeld: Texteingabe oeffnen
bool edit_ime_focused() {
    Wnd* w = W(focus_hwnd()); if (!w || w->ctl != K_EDIT || (w->style & 0x800)) return false;
    post_msg(w->h, 0x201, 1, 0); post_msg(w->h, 0x202, 0, 0);
    return true;
}
