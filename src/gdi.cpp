// civ2vita — GDI32: Objekte, DCs, Bitmaps, Paletten, Blits, Linien, Text (stb_truetype)
#include "win.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#include <unordered_map>
#include <cmath>

std::map<uint32_t, GObj> g_obj;
std::map<uint32_t, GDC>  g_dc;
static uint32_t g_gnext = 0x00400010;   // GDI-Handles (unterscheidbar von HWNDs)
uint32_t gdi_new(GObj o) { uint32_t h = g_gnext; g_gnext += 4; g_obj[h] = std::move(o); return h; }
GObj* gdi_get(uint32_t h) { auto it = g_obj.find(h); return it == g_obj.end() ? nullptr : &it->second; }
GDC* dc_get(uint32_t h) { auto it = g_dc.find(h); return it == g_dc.end() ? nullptr : &it->second; }
static uint32_t g_stock[20];
static uint32_t g_default_bmp = 0;   // 1x1 mono fuer Speicher-DCs
extern uint16_t cp1252_to_u(uint8_t ch);

void rsub(std::vector<Rect>& v, const Rect& s) {
    std::vector<Rect> o;
    for (auto& r : v) {
        Rect i = rI(r, s);
        if (i.empty()) { o.push_back(r); continue; }
        if (r.t < i.t) o.push_back({r.l, r.t, r.r, i.t});
        if (i.b < r.b) o.push_back({r.l, i.b, r.r, r.b});
        if (r.l < i.l) o.push_back({r.l, i.t, i.l, i.b});
        if (i.r < r.r) o.push_back({i.r, i.t, r.r, i.b});
    }
    v.swap(o);
}

// ============================================================ Farben
uint32_t sysclr(int i) {
    static const uint32_t t[] = { 0xC0C0C0, 0x808000, 0x800000, 0x808080, 0xC0C0C0, 0xFFFFFF, 0x000000, 0x000000, 0x000000, 0xFFFFFF,
        0xC0C0C0, 0xC0C0C0, 0x808080, 0x800000, 0xFFFFFF, 0xC0C0C0, 0x808080, 0x808080, 0x000000, 0xC0C0C0, 0xFFFFFF, 0x404040, 0xDFDFDF, 0x000000, 0xE1FFFF };
    // Hinweis: COLORREF = 0x00BBGGRR; ACTIVECAPTION (2) = dunkelblau 0x800000
    return (i >= 0 && i < 25) ? t[i] : 0xC0C0C0;
}
static int nearest_idx(const uint32_t* pal, int n, uint32_t cr) {
    static std::unordered_map<uint64_t, int> cache;
    if (cache.size() > 8192) cache.clear();
    uint64_t key = ((uint64_t)(uintptr_t)pal << 24) ^ ((uint64_t)n << 56) ^ cr;
    uint32_t h = 0; for (int i = 0; i < n; i++) h = h * 31 + pal[i];
    key ^= (uint64_t)h << 20;
    auto it = cache.find(key); if (it != cache.end()) return it->second;
    int r = cr & 255, g = (cr >> 8) & 255, b = (cr >> 16) & 255, best = 0, bd = 1 << 30;
    for (int i = 0; i < n; i++) { int dr = r - (int)(pal[i] & 255), dg = g - (int)((pal[i] >> 8) & 255), db = b - (int)((pal[i] >> 16) & 255);
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2; if (d < bd) { bd = d; best = i; if (!d) break; } }
    cache[key] = best; return best;
}
uint32_t resolve_color(GDC* dc, uint32_t cr) {
    if ((cr & 0xFFFF0000u) == 0x10FF0000u) {   // DIBINDEX
        if (dc && dc->mem) { GObj* o = gdi_get(dc->bmp); if (o && o->bmp && (int)(cr & 0xFF) < o->bmp->npal) return o->bmp->pal[cr & 0xFF]; }
        return 0;
    }
    if ((cr >> 24) == 1) {   // PALETTEINDEX
        GObj* p = dc ? gdi_get(dc->pal) : nullptr; int i = cr & 0xFFFF;
        if (p && i < p->npal) return p->pal[i];
        return 0;
    }
    return cr & 0xFFFFFF;
}

// ============================================================ Pixel-Zugriff
static inline uint32_t px_get(const Surf& s, int x, int y) {   // -> COLORREF
    const uint8_t* row = s.base + (intptr_t)y * s.pitch;
    switch (s.fmt) {
        case PF_SCR: return ((const uint32_t*)row)[x] & 0xFFFFFF;
        case PF_32: { uint32_t v = ((const uint32_t*)row)[x]; return ((v >> 16) & 255) | (v & 0xFF00) | ((v & 255) << 16); }
        case PF_24: { const uint8_t* p = row + 3 * x; return p[2] | (p[1] << 8) | (p[0] << 16); }
        case PF_16: { uint16_t v = ((const uint16_t*)row)[x]; int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return (r << 3 | r >> 2) | ((g << 3 | g >> 2) << 8) | ((b << 3 | b >> 2) << 16); }
        case PF_8: { uint8_t i = row[x]; return i < s.npal ? s.pal[i] : 0; }
        case PF_1: return row[x] ? 0xFFFFFF : 0;
    }
    return 0;
}
static inline uint32_t px_raw(const Surf& s, int x, int y) {
    const uint8_t* row = s.base + (intptr_t)y * s.pitch;
    switch (s.fmt) { case PF_8: case PF_1: return row[x]; case PF_16: return ((const uint16_t*)row)[x];
        case PF_24: { const uint8_t* p = row + 3 * x; return p[0] | (p[1] << 8) | (p[2] << 16); }
        default: return ((const uint32_t*)row)[x] & 0xFFFFFF; }
}
static inline void px_put_raw(Surf& s, int x, int y, uint32_t v) {
    uint8_t* row = s.base + (intptr_t)y * s.pitch;
    switch (s.fmt) { case PF_8: row[x] = v; break; case PF_1: row[x] = v & 1; break; case PF_16: ((uint16_t*)row)[x] = v; break;
        case PF_24: { uint8_t* p = row + 3 * x; p[0] = v; p[1] = v >> 8; p[2] = v >> 16; break; }
        case PF_SCR: ((uint32_t*)row)[x] = v | 0xFF000000u; break;
        default: ((uint32_t*)row)[x] = v; }
}
static inline uint32_t to_raw(const Surf& s, uint32_t cr) {   // COLORREF -> Rohwert der Flaeche
    switch (s.fmt) {
        case PF_SCR: return cr & 0xFFFFFF;
        case PF_32: return ((cr & 255) << 16) | (cr & 0xFF00) | ((cr >> 16) & 255);
        case PF_24: return ((cr & 255) << 16) | (cr & 0xFF00) | ((cr >> 16) & 255);
        case PF_16: return (((cr & 255) >> 3) << 10) | ((((cr >> 8) & 255) >> 3) << 5) | (((cr >> 16) & 255) >> 3);
        case PF_8: return nearest_idx(s.pal, s.npal, cr);
        case PF_1: return (cr & 0xFFFFFF) == 0xFFFFFF ? 1 : ((cr & 0xFFFFFF) ? 1 : 0);
    }
    return 0;
}
static inline uint32_t raw_to_cr(const Surf& s, uint32_t v) {
    switch (s.fmt) {
        case PF_SCR: return v & 0xFFFFFF;
        case PF_32: case PF_24: return ((v >> 16) & 255) | (v & 0xFF00) | ((v & 255) << 16);
        case PF_16: { int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return (r << 3) | ((g << 3) << 8) | ((b << 3) << 16); }
        case PF_8: return (int)v < s.npal ? s.pal[v] : 0;
        case PF_1: return v ? 0xFFFFFF : 0;
    }
    return 0;
}

