// civ2vita — Kern: Logging, Gast-Heap, Shim-Registrierung, verschachtelte Gast-Aufrufe, Pfade
#include "rt.h"
#include "runtime/guest_thread.h"
#include <cstdarg>
#include <map>
#include <set>
#include <mutex>
#include <algorithm>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

Cpu*    g_cpu = nullptr;
Bridge* g_br  = nullptr;
uintptr_t g_membase = 0;
std::string g_root = "ux0:data/civ2";
std::string g_cwd_win = "C:\\CIV2";
uint32_t g_exe_base = 0x400000;
uint32_t g_lasterr = 0;

// ---------------------------------------------------------------- Logging
static FILE* g_log = nullptr;
static std::mutex g_logmx;
void rlog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logmx);
    if (!g_log) { g_log = fopen((g_root + "/civ2vita.log").c_str(), "w"); }
    va_list ap; va_start(ap, fmt);
    if (g_log) { vfprintf(g_log, fmt, ap); fputc('\n', g_log); fflush(g_log); }
    va_end(ap);
}
void rlog_flush() { std::lock_guard<std::mutex> lk(g_logmx); if (g_log) fflush(g_log); }
void fatal(const char* fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rlog("FATAL: %s", b); rlog_flush();
    extern void fatal_screen(const char*);
    fatal_screen(b);
    sceKernelExitProcess(1);
}

// ---------------------------------------------------------------- Gast-Heap
// Bereich [HEAP_LO, HEAP_HI) in der Arena. Kopf je Block (8 Bytes): Kapazitaet, angeforderte Groesse.
static const uint32_t HEAP_LO = 0x01000000, HEAP_HI = 0x04800000;   // 56 MiB Spielspeicher
static uint32_t g_bump = HEAP_LO;
static std::vector<uint32_t> g_bins[64];            // kleine Bloecke: Klassen je 64 Bytes bis 4 KiB
static std::map<uint32_t,uint32_t> g_freeBig;      // addr(header) -> kapazitaet (inkl. Kopf)
static std::mutex g_heapmx;

static inline uint32_t cls_of(uint32_t n){ return (n + 63) / 64; } // 1..64

void mem_init() {
    if (!g_cpu->map(HEAP_LO, HEAP_HI - HEAP_LO, nullptr, d2rt::P_RW))
        fatal("heap map failed");
}

uint32_t galloc(uint32_t n, bool zero) {
    std::lock_guard<std::mutex> lk(g_heapmx);
    if (n == 0) n = 1;
    uint32_t cap, hdr = 0;
    if (n <= 4096) {
        uint32_t k = cls_of(n); cap = k * 64;
        auto& b = g_bins[k - 1];
        if (!b.empty()) { hdr = b.back(); b.pop_back(); }
    } else {
        cap = (n + 4095) & ~4095u;
        for (auto it = g_freeBig.begin(); it != g_freeBig.end(); ++it) {
            if (it->second - 8 >= cap) {
                uint32_t a = it->first, total = it->second;
                g_freeBig.erase(it);
                uint32_t used = cap + 8;
                if (total - used >= 8192) g_freeBig[a + used] = total - used;
                else cap = total - 8;
                hdr = a; break;
            }
        }
    }
    if (!hdr) {
        uint32_t need = cap + 8;
        if (g_bump + need > HEAP_HI) { rlog("galloc: Heap voll (%u Bytes)", n); return 0; }
        hdr = g_bump; g_bump += (need + 15) & ~15u;
    }
    wr32(hdr, cap); wr32(hdr + 4, n);
    uint32_t va = hdr + 8;
    if (zero) memset(G(va), 0, cap);
    return va;
}
void gfree(uint32_t va) {
    if (va < HEAP_LO + 8 || va >= HEAP_HI) return;
    std::lock_guard<std::mutex> lk(g_heapmx);
    uint32_t hdr = va - 8, cap = rd32(hdr);
    if (cap <= 4096 && (cap % 64) == 0 && cap) g_bins[cap / 64 - 1].push_back(hdr);
    else {
        uint32_t total = cap + 8;
        auto it = g_freeBig.emplace(hdr, total).first;
        // mit Nachbarn verschmelzen
        auto nx = std::next(it);
        if (nx != g_freeBig.end() && it->first + it->second == nx->first) { it->second += nx->second; g_freeBig.erase(nx); }
        if (it != g_freeBig.begin()) { auto pv = std::prev(it);
            if (pv->first + pv->second == it->first) { pv->second += it->second; g_freeBig.erase(it); } }
    }
}
uint32_t gsize(uint32_t va) { return (va >= HEAP_LO + 8 && va < HEAP_HI) ? rd32(va - 4) : 0; }
uint32_t grealloc(uint32_t va, uint32_t n) {
    if (!va) return galloc(n);
    uint32_t cap = rd32(va - 8), old = rd32(va - 4);
    if (n <= cap) { if (n > old) memset((char*)G(va) + old, 0, n - old); wr32(va - 4, n); return va; }
    uint32_t nv = galloc(n);
    if (!nv) return 0;
    memcpy(G(nv), G(va), old);
    gfree(va);
    return nv;
}
uint32_t gstrdup(const char* s) { uint32_t n = strlen(s) + 1, v = galloc(n, false); memcpy(G(v), s, n); return v; }