// ============================================================ DCs / Flaechen
extern uint32_t* g_screen;
uint32_t dc_create_window(uint32_t hwnd, bool client, bool paint, const Rect* pclip) {
    GDC d; d.hwnd = hwnd; d.client = client; d.paint = paint; d.screen = (hwnd == 0);
    d.pen = g_stock[7]; d.brush = g_stock[0]; d.font = g_stock[13]; d.pal = g_stock[15];
    if (pclip) { d.paintclip = *pclip; d.haspaintclip = true; }
    uint32_t h = gdi_new(GObj{GO_DC});
    g_dc[h] = d; return h;
}
void dc_release(uint32_t h) { g_dc.erase(h); g_obj.erase(h); }

static void bm_surf(GBitmap* b, Surf& s) {
    s.bm = b; s.w = b->w; s.h = b->h; s.fmt = b->fmt; s.pal = b->pal; s.npal = b->npal;
    uint8_t* bits = b->bitsva ? (uint8_t*)G(b->bitsva) : b->bits;
    if (b->topdown) { s.base = bits; s.pitch = b->pitch; }
    else { s.base = bits + (intptr_t)(b->h - 1) * b->pitch; s.pitch = -b->pitch; }
    s.clip.clear(); s.clip.push_back({0, 0, b->w, b->h});
}
static void refresh_palidx(GBitmap* b, GDC* dc) {
    if (!b->pal_is_idx || !dc) return;
    GObj* p = gdi_get(dc->pal); if (!p) return;
    for (int i = 0; i < b->npal; i++) b->pal[i] = b->palidx[i] < p->npal ? p->pal[b->palidx[i]] : 0;
}
bool dc_surf(uint32_t hdc, Surf& s) {
    GDC* d = dc_get(hdc); if (!d) return false;
    s = Surf();
    if (d->mem) {
        GObj* o = gdi_get(d->bmp); if (!o || !o->bmp) return false;
        refresh_palidx(o->bmp.get(), d);
        bm_surf(o->bmp.get(), s);
        return true;
    }
    s.base = (uint8_t*)g_screen; s.pitch = SCR_W * 4; s.w = SCR_W; s.h = SCR_H; s.fmt = PF_SCR;
    if (d->screen) { s.clip.push_back({0, 0, SCR_W, SCR_H}); return true; }
    Wnd* w = W(d->hwnd); if (!w || !wnd_visible(w)) return true;   // keine Clip-Rechtecke -> nichts sichtbar
    Rect org = d->client ? client_rect_screen(w) : w->wr;
    s.ox = org.l; s.oy = org.t;
    vis_region(w, d->client, s.clip);
    if (d->haspaintclip) { Rect pc = rOff(d->paintclip, s.ox, s.oy); for (auto& r : s.clip) r = rI(r, pc); }
    s.clip.erase(std::remove_if(s.clip.begin(), s.clip.end(), [](const Rect& r) { return r.empty(); }), s.clip.end());
    g_screen_dirty = true;
    return true;
}