// ---------------------------------------------------------------- Shims
const char* volatile g_lastshim = "";
volatile uint32_t g_shimcount = 0;
static std::set<std::string> g_known;
static std::string lcs(std::string s){ for (auto& ch : s) ch = tolower(ch); return s; }
void reg(const char* dll, const char* name, uint32_t argc, ShimFn fn, bool cdecl_) {
    d2rt::Shim s; s.argc = argc; s.stdcall_cleanup = !cdecl_;
    s.tag = std::string(dll) + "!" + name;
    const char* t = strdup(s.tag.c_str());
    s.fn = [fn, t](Cpu& c) -> uint32_t { g_lastshim = t; g_shimcount = g_shimcount + 1; return fn(c); };
    g_br->register_shim(dll, name, s);
    g_known.insert(lcs(dll) + "!" + name);
}
void reg_ord(const char* dll, uint32_t ord, uint32_t argc, ShimFn fn) {
    d2rt::Shim s; s.argc = argc; s.stdcall_cleanup = true;
    s.tag = std::string(dll) + "!#" + std::to_string(ord);
    s.fn = std::move(fn);
    g_br->register_shim_ordinal(dll, ord, s);
    g_known.insert(lcs(dll) + "!#" + std::to_string(ord));
}
bool shim_known(const std::string& dll, const std::string& name) { return g_known.count(lcs(dll) + "!" + name) != 0; }

// ---------------------------------------------------------------- verschachtelter Gast-Aufruf
static int g_depth = 0;
uint32_t gcallv(uint32_t proc, const uint32_t* args, int n) {
    if (!proc) return 0;
    Cpu& c = *g_cpu;
    d2rt::X86Context ctx; c.save_context(ctx);
    uint32_t esp = (c.reg(d2rt::R_ESP) - 512) & ~15u;
    for (int i = n - 1; i >= 0; --i) { esp -= 4; wr32(esp, args[i]); }
    esp -= 4; wr32(esp, g_br->sentinel());
    c.set_reg(d2rt::R_ESP, esp);
    if (++g_depth > 200) fatal("gcall: Rekursion zu tief");
    { extern int native_lookup(uint32_t); extern uint32_t native_call_idx(int);   // Rueckruf direkt nativ (ohne Emulator-Sprung)
      int ni = native_lookup(proc);
      if (ni >= 0) { uint32_t r = native_call_idx(ni); --g_depth; c.load_context(ctx); return r; } }
    uint32_t ip = proc;
    for (;;) {
        const char* f = nullptr;
        if (!c.run(ip, &f)) {
            fatal("Gast-Absturz in Rueckruf %08x: %s bei EIP=%08x", proc, f ? f : "?", c.fault_addr());
        }
        ip = c.reg(d2rt::R_EIP);
        if ((ip & ~0xFu) == (g_br->sentinel() & ~0xFu)) break;
    }
    --g_depth;
    uint32_t ret = c.reg(d2rt::R_EAX);
    c.load_context(ctx);
    return ret;
}
uint32_t gcall(uint32_t proc, std::initializer_list<uint32_t> args) {
    return gcallv(proc, args.begin(), (int)args.size());
}