// ============================================================ Primitive
void surf_fill(Surf& s, Rect r, uint32_t cr) {
    r = rOff(r, s.ox, s.oy);
    uint32_t v = to_raw(s, cr);
    for (auto& c : s.clip) { Rect i = rI(r, c);
        for (int y = i.t; y < i.b; y++) {
            if (s.fmt == PF_SCR || s.fmt == PF_32) { uint32_t* p = (uint32_t*)(s.base + (intptr_t)y * s.pitch) + i.l; uint32_t vv = s.fmt == PF_SCR ? (v | 0xFF000000u) : v; for (int x = i.l; x < i.r; x++) *p++ = vv; }
            else if (s.fmt == PF_8) memset(s.base + (intptr_t)y * s.pitch + i.l, v, i.w());
            else for (int x = i.l; x < i.r; x++) px_put_raw(s, x, y, v);
        } }
}
void surf_pixel(Surf& s, int x, int y, uint32_t cr) {
    x += s.ox; y += s.oy;
    for (auto& c : s.clip) if (c.has(x, y)) { px_put_raw(s, x, y, to_raw(s, cr)); return; }
}
void surf_frame(Surf& s, Rect r, uint32_t cr) {
    if (r.empty()) return;
    surf_fill(s, {r.l, r.t, r.r, r.t + 1}, cr); surf_fill(s, {r.l, r.b - 1, r.r, r.b}, cr);
    surf_fill(s, {r.l, r.t, r.l + 1, r.b}, cr); surf_fill(s, {r.r - 1, r.t, r.r, r.b}, cr);
}
void surf_line(Surf& s, int x0, int y0, int x1, int y1, uint32_t cr) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    uint32_t v = to_raw(s, cr);
    for (;;) {
        if (x0 == x1 && y0 == y1) break;   // Endpunkt exklusiv (wie GDI)
        int X = x0 + s.ox, Y = y0 + s.oy;
        for (auto& c : s.clip) if (c.has(X, Y)) { px_put_raw(s, X, Y, v); break; }
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ============================================================ Bitmaps
static std::shared_ptr<GBitmap> new_bm(int w, int h, int fmt) {
    auto b = std::make_shared<GBitmap>();
    b->w = w; b->h = h; b->fmt = fmt; b->topdown = true;
    int bpp = fmt == PF_SCR ? 32 : fmt == PF_1 ? 8 : fmt;
    b->pitch = ((w * bpp + 31) / 32) * 4;
    b->store.assign((size_t)b->pitch * (h ? h : 1), 0);
    b->bits = b->store.data();
    if (fmt == PF_1) { b->pal[0] = 0; b->pal[1] = 0xFFFFFF; b->npal = 2; }
    return b;
}
static int fmt_of_bpp(int bpp) { return bpp == 8 ? PF_8 : bpp == 16 ? PF_16 : bpp == 24 ? PF_24 : bpp == 32 ? PF_32 : bpp == 1 ? PF_1 : bpp == 4 ? PF_8 : PF_8; }
static void read_colortable(GBitmap* b, uint32_t bih, uint32_t usage, int bpp, GDC* dc) {
    uint32_t hs = rd32(bih);
    int n = rd32(bih + 32); if (!n && bpp <= 8) n = 1 << bpp; if (n > 256) n = 256;
    if (bpp > 8) { b->npal = 0; return; }
    uint32_t ct = bih + hs;
    b->npal = n;
    if (usage == 1) {   // DIB_PAL_COLORS
        b->pal_is_idx = true;
        for (int i = 0; i < n; i++) b->palidx[i] = rd16(ct + 2 * i);
        refresh_palidx(b, dc);
    } else for (int i = 0; i < n; i++) { uint32_t q = rd32(ct + 4 * i); b->pal[i] = ((q >> 16) & 255) | (q & 0xFF00) | ((q & 255) << 16); }
}
// gepackte DIB (BITMAPINFOHEADER + Farbtabelle + Bits) -> HBITMAP (Kopie im Host-Speicher)
uint32_t bitmap_from_dib(uint32_t bih, uint32_t bits, uint32_t usage) {
    uint32_t hs = rd32(bih);
    int w = (int)rd32(bih + 4), h = (int)rd32(bih + 8), bpp = rd16(bih + 14);
    uint32_t comp = rd32(bih + 16);
    bool td = h < 0; if (td) h = -h;
    int n = rd32(bih + 32); if (!n && bpp <= 8) n = 1 << bpp; if (bpp > 8) n = 0;
    if (!bits) bits = bih + hs + n * (usage == 1 ? 2 : 4) + (comp == 3 ? 12 : 0);
    int srcpitch = ((w * bpp + 31) / 32) * 4;
    int fmt = (bpp == 4 || bpp == 1) ? PF_8 : fmt_of_bpp(bpp);
    auto b = new_bm(w, h, fmt);
    read_colortable(b.get(), bih, usage, bpp, nullptr);
    if (comp == 1 && bpp == 8) {   // RLE8
        const uint8_t* p = (const uint8_t*)G(bits); int x = 0, y = 0;
        auto put = [&](uint8_t v) { if (x < w && y < h) b->store[(size_t)(h - 1 - y) * b->pitch + x] = v; x++; };
        for (int guard = 0; guard < w * h * 4 && y < h; guard++) {
            uint8_t a = *p++, c2 = *p++;
            if (a) { for (int i = 0; i < a; i++) put(c2); }
            else if (c2 == 0) { x = 0; y++; } else if (c2 == 1) break;
            else if (c2 == 2) { x += *p++; y += *p++; }
            else { for (int i = 0; i < c2; i++) put(*p++); if (c2 & 1) p++; }
        }
        b->topdown = true;  // wir haben schon umgedreht
        return gdi_new(GObj{GO_BITMAP, false, 0, 0, 1, b});
    }
    for (int y = 0; y < h; y++) {
        const uint8_t* s = (const uint8_t*)G(bits) + (size_t)y * srcpitch;
        int dy = td ? y : (h - 1 - y);
        uint8_t* d = b->store.data() + (size_t)dy * b->pitch;
        if (bpp == 4) for (int x = 0; x < w; x++) d[x] = (s[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
        else if (bpp == 1) for (int x = 0; x < w; x++) d[x] = (s[x >> 3] >> (7 - (x & 7))) & 1;
        else memcpy(d, s, std::min(srcpitch, b->pitch));
    }
    return gdi_new(GObj{GO_BITMAP, false, 0, 0, 1, b});
}

// ============================================================ Blit
static inline uint32_t rop3(uint8_t rop, uint32_t P, uint32_t S, uint32_t D) {
    uint32_t r = 0;
    for (int i = 0; i < 8; i++) if (rop & (1 << i)) {
        uint32_t t = ((i & 4) ? P : ~P) & ((i & 2) ? S : ~S) & ((i & 1) ? D : ~D);
        r |= t;
    }
    return r;
}
static bool rop_uses_src(uint8_t r) { return ((r >> 2) & 0x33) != (r & 0x33); }
static bool rop_uses_dst(uint8_t r) { return ((r >> 1) & 0x55) != (r & 0x55); }

static void do_blit(uint32_t hdst, int dx, int dy, int dw, int dh, uint32_t hsrc, int sx, int sy, int sw, int sh, uint32_t rop) {
    Surf D; if (!dc_surf(hdst, D)) return;
    GDC* ddc = dc_get(hdst);
    uint8_t r8 = (rop >> 16) & 0xFF;
    bool useS = rop_uses_src(r8), useD = rop_uses_dst(r8);
    Surf S; GDC* sdc = nullptr;
    if (useS) { if (!dc_surf(hsrc, S)) return; sdc = dc_get(hsrc); sx += S.ox; sy += S.oy; }   // Fenster-DC als Quelle: Ursprung beachten
    bool flipx = false, flipy = false;
    if (dw < 0) { dw = -dw; dx -= dw; flipx = !flipx; }
    if (dh < 0) { dh = -dh; dy -= dh; flipy = !flipy; }
    if (sw < 0) { sw = -sw; sx -= sw; flipx = !flipx; }
    if (sh < 0) { sh = -sh; sy -= sh; flipy = !flipy; }
    if (!dw || !dh) return;
    // Muster
    uint32_t pcr = 0; GObj* br = ddc ? gdi_get(ddc->brush) : nullptr;
    if (br && br->type == GO_BRUSH) pcr = resolve_color(ddc, br->color);
    uint32_t P = to_raw(D, pcr);
    Rect dr = rOff(Rect{dx, dy, dx + dw, dy + dh}, D.ox, D.oy);
    bool same = useS && S.fmt == D.fmt && (S.fmt != PF_8 || (S.npal == D.npal && !memcmp(S.pal, D.pal, 4 * S.npal)) || D.fmt == PF_8);
    // Hinweis 8->8: Windows kopiert Indizes zwischen DIBs mit gleicher Farbtabelle; bei abweichender Tabelle mappen wir per Farbe
    if (useS && S.fmt == PF_8 && D.fmt == PF_8 && (S.npal != D.npal || memcmp(S.pal, D.pal, 4 * S.npal))) same = false;
    uint32_t mono_fg = ddc ? resolve_color(ddc, ddc->text) : 0, mono_bg = ddc ? resolve_color(ddc, ddc->bk) : 0xFFFFFF;
    uint32_t src_bk = sdc ? resolve_color(sdc, sdc->bk) : 0xFFFFFF;
    bool stretch = useS && (sw != dw || sh != dh);
    { static std::vector<uint64_t> seen; uint64_t key = ((uint64_t)rop << 24) ^ ((uint64_t)(useS ? S.fmt : 0) << 8) ^ D.fmt ^ ((uint64_t)stretch << 60);
      if (std::find(seen.begin(), seen.end(), key) == seen.end() && seen.size() < 60) { seen.push_back(key);
        rlog("Blit rop=%06x quelle=%d ziel=%d %dx%d%s qbk=%08x txt=%06x bk=%06x pinsel=%06x", rop, useS ? S.fmt : 0, D.fmt, dw, dh, stretch ? " gestreckt" : "", sdc ? sdc->bk : 0,
             ddc ? ddc->text : 0, ddc ? ddc->bk : 0, pcr); } }
    { static int kn = 0; if (((dw == 32 && dh == 20) || (dw <= 64 && dh == 23) || (dw == 277 && dh == 23)) && kn < 0) { kn++;
        rlog("Kreis-Blit rop=%06x %d,%d %dx%d <- %d,%d %dx%d quelle=%d(%dx%d) ziel=%d(%dx%d) hdst=%x hsrc=%x", rop, dx, dy, dw, dh, sx, sy, sw, sh,
             useS ? S.fmt : 0, useS ? S.w : 0, useS ? S.h : 0, D.fmt, D.w, D.h, hdst, hsrc); } }
    // LUT 8bit -> Ziel
    uint32_t lut[256]; bool haslut = false;
    if (useS && !same && (S.fmt == PF_8 || S.fmt == PF_1)) {
        haslut = true;
        int bkidx = -1;
        if (S.fmt == PF_8 && D.fmt == PF_1) {
            uint32_t rb = sdc ? sdc->bk : 0xFFFFFF;
            if ((rb & 0xFFFF0000u) == 0x10FF0000u) bkidx = rb & 0xFF;
            else bkidx = nearest_idx(S.pal, S.npal, src_bk & 0xFFFFFF);
        }
        for (int i = 0; i < 256; i++) {
            uint32_t cr = (S.fmt == PF_1) ? (i ? mono_bg : mono_fg) : (i < S.npal ? S.pal[i] : 0);
            if (D.fmt == PF_1) {
                if (S.fmt == PF_8) {   // Windows vergleicht Farben, nicht Indizes: alle Eintraege mit der Hintergrundfarbe zaehlen
                    bool exact = false; uint32_t rb = sdc ? sdc->bk : 0xFFFFFF;
                    if ((rb & 0xFFFF0000u) != 0x10FF0000u) for (int k = 0; k < S.npal; k++) if ((S.pal[k] & 0xFFFFFF) == (src_bk & 0xFFFFFF)) { exact = true; break; }
                    lut[i] = exact ? ((i < S.npal && (S.pal[i] & 0xFFFFFF) == (src_bk & 0xFFFFFF)) ? 1 : 0) : (i == bkidx ? 1 : 0);
                } else lut[i] = (cr & 0xFFFFFF) == (src_bk & 0xFFFFFF) ? 1 : 0;
            }
            else lut[i] = to_raw(D, cr);
        }
    }
    if (useS && S.w == 32 && S.h == 32 && S.npal == 16) {   // Diagnose: Auswahlkreis-Bitmaps (Ressource 301-306)
        static int dn = 0; if (dn < 0) { dn++;
            char buf[600]; int o = snprintf(buf, sizeof buf, "Knopfbild rop=%06x ziel fmt=%d npal=%d %dx%d von %d,%d nach %d,%d same=%d lut:", rop, D.fmt, D.npal, dw, dh, sx, sy, dx, dy, (int)same);
            for (int k = 0; k < 16 && o < 560; k++) o += snprintf(buf + o, sizeof buf - o, " %06x>%x", S.pal[k] & 0xFFFFFF, haslut ? lut[k] : 0);
            rlog("%s", buf); } }
    for (auto& c : D.clip) {
        Rect i = rI(dr, c); if (i.empty()) continue;
        for (int y = i.t; y < i.b; y++) {
            int ly = y - dr.t; if (flipy) ly = dh - 1 - ly;
            int syy = useS ? (stretch ? sy + (int)((int64_t)ly * sh / dh) : sy + ly) : 0;
            if (useS && (syy < 0 || syy >= S.h)) continue;
            // schneller Pfad: SRCCOPY ohne Streckung
            if (r8 == 0xCC && !stretch && !flipx) {
                int sx0 = sx + (i.l - dr.l), n = i.w();
                int a = 0; if (sx0 < 0) { a = -sx0; } int e = n; if (sx0 + e > S.w) e = S.w - sx0;
                if (e <= a) continue;
                uint8_t* drow = D.base + (intptr_t)y * D.pitch; const uint8_t* srow = S.base + (intptr_t)syy * S.pitch;
                if (same) {
                    int bpp = (D.fmt == PF_SCR || D.fmt == PF_32) ? 4 : D.fmt == PF_24 ? 3 : D.fmt == PF_16 ? 2 : 1;
                    memcpy(drow + (i.l + a) * bpp, srow + (sx0 + a) * bpp, (e - a) * bpp);
                    if (D.fmt == PF_SCR) { uint32_t* q = (uint32_t*)drow + i.l + a; for (int k = a; k < e; k++) *q++ |= 0xFF000000u; }
                    continue;
                }
                if (haslut && D.fmt == PF_SCR) { uint32_t* q = (uint32_t*)drow + i.l + a; const uint8_t* p = srow + sx0 + a;
                    for (int k = a; k < e; k++) *q++ = lut[*p++] | 0xFF000000u; continue; }
                if (haslut && (D.fmt == PF_8 || D.fmt == PF_1)) { uint8_t* q = drow + i.l + a; const uint8_t* p = srow + sx0 + a; for (int k = a; k < e; k++) *q++ = lut[*p++]; continue; }
                for (int k = a; k < e; k++) {
                    uint32_t cr = px_get(S, sx0 + k, syy);
                    px_put_raw(D, i.l + k, y, D.fmt == PF_1 ? ((cr == (src_bk & 0xFFFFFF)) ? 1 : 0) : to_raw(D, cr));
                }
                continue;
            }
            for (int x = i.l; x < i.r; x++) {
                int lx = x - dr.l; if (flipx) lx = dw - 1 - lx;
                uint32_t Sv = 0;
                if (useS) {
                    int sxx = stretch ? sx + (int)((int64_t)lx * sw / dw) : sx + lx;
                    if (sxx < 0 || sxx >= S.w) continue;
                    if (same) Sv = px_raw(S, sxx, syy);
                    else if (haslut) Sv = lut[px_raw(S, sxx, syy) & 255];
                    else { uint32_t cr = px_get(S, sxx, syy); Sv = D.fmt == PF_1 ? ((cr == (src_bk & 0xFFFFFF)) ? 1 : 0) : to_raw(D, cr); }
                }
                uint32_t Dv = useD ? px_raw(D, x, y) : 0;
                uint32_t v;
                if (r8 == 0xCC) v = Sv; else if (r8 == 0xF0) v = P; else if (r8 == 0x00) v = to_raw(D, 0); else if (r8 == 0xFF) v = to_raw(D, 0xFFFFFF);
                else v = rop3(r8, P, Sv, Dv);
                uint32_t mask = D.fmt == PF_8 ? 0xFF : D.fmt == PF_1 ? 1 : D.fmt == PF_16 ? 0x7FFF : 0xFFFFFF;
                px_put_raw(D, x, y, v & mask);
            }
        }
    }
}

// ============================================================ Schriften (stb_truetype)
struct FontFile { std::vector<uint8_t> data; stbtt_fontinfo info; bool ok = false; int winAsc = 0, winDesc = 0, avgw = 0, upem = 2048; };
static FontFile g_ff[10];
static const char* kFontWin[10] = { "times.ttf","timesbd.ttf","timesi.ttf","timesbi.ttf","arial.ttf","arialbd.ttf","ariali.ttf","arialbi.ttf","cour.ttf","courbd.ttf" };
static const char* kFontApp[10] = { "IBMPlexSerif-Regular.ttf","IBMPlexSerif-Bold.ttf","IBMPlexSerif-Italic.ttf","IBMPlexSerif-BoldItalic.ttf",
    "WorkSans-Regular.ttf","WorkSans-Bold.ttf","WorkSans-Italic.ttf","WorkSans-BoldItalic.ttf","IBMPlexMono-Regular.ttf","IBMPlexMono-Bold.ttf" };
extern std::vector<uint8_t> slurp(const std::string& p);
static GFont g_sysfont, g_uifont, g_uibold;
static bool g_fonts_done = false;
void fonts_init() {
    if (g_fonts_done) return; g_fonts_done = true;
    for (int i = 0; i < 10; i++) {
        FontFile& f = g_ff[i];
        f.data = slurp(g_root + "/fonts/" + kFontWin[i]);
        bool win = !f.data.empty();
        if (!win) f.data = slurp(std::string("app0:fonts/") + kFontApp[i]);
        if (f.data.empty()) { rlog("Schrift fehlt: %s", kFontApp[i]); continue; }
        if (!stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0))) continue;
        f.ok = true;
        uint8_t* d = f.data.data();
        uint32_t os2 = stbtt__find_table((uint8_t*)d, f.info.fontstart, "OS/2");
        uint32_t head = stbtt__find_table((uint8_t*)d, f.info.fontstart, "head");
        if (head) f.upem = ttUSHORT(d + head + 18);
        if (os2) { f.avgw = ttSHORT(d + os2 + 2); f.winAsc = ttUSHORT(d + os2 + 74); f.winDesc = ttUSHORT(d + os2 + 76); }
        if (!f.winAsc) { int a, de, lg; stbtt_GetFontVMetrics(&f.info, &a, &de, &lg); f.winAsc = a; f.winDesc = -de; }
        rlog("Schrift %d: %s (%s)", i, win ? kFontWin[i] : kFontApp[i], win ? "Windows" : "Ersatz");
    }
    g_sysfont.face = "Arial"; g_sysfont.h = -12; g_sysfont.weight = 700; font_setup(g_sysfont);
    g_uifont.face = "Arial"; g_uifont.h = -12; g_uifont.weight = 400; font_setup(g_uifont);
    g_uibold = g_uifont; g_uibold.weight = 700; font_setup(g_uibold);
}
void font_setup(GFont& f) {
    std::string fc = f.face; for (auto& ch : fc) ch = tolower(ch);
    int fam = 4;
    if (fc.find("times") != std::string::npos || fc.find("roman") != std::string::npos || fc.find("serif") != std::string::npos && fc.find("sans") == std::string::npos) fam = 0;
    if (fc.find("courier") != std::string::npos || fc.find("fixed") != std::string::npos || fc.find("mono") != std::string::npos) fam = 8;
    bool bold = f.weight >= 600;
    int idx = fam == 8 ? (8 + (bold ? 1 : 0)) : fam + (bold ? 1 : 0) + (f.italic ? 2 : 0);
    if (!g_ff[idx].ok) idx = fam == 8 ? 8 : fam;
    if (!g_ff[idx].ok) idx = 4;
    if (!g_ff[idx].ok) { for (int i = 0; i < 10; i++) if (g_ff[i].ok) { idx = i; break; } }
    f.fidx = idx;
    FontFile& ff = g_ff[idx];
    int cell = ff.winAsc + ff.winDesc; if (cell <= 0) cell = ff.upem;
    int h = f.h;
    if (h == 0) h = 16;
    float scale;
    if (h < 0) scale = (float)(-h) / ff.upem;    // Zeichenhoehe = em
    else scale = (float)h / cell;                 // Zellhoehe
    f.scale = scale;
    f.asc = (int)lroundf(ff.winAsc * scale); f.desc = (int)lroundf(ff.winDesc * scale);
    f.height = f.asc + f.desc;
    f.ileading = h < 0 ? std::max(0, f.height + h) : std::max(0, (int)lroundf((cell - ff.upem) * scale));
    f.avgw = ff.avgw ? (int)lroundf(ff.avgw * scale) : (int)(f.height * 0.45f);
    int adv, lsb; if (ff.ok) { stbtt_GetCodepointHMetrics(&ff.info, 'W', &adv, &lsb); f.maxw = (int)lroundf(adv * scale) + 1; }
}
struct Glyph { int w = 0, h = 0, xo = 0, yo = 0, adv = 0; std::vector<uint8_t> px; };
static std::unordered_map<uint64_t, Glyph> g_glyphs;
static Glyph& glyph(GFont& f, uint16_t cp) {
    uint64_t key = ((uint64_t)f.fidx << 56) ^ ((uint64_t)(uint32_t)(f.scale * 65536.0f) << 20) ^ cp;
    auto it = g_glyphs.find(key); if (it != g_glyphs.end()) return it->second;
    if (g_glyphs.size() > 6000) g_glyphs.clear();
    Glyph g; FontFile& ff = g_ff[f.fidx];
    if (ff.ok) {
        int adv, lsb; stbtt_GetCodepointHMetrics(&ff.info, cp, &adv, &lsb);
        g.adv = (int)lroundf(adv * f.scale);
        int x0, y0, x1, y1; stbtt_GetCodepointBitmapBox(&ff.info, cp, f.scale, f.scale, &x0, &y0, &x1, &y1);
        g.w = x1 - x0; g.h = y1 - y0; g.xo = x0; g.yo = y0;
        if (g.w > 0 && g.h > 0) { g.px.resize(g.w * g.h); stbtt_MakeCodepointBitmap(&ff.info, g.px.data(), g.w, g.h, g.w, f.scale, f.scale, cp); }
    }
    return g_glyphs[key] = std::move(g);
}
int text_width(GFont& f, const char* s, int n) {
    int w = 0; for (int i = 0; i < n; i++) w += glyph(f, cp1252_to_u((uint8_t)s[i])).adv; return w;
}
int text_draw(Surf& s, GFont& f, int x, int y, const char* str, int n, uint32_t cr, bool, int ulpos) {
    int pen = x;
    uint32_t raw = to_raw(s, cr);
    int cr_r = cr & 255, cr_g = (cr >> 8) & 255, cr_b = (cr >> 16) & 255;
    bool blend = (s.fmt == PF_SCR || s.fmt == PF_32 || s.fmt == PF_24 || s.fmt == PF_16);
    for (int i = 0; i < n; i++) {
        Glyph& g = glyph(f, cp1252_to_u((uint8_t)str[i]));
        int gx = pen + g.xo + s.ox, gy = y + f.asc + g.yo + s.oy;
        Rect gr{gx, gy, gx + g.w, gy + g.h};
        for (auto& c : s.clip) { Rect ii = rI(gr, c); if (ii.empty()) continue;
            for (int yy = ii.t; yy < ii.b; yy++) for (int xx = ii.l; xx < ii.r; xx++) {
                uint8_t a = g.px[(yy - gy) * g.w + (xx - gx)]; if (!a) continue;
                if (!blend) { if (a >= 110) px_put_raw(s, xx, yy, raw); continue; }
                if (a >= 250) { px_put_raw(s, xx, yy, raw); continue; }
                uint32_t d = px_get(s, xx, yy);
                int r = ((d & 255) * (255 - a) + cr_r * a) / 255, gg = (((d >> 8) & 255) * (255 - a) + cr_g * a) / 255, b = (((d >> 16) & 255) * (255 - a) + cr_b * a) / 255;
                px_put_raw(s, xx, yy, to_raw(s, r | (gg << 8) | (b << 16)));
            } }
        if (i == ulpos || f.underline) {
            int uy = y + f.asc + 1;
            Surf t = s; Rect ur{pen, uy, pen + g.adv, uy + 1}; surf_fill(t, ur, cr);
        }
        pen += g.adv;
    }
    return pen - x;
}
GFont& sys_font() { return g_sysfont; }
GFont& dc_font(GDC* dc) {
    GObj* o = dc ? gdi_get(dc->font) : nullptr;
    if (o && o->type == GO_FONT) return o->font;
    return g_sysfont;
}

// DrawText
uint32_t draw_text_impl(uint32_t hdc, const char* s, int n, Rect& r, uint32_t fmt) {
    GDC* dc = dc_get(hdc); if (!dc) return 0;
    GFont& f = dc_font(dc);
    if (n < 0) n = strlen(s);
    std::string str(s, n);
    if (fmt & 0x40) { std::string o; for (char ch : str) { if (ch == '\t') o += "        "; else o += ch; } str = o; }
    // Zeilen bilden
    struct Line { std::string t; int ul = -1; };
    std::vector<Line> lines;
    auto strip = [&](const std::string& in, Line& L) {
        if (fmt & 0x800) { L.t = in; return; }
        for (size_t i = 0; i < in.size(); i++) {
            if (in[i] == '&') { if (i + 1 < in.size() && in[i + 1] == '&') { L.t += '&'; i++; } else L.ul = L.t.size(); continue; }
            L.t += in[i]; }
    };
    std::vector<std::string> paras;
    if (fmt & 0x20) { std::string t; for (char ch : str) if (ch != '\r' && ch != '\n') t += ch; paras.push_back(t); }
    else { std::string cur; for (size_t i = 0; i < str.size(); i++) { if (str[i] == '\r') continue; if (str[i] == '\n') { paras.push_back(cur); cur.clear(); } else cur += str[i]; } paras.push_back(cur); }
    int maxw = r.w();
    for (auto& p : paras) {
        if (!(fmt & 0x10) || (fmt & 0x20)) { Line L; strip(p, L); lines.push_back(L); continue; }
        // Wortumbruch
        Line L; strip(p, L);
        std::string& t = L.t; size_t start = 0;
        if (t.empty()) { lines.push_back(L); continue; }
        while (start < t.size()) {
            size_t end = start, lastsp = std::string::npos;
            while (end < t.size()) {
                if (t[end] == ' ') lastsp = end;
                if (text_width(f, t.c_str() + start, end - start + 1) > maxw && end > start) break;
                end++;
            }
            if (end < t.size() && lastsp != std::string::npos && lastsp > start) end = lastsp;
            Line part; part.t = t.substr(start, end - start);
            if (L.ul >= (int)start && L.ul < (int)end) part.ul = L.ul - start;
            lines.push_back(part);
            start = end; while (start < t.size() && t[start] == ' ') start++;
        }
    }
    int lh = f.height;
    int toth = lh * (int)lines.size();
    if (fmt & 0x400) {   // DT_CALCRECT
        int w = 0; for (auto& L : lines) w = std::max(w, text_width(f, L.t.c_str(), L.t.size()));
        if (!(fmt & 0x10)) r.r = r.l + w; else if (w < maxw && lines.size() <= 1) r.r = r.l + w;
        r.b = r.t + toth; return toth;
    }
    int y = r.t;
    if (fmt & 0x20) { if (fmt & 4) y = r.t + (r.h() - lh) / 2; else if (fmt & 8) y = r.b - lh; }
    Surf S; if (!dc_surf(hdc, S)) return toth;
    if (!(fmt & 0x100)) { Rect cr = rOff(r, S.ox, S.oy); for (auto& c : S.clip) c = rI(c, cr); }
    uint32_t col = resolve_color(dc, dc->text), bk = resolve_color(dc, dc->bk);
    for (auto& L : lines) {
        int w = text_width(f, L.t.c_str(), L.t.size());
        int x = r.l; if (fmt & 1) x = r.l + (r.w() - w) / 2; else if (fmt & 2) x = r.r - w;
        if (dc->bkmode == 2) surf_fill(S, {x, y, x + w, y + lh}, bk);
        text_draw(S, f, x, y, L.t.c_str(), L.t.size(), col, false, L.ul);
        y += lh;
    }
    return toth;
}

// ============================================================ Bildschirm-Hilfen fuer modale UI
void screen_save(ScreenSave& s, Rect r) {
    r = rI(r, {0, 0, SCR_W, SCR_H}); s.r = r; s.px.resize((size_t)r.w() * r.h());
    for (int y = r.t; y < r.b; y++) memcpy(&s.px[(size_t)(y - r.t) * r.w()], g_screen + y * SCR_W + r.l, r.w() * 4);
}
void screen_restore(const ScreenSave& s) {
    for (int y = s.r.t; y < s.r.b; y++) memcpy(g_screen + y * SCR_W + s.r.l, &s.px[(size_t)(y - s.r.t) * s.r.w()], s.r.w() * 4);
    g_screen_dirty = true;
}
static Surf scr_surf() { Surf s; s.base = (uint8_t*)g_screen; s.pitch = SCR_W * 4; s.w = SCR_W; s.h = SCR_H; s.fmt = PF_SCR; s.clip.push_back({0, 0, SCR_W, SCR_H}); return s; }
void scr_fill(Rect r, uint32_t cr) { Surf s = scr_surf(); surf_fill(s, r, cr); g_screen_dirty = true; }
void scr_frame3d(Rect r, bool raised) {
    Surf s = scr_surf();
    uint32_t a = raised ? 0xFFFFFF : 0x808080, b = raised ? 0x404040 : 0xFFFFFF;
    surf_fill(s, {r.l, r.t, r.r, r.t + 1}, a); surf_fill(s, {r.l, r.t, r.l + 1, r.b}, a);
    surf_fill(s, {r.l, r.b - 1, r.r, r.b}, b); surf_fill(s, {r.r - 1, r.t, r.r, r.b}, b);
    g_screen_dirty = true;
}
int scr_text(Rect r, const char* str, uint32_t cr, int align, bool bold) {
    Surf s = scr_surf(); for (auto& c : s.clip) c = rI(c, r);
    GFont& f = bold ? g_uibold : g_uifont;
    int n = strlen(str); int w = text_width(f, str, n);
    int x = align == 1 ? r.l + (r.w() - w) / 2 : align == 2 ? r.r - w - 2 : r.l + 2;
    int y = r.t + (r.h() - f.height) / 2;
    text_draw(s, f, x, y, str, n, cr, false, -1);
    g_screen_dirty = true; return w;
}
static uint32_t g_uifont_h = 0;
uint32_t ui_font() { return g_uifont_h; }

// ============================================================ Install
void install_gdi32() {
    fonts_init();
    // Lagerobjekte
    uint32_t bcol[5] = { 0xFFFFFF, 0xC0C0C0, 0x808080, 0x404040, 0 };
    for (int i = 0; i < 5; i++) { GObj o; o.type = GO_BRUSH; o.stock = true; o.color = bcol[i]; g_stock[i] = gdi_new(o); }
    { GObj o; o.type = GO_BRUSH; o.stock = true; o.style = 1; g_stock[5] = gdi_new(o); }   // NULL_BRUSH
    { GObj o; o.type = GO_PEN; o.stock = true; o.color = 0xFFFFFF; g_stock[6] = gdi_new(o); }
    { GObj o; o.type = GO_PEN; o.stock = true; o.color = 0; g_stock[7] = gdi_new(o); }
    { GObj o; o.type = GO_PEN; o.stock = true; o.style = 5; g_stock[8] = gdi_new(o); }
    auto mkfont = [](const char* face, int h, int wt) { GObj o; o.type = GO_FONT; o.stock = true; o.font.face = face; o.font.h = h; o.font.weight = wt; font_setup(o.font); return gdi_new(o); };
    g_stock[10] = mkfont("Courier New", -13, 400); g_stock[11] = g_stock[10];
    g_stock[12] = mkfont("Arial", -13, 400);
    g_stock[13] = mkfont("Arial", -12, 700); g_stock[14] = g_stock[13]; g_stock[16] = g_stock[10];
    g_stock[17] = mkfont("Arial", -11, 400);
    g_uifont_h = g_stock[17];
    { GObj o; o.type = GO_PALETTE; o.stock = true; o.npal = 20;
      static const uint32_t def[20] = { 0x000000,0x000080,0x008000,0x008080,0x800000,0x800080,0x808000,0xC0C0C0,0xC0DCC0,0xF0CAA6,
                                         0xF0FBFF,0xA4A0A0,0x808080,0x0000FF,0x00FF00,0x00FFFF,0xFF0000,0xFF00FF,0xFFFF00,0xFFFFFF };
      memcpy(o.pal, def, sizeof def); g_stock[15] = gdi_new(o); }
    { auto b = new_bm(1, 1, PF_1); g_default_bmp = gdi_new(GObj{GO_BITMAP, true, 0, 0, 1, b}); }

    const char* D = "GDI32.dll";
    reg(D, "GetStockObject", 1, [](Cpu& c) { uint32_t i = A(0); return i < 20 ? g_stock[i] : 0u; });
    reg(D, "CreateCompatibleDC", 1, [](Cpu& c) {
        uint32_t h = dc_create_window(0, true, false, nullptr);
        GDC* d = dc_get(h); d->mem = true; d->screen = false; d->bmp = g_default_bmp;
        if (GDC* src = dc_get(A(0))) d->pal = src->pal;
        return h; });
    reg(D, "DeleteDC", 1, [](Cpu& c) { dc_release(A(0)); return 1u; });
    reg(D, "SelectObject", 2, [](Cpu& c) -> uint32_t {
        GDC* d = dc_get(A(0)); GObj* o = gdi_get(A(1)); if (!d || !o) return 0;
        uint32_t prev = 0;
        switch (o->type) {
            case GO_BITMAP: prev = d->bmp; d->bmp = A(1); break;
            case GO_PEN: prev = d->pen; d->pen = A(1); break;
            case GO_BRUSH: prev = d->brush; d->brush = A(1); break;
            case GO_FONT: prev = d->font; d->font = A(1); break;
            default: return 0;
        }
        return prev; });
    reg(D, "DeleteObject", 1, [](Cpu& c) {
        GObj* o = gdi_get(A(0)); if (!o || o->stock || o->type == GO_DC) return 1u;
        if (o->bmp && o->bmp->bitsva && o->bmp.use_count() == 1) { gfree(o->bmp->bitsva); o->bmp->bitsva = 0; }   // DIB-Section-Speicher zurueckgeben
        g_obj.erase(A(0)); return 1u; });
    reg(D, "GdiFlush", 0, [](Cpu&) { return 1u; });
    reg(D, "SetTextColor", 2, [](Cpu& c) { GDC* d = dc_get(A(0)); if (!d) return 0xFFFFFFFFu; uint32_t p = d->text; d->text = A(1); return p; });
    reg(D, "SetBkColor", 2, [](Cpu& c) { GDC* d = dc_get(A(0)); if (!d) return 0xFFFFFFFFu; uint32_t p = d->bk; d->bk = A(1); return p; });
    reg(D, "SetBkMode", 2, [](Cpu& c) { GDC* d = dc_get(A(0)); if (!d) return 0u; uint32_t p = d->bkmode; d->bkmode = A(1); return p; });
    reg(D, "SetTextAlign", 2, [](Cpu& c) { GDC* d = dc_get(A(0)); if (!d) return 0xFFFFFFFFu; uint32_t p = d->align; d->align = A(1); return p; });
    reg(D, "GetTextAlign", 1, [](Cpu& c) { GDC* d = dc_get(A(0)); return d ? d->align : 0u; });
    reg(D, "CreatePen", 3, [](Cpu& c) { GObj o; o.type = GO_PEN; o.style = A(0); o.width = A(1) ? A(1) : 1; o.color = A(2); return gdi_new(o); });
    reg(D, "CreateSolidBrush", 1, [](Cpu& c) { GObj o; o.type = GO_BRUSH; o.color = A(0); return gdi_new(o); });
    reg(D, "CreateHatchBrush", 2, [](Cpu& c) { GObj o; o.type = GO_BRUSH; o.color = A(1); return gdi_new(o); });
    reg(D, "MoveToEx", 4, [](Cpu& c) { GDC* d = dc_get(A(0)); if (!d) return 0u; if (A(3)) { wr32(A(3), d->curx); wr32(A(3) + 4, d->cury); } d->curx = A(1); d->cury = A(2); return 1u; });
    reg(D, "LineTo", 3, [](Cpu& c) {
        GDC* d = dc_get(A(0)); if (!d) return 0u;
        GObj* p = gdi_get(d->pen);
        if (p && p->style != 5) { Surf s; if (dc_surf(A(0), s)) { uint32_t cr = resolve_color(d, p->color);
            int wdt = p->width;
            if (wdt <= 1) surf_line(s, d->curx, d->cury, (int)A(1), (int)A(2), cr);
            else for (int o = -(wdt / 2); o < wdt - wdt / 2; o++) {
                if (abs((int)A(1) - d->curx) >= abs((int)A(2) - d->cury)) surf_line(s, d->curx, d->cury + o, (int)A(1), (int)A(2) + o, cr);
                else surf_line(s, d->curx + o, d->cury, (int)A(1) + o, (int)A(2), cr); } } }
        d->curx = A(1); d->cury = A(2); return 1u; });
    reg(D, "SetPixel", 4, [](Cpu& c) { GDC* d = dc_get(A(0)); Surf s; if (!d || !dc_surf(A(0), s)) return 0xFFFFFFFFu; uint32_t cr = resolve_color(d, A(3)); surf_pixel(s, A(1), A(2), cr); return cr; });
    reg(D, "GetPixel", 3, [](Cpu& c) {
        Surf s; if (!dc_surf(A(0), s)) return 0xFFFFFFFFu;
        int x = (int)A(1) + s.ox, y = (int)A(2) + s.oy;
        if (x < 0 || y < 0 || x >= s.w || y >= s.h) return 0xFFFFFFFFu;
        return px_get(s, x, y); });
    reg(D, "BitBlt", 9, [](Cpu& c) { do_blit(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(3), A(4), A(8)); return 1u; });
    reg(D, "StretchBlt", 11, [](Cpu& c) { do_blit(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8), A(9), A(10)); return 1u; });
    reg(D, "PatBlt", 6, [](Cpu& c) { do_blit(A(0), A(1), A(2), A(3), A(4), 0, 0, 0, A(3), A(4), A(5)); return 1u; });
    reg(D, "CreateCompatibleBitmap", 3, [](Cpu& c) {
        int fmt = PF_SCR; GDC* d = dc_get(A(0));
        if (d && d->mem) { GObj* o = gdi_get(d->bmp); if (o && o->bmp) fmt = o->bmp->fmt; }
        auto b = new_bm(std::max(1, (int)A(1)), std::max(1, (int)A(2)), fmt);
        if (d && d->mem) { GObj* o = gdi_get(d->bmp); if (o && o->bmp && fmt == PF_8) { memcpy(b->pal, o->bmp->pal, sizeof b->pal); b->npal = o->bmp->npal; } }
        return gdi_new(GObj{GO_BITMAP, false, 0, 0, 1, b}); });
    reg(D, "CreateBitmap", 5, [](Cpu& c) {
        int w = A(0), h = A(1), bpp = A(2) * A(3);
        auto b = new_bm(std::max(1, w), std::max(1, h), bpp == 1 ? PF_1 : PF_SCR);
        if (A(4) && bpp == 1) { int sp = ((w + 15) / 16) * 2; const uint8_t* s = (const uint8_t*)G(A(4));
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) b->store[(size_t)y * b->pitch + x] = (s[y * sp + (x >> 3)] >> (7 - (x & 7))) & 1; }
        return gdi_new(GObj{GO_BITMAP, false, 0, 0, 1, b}); });
    reg(D, "GetBitmapDimensionEx", 2, [](Cpu& c) { if (A(1)) { wr32(A(1), 0); wr32(A(1) + 4, 0); } return 1u; });
    reg(D, "CreateDIBSection", 6, [](Cpu& c) -> uint32_t {
        uint32_t bih = A(1), usage = A(2);
        int w = (int)rd32(bih + 4), h = (int)rd32(bih + 8), bpp = rd16(bih + 14);
        auto b = std::make_shared<GBitmap>();
        b->w = w; b->h = h < 0 ? -h : h; b->topdown = h < 0; b->fmt = fmt_of_bpp(bpp); b->dibsec = true;
        if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) rlog("CreateDIBSection: bpp %d nicht unterstuetzt", bpp);
        b->pitch = ((w * bpp + 31) / 32) * 4;
        b->bitsva = galloc(b->pitch * b->h + 16, true);
        read_colortable(b.get(), bih, usage, bpp, dc_get(A(0)));
        if (A(3)) wr32(A(3), b->bitsva);
        return gdi_new(GObj{GO_BITMAP, false, 0, 0, 1, b}); });
    reg(D, "GetDIBColorTable", 4, [](Cpu& c) -> uint32_t {
        GDC* d = dc_get(A(0)); if (!d) return 0; GObj* o = gdi_get(d->bmp); if (!o || !o->bmp) return 0;
        GBitmap* b = o->bmp.get(); uint32_t st = A(1), n = A(2);
        uint32_t k = 0;
        for (; k < n && st + k < (uint32_t)b->npal; k++) { uint32_t cr = b->pal[st + k]; wr32(A(3) + 4 * k, ((cr & 255) << 16) | (cr & 0xFF00) | ((cr >> 16) & 255)); }
        return k; });
    reg(D, "SetDIBColorTable", 4, [](Cpu& c) -> uint32_t {
        GDC* d = dc_get(A(0)); if (!d) return 0; GObj* o = gdi_get(d->bmp); if (!o || !o->bmp) return 0;
        GBitmap* b = o->bmp.get(); uint32_t st = A(1), n = A(2);
        uint32_t k = 0;
        for (; k < n && st + k < 256; k++) { uint32_t q = rd32(A(3) + 4 * k); b->pal[st + k] = ((q >> 16) & 255) | (q & 0xFF00) | ((q & 255) << 16); }
        if ((int)(st + k) > b->npal) b->npal = st + k;
        b->pal_is_idx = false;
        return k; });
    reg(D, "CreatePalette", 1, [](Cpu& c) {
        GObj o; o.type = GO_PALETTE; uint32_t lp = A(0); o.npal = std::min(256, (int)rd16(lp + 2));
        for (int i = 0; i < o.npal; i++) { uint32_t e = rd32(lp + 4 + 4 * i); o.pal[i] = e & 0xFFFFFF; }
        return gdi_new(o); });
    reg(D, "SelectPalette", 3, [](Cpu& c) { GDC* d = dc_get(A(0)); if (!d) return 0u; uint32_t p = d->pal; if (gdi_get(A(1))) d->pal = A(1); return p; });
    reg(D, "RealizePalette", 1, [](Cpu& c) { GDC* d = dc_get(A(0)); GObj* p = d ? gdi_get(d->pal) : nullptr; return p ? (uint32_t)p->npal : 0u; });
    auto setpal = [](Cpu& c) -> uint32_t {
        GObj* p = gdi_get(A(0)); if (!p || p->type != GO_PALETTE) return 0;
        uint32_t st = A(1), n = A(2), k = 0;
        for (; k < n && st + k < 256; k++) p->pal[st + k] = rd32(A(3) + 4 * k) & 0xFFFFFF;
        if ((int)(st + k) > p->npal) p->npal = st + k;
        return k; };
    reg(D, "SetPaletteEntries", 4, setpal);
    reg(D, "AnimatePalette", 4, setpal);
    reg(D, "GetPaletteEntries", 4, [](Cpu& c) -> uint32_t {
        GObj* p = gdi_get(A(0)); if (!p || p->type != GO_PALETTE) return 0;
        uint32_t st = A(1), n = A(2);
        if (!A(3)) return p->npal;
        uint32_t k = 0; for (; k < n && st + k < (uint32_t)p->npal; k++) wr32(A(3) + 4 * k, p->pal[st + k]);
        return k; });
    reg(D, "GetSystemPaletteEntries", 4, [](Cpu& c) -> uint32_t {
        GObj* p = gdi_get(g_stock[15]);
        uint32_t st = A(1), n = A(2);
        for (uint32_t k = 0; k < n; k++) { uint32_t i = st + k, v = 0;
            if (i < 10) v = p->pal[i]; else if (i >= 246) v = p->pal[i - 236]; else v = 0;
            if (A(3)) wr32(A(3) + 4 * k, v); }
        return n; });
    reg(D, "GetNearestPaletteIndex", 2, [](Cpu& c) { GObj* p = gdi_get(A(0)); if (!p || !p->npal) return 0u; return (uint32_t)nearest_idx(p->pal, p->npal, A(1) & 0xFFFFFF); });
    reg(D, "GetNearestColor", 2, [](Cpu& c) { return A(1) & 0xFFFFFF; });
    reg(D, "GetDeviceCaps", 2, [](Cpu& c) -> uint32_t {
        GDC* d = dc_get(A(0));
        switch (A(1)) {
            case 2: return 1; case 4: return 320; case 6: return 240;
            case 8: if (d && d->mem) { GObj* o = gdi_get(d->bmp); if (o && o->bmp) return o->bmp->w; } return SCR_W;
            case 10: if (d && d->mem) { GObj* o = gdi_get(d->bmp); if (o && o->bmp) return o->bmp->h; } return SCR_H;
            case 12: return 32; case 14: return 1; case 24: return 0xFFFFFFFFu;
            case 38: return 0x1 | 0x8 | 0x80 | 0x200 | 0x800;
            case 88: case 90: return 96;
            case 104: return 0; case 106: return 20; case 108: return 24;
            case 117: return SCR_W; case 118: return SCR_H; case 116: return 60;
        }
        return 0; });
    reg(D, "CreateFontIndirectA", 1, [](Cpu& c) {
        uint32_t lf = A(0); GObj o; o.type = GO_FONT;
        o.font.h = (int)rd32(lf); o.font.weight = rd32(lf + 16); o.font.italic = *(uint8_t*)G(lf + 20); o.font.underline = *(uint8_t*)G(lf + 21);
        o.font.face = GS(lf + 28);
        font_setup(o.font);
        return gdi_new(o); });
    reg(D, "CreateFontA", 14, [](Cpu& c) {
        GObj o; o.type = GO_FONT; o.font.h = (int)A(0); o.font.weight = A(4); o.font.italic = A(5); o.font.underline = A(6);
        o.font.face = A(13) ? GS(A(13)) : "Arial"; font_setup(o.font); return gdi_new(o); });
    reg(D, "GetTextMetricsA", 2, [](Cpu& c) {
        GDC* d = dc_get(A(0)); GFont& f = dc_font(d); uint32_t t = A(1);
        memset(G(t), 0, 56);
        wr32(t, f.height); wr32(t + 4, f.asc); wr32(t + 8, f.desc); wr32(t + 12, f.ileading); wr32(t + 16, 0);
        wr32(t + 20, f.avgw); wr32(t + 24, f.maxw); wr32(t + 28, f.weight); wr32(t + 32, 0); wr32(t + 36, 96); wr32(t + 40, 96);
        wr8(t + 44, 0x20); wr8(t + 45, 0xFF); wr8(t + 46, 0x1F); wr8(t + 47, 0x20); wr8(t + 48, f.italic); wr8(t + 49, f.underline);
        wr8(t + 51, f.fidx >= 8 ? 0x30 : 0x26); wr8(t + 52, 0);
        return 1u; });
    reg(D, "GetTextExtentPointA", 4, [](Cpu& c) {
        GDC* d = dc_get(A(0)); GFont& f = dc_font(d);
        wr32(A(3), text_width(f, GS(A(1)), (int)A(2))); wr32(A(3) + 4, f.height); return 1u; });
    reg(D, "GetTextExtentPoint32A", 4, [](Cpu& c) {
        GDC* d = dc_get(A(0)); GFont& f = dc_font(d);
        wr32(A(3), text_width(f, GS(A(1)), (int)A(2))); wr32(A(3) + 4, f.height); return 1u; });
    reg(D, "TextOutA", 5, [](Cpu& c) {
        GDC* d = dc_get(A(0)); if (!d) return 0u; GFont& f = dc_font(d);
        Surf s; if (!dc_surf(A(0), s)) return 0u;
        int n = (int)A(4); const char* str = GS(A(3)); int w = text_width(f, str, n);
        int x = (int)A(1), y = (int)A(2);
        if ((d->align & 6) == 6) x -= w / 2; else if (d->align & 2) x -= w;
        if (d->align & 8) y -= f.height; else if (d->align & 24) y -= f.asc;
        if (d->bkmode == 2) surf_fill(s, {x, y, x + w, y + f.height}, resolve_color(d, d->bk));
        text_draw(s, f, x, y, str, n, resolve_color(d, d->text), false, -1);
        return 1u; });
    reg(D, "AddFontResourceA", 1, [](Cpu& c) { rlog("AddFontResource(%s)", GS(A(0))); return 1u; });
    reg(D, "RemoveFontResourceA", 1, [](Cpu&) { return 1u; });
    reg(D, "EnumFontFamiliesA", 4, [](Cpu& c) -> uint32_t {
        const char* fam = A(1) ? GS(A(1)) : nullptr;
        static const char* known[] = { "Times New Roman", "Arial", "Courier New", "Courier", "MS Sans Serif", "System", nullptr };
        uint32_t lf = galloc(188), tm = galloc(96); uint32_t ret = 1;
        for (int i = 0; known[i]; i++) {
            if (fam && strcasecmp(fam, known[i])) continue;
            memset(G(lf), 0, 188); memset(G(tm), 0, 96);
            wr32(lf, (uint32_t)-16); wr32(lf + 16, 400); strcpy(GS(lf + 28), known[i]); strcpy(GS(lf + 60), known[i]);
            wr32(tm, 16); wr32(tm + 4, 13); wr32(tm + 8, 3); wr32(tm + 20, 7); wr32(tm + 24, 14); wr32(tm + 28, 400);
            ret = gcall(A(2), { lf, tm, (uint32_t)(i < 4 ? 4 : 0), A(3) });
            if (!ret) break;
        }
        gfree(lf); gfree(tm);
        return ret; });
}