// ---------------------------------------------------------------- Waechter (Haenger-Diagnose)
extern "C" volatile uint32_t civ2_dyn_exe_bytes, civ2_dyn_exe_blocks, civ2_dyn_other_bytes, civ2_dyn_other_blocks, civ2_dyn_hist[512];
static int watchdog_thread(SceSize, void*) {
    uint32_t lastc = 0; int lines = 0;
    for (;;) {
        sceKernelDelayThread(4000000);
        const uint32_t* ip = g_cpu ? g_cpu->ip_ptr() : nullptr;
        uint32_t n = g_shimcount;
        uint32_t esp = g_cpu ? g_cpu->reg(d2rt::R_ESP) : 0, ret = 0;
        if (esp > 0x00010000 && esp < 0x07100000) ret = rd32(esp);
        extern uint32_t g_native_calls, g_native_verified, g_native_mismatch; static uint32_t lastn = 0;
        if (lines < 400) { rlog("[waechter] EIP=%08x Aufrufer=%08x Shims/4s=%u nativ/4s=%u geprueft=%u Abweichungen=%u letzter=%s Tiefe=%d", ip ? *ip : 0, ret, n - lastc, g_native_calls - lastn, g_native_verified, g_native_mismatch, g_lastshim, g_depth); lines++; lastn = g_native_calls;
            if (esp > 0x00010000 && esp < 0x07100000 && lines % 3 == 0) {
                std::string ch; for (int i = 0; i < 600 && ch.size() < 420; i++) { uint32_t a = esp + 4 * i; if (a >= 0x07100000) break; uint32_t v = rd32(a);
                    if (v > 0x401000 && v < 0x61C000) { uint8_t b5 = *(uint8_t*)G(v - 5), b6 = *(uint8_t*)G(v - 6); if (b5 == 0xE8 || b6 == 0xFF) { char b[12]; snprintf(b, 12, "%08x ", v); ch += b; } } }
                rlog("[waechter] Stapel: %s", ch.c_str()); } }
        lastc = n;
        {   // Wie viel Spielcode laeuft noch im Emulator (Dynarec)?
            static uint32_t lb = 0; static int k = 0;
            if (++k % 4 == 0 && civ2_dyn_exe_bytes != lb && lines < 600) { lb = civ2_dyn_exe_bytes; lines++;
                std::string top; uint32_t used[8] = {};
                for (int t = 0; t < 6; t++) { int bi = -1; uint32_t bv = 0;
                    for (int i = 0; i < 512; i++) { bool skip = false; for (int u = 0; u < t; u++) if ((int)used[u] == i) skip = true; if (!skip && civ2_dyn_hist[i] > bv) { bv = civ2_dyn_hist[i]; bi = i; } }
                    if (bi < 0) break; used[t] = bi; char b[32]; snprintf(b, 32, " %06x:%uB", 0x400000 + bi * 8192, bv); top += b; }
                rlog("[dynarec] civ2.exe im Emulator: %u Bytes x86 in %u Bloecken, andere Module: %u Bytes in %u Bloecken; Schwerpunkte:%s",
                     civ2_dyn_exe_bytes, civ2_dyn_exe_blocks, civ2_dyn_other_bytes, civ2_dyn_other_blocks, top.c_str()); }
        }
    }
    return 0;
}
void watchdog_start() {
    SceUID t = sceKernelCreateThread("civ2_watch", watchdog_thread, 0x10000100 + 20, 0x4000, 0, 0x40000 /* Kern 2 */, nullptr);
    if (t >= 0) sceKernelStartThread(t, 0, nullptr);
}

// ---------------------------------------------------------------- Pfade
std::string win2host(const char* p) {
    if (!p) return g_root;
    std::string s(p);
    for (auto& ch : s) if (ch == '\\') ch = '/';
    bool abs = false;
    if (s.size() >= 2 && s[1] == ':') { s = s.substr(2); abs = true; }
    if (!s.empty() && s[0] == '/') abs = true;
    if (!abs) {
        std::string cwd = g_cwd_win; for (auto& ch : cwd) if (ch == '\\') ch = '/';
        if (cwd.size() >= 2 && cwd[1] == ':') cwd = cwd.substr(2);
        s = cwd + "/" + s;
    }
    // "/CIV2" bzw. "/CIV2/..." ist das Spielverzeichnis = g_root
    std::vector<std::string> parts; std::string cur;
    for (char ch : s) { if (ch == '/') { if (!cur.empty()) parts.push_back(cur); cur.clear(); } else cur += ch; }
    if (!cur.empty()) parts.push_back(cur);
    std::vector<std::string> out;
    for (auto& q : parts) { if (q == ".") continue; if (q == "..") { if (!out.empty()) out.pop_back(); continue; } out.push_back(q); }
    size_t i = 0;
    if (!out.empty() && lcs(out[0]) == "civ2") i = 1;
    else if (!out.empty() && (lcs(out[0]) == "windows" || lcs(out[0]) == "win95")) i = out.size() > 1 ? out.size() - 1 : 1; // Windows-Verzeichnis -> Spielordner
    std::string r = g_root;
    for (; i < out.size(); ++i) r += "/" + out[i];
    return r;
}
