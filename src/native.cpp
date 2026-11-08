// civ2vita — Einbindung des statisch rekompilierten Codes: Einsprung-Patches, Gast-Aufrufe, Pruefmodus
#include "win.h"
#include "native_rt.h"
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <unordered_map>
#include <setjmp.h>
#include <set>

uint8_t* MB = nullptr;
uint32_t TIB = 0;
volatile int g_vlog = 0;
uint8_t g_nat_on[8192];
extern uint32_t g_tib;

static int g_mode = 3;            // 0=aus 1=nur gepruefte 2=alles ungeprueft 3=voll (pruefbare pruefen, Rest direkt nativ)
static int g_trust = 20;
static std::unordered_map<uint32_t, int> g_idx;       // va -> Tabellenindex
static std::vector<uint32_t> g_okcnt;
static std::vector<uint8_t> g_trusted, g_bad;
static std::vector<uint32_t> g_tramp;
uint32_t g_native_calls = 0, g_native_verified = 0, g_native_mismatch = 0;

struct WLog { uint32_t a, n, oldv, newv; };
static std::vector<WLog> g_wl;
static jmp_buf* g_natjmp = nullptr;
static const size_t WL_MAX = 131072;   // max. 2 MB Schreibprotokoll pro Pruefung
static bool g_wl_over = false;
extern "C" void vlog_write(uint32_t a, uint32_t n) {
    if (g_wl.size() >= WL_MAX) { g_wl_over = true; if (g_natjmp) longjmp(*g_natjmp, 1); return; }
    uint32_t v = 0; memcpy(&v, MB + a, n);
    g_wl.push_back({a, n, v, 0});
}
static uint32_t g_cur_native = 0;
static void append_list(const char* file, uint32_t va);
extern "C" void nat_fault(uint32_t a) {
    if (g_natjmp) longjmp(*g_natjmp, 1);
    if (g_cur_native) append_list("native_bad4.txt", g_cur_native);   // beim naechsten Start gesperrt
    fatal("[nativ] ungueltiger Speicherzugriff %08x in Funktion %08x", a, g_cur_native);
}
extern "C" void DIVZERO(uint32_t a) { rlog("[nativ] Division durch 0 bei %08x", a); }
extern "C" void BADJMP(uint32_t a) { fatal("[nativ] unbekanntes Sprungziel bei %08x", a); }

static void ctx_from_cpu(Ctx& c) { for (int i = 0; i < 8; i++) c.r[i] = g_cpu->reg(i); }
static void ctx_to_cpu(const Ctx& c) { for (int i = 0; i < 8; i++) g_cpu->set_reg(i, c.r[i]); }

// Gastcode (Dynarec) ab target ausfuehren; [esp] enthaelt bereits die Ruecksprungadresse -> durch Sentinel ersetzen
static void run_guest_until_sentinel(uint32_t target) {
    Cpu& cpu = *g_cpu;
    uint32_t ip = target;
    for (;;) {
        const char* f = nullptr;
        if (!cpu.run(ip, &f)) fatal("Gast-Absturz (nativer Aufruf %08x): %s bei EIP=%08x", target, f ? f : "?", cpu.fault_addr());
        ip = cpu.reg(d2rt::R_EIP);
        if ((ip & ~0xFu) == (g_br->sentinel() & ~0xFu)) break;
    }
}
extern "C" void CALL_GUEST(Ctx* c, uint32_t target) {
    uint32_t esp = c->r[4];
    uint32_t ret = RD32(esp);
    uint32_t s = g_br->sentinel(); memcpy(MB + esp, &s, 4);
    ctx_to_cpu(*c);
    uint32_t prevf = g_cur_native; g_cur_native = 0;   // waehrend Gastcode laeuft, ist keine native Funktion "schuld"
    run_guest_until_sentinel(target);
    g_cur_native = prevf;
    ctx_from_cpu(*c);
    (void)ret;
}
extern "C" void CALL_ANY(Ctx* c, uint32_t target) {
    auto it = g_idx.find(target);
    for (int k = 0; k < 3 && it == g_idx.end() && target >= 0x401000u && target < 0x404000u && MB[target] == 0xE9; k++) {   // ILT-Sprung aufloesen
        int32_t rel; memcpy(&rel, MB + target + 1, 4); target = target + 5 + (uint32_t)rel; it = g_idx.find(target);
    }
    if (it != g_idx.end() && g_nat_on[it->second]) {
        const NativeFn& nf = g_native_table[it->second];
        nf.fn(c); c->r[4] += 4 + nf.ret; return;
    }
    CALL_GUEST(c, target);
}

static void append_list(const char* file, uint32_t va) {
    FILE* f = fopen((g_root + "/" + file).c_str(), "a"); if (!f) return; fprintf(f, "%08x\n", va); fclose(f);
}
static std::set<uint32_t> read_list(const char* file) {
    std::set<uint32_t> s; FILE* f = fopen((g_root + "/" + file).c_str(), "r"); if (!f) return s;
    char line[64]; while (fgets(line, sizeof line, f)) s.insert((uint32_t)strtoul(line, nullptr, 16)); fclose(f); return s;
}

// Pruefung: nativ und original ausfuehren, vergleichen
static uint32_t verify_call(int i) {
    const NativeFn& nf = g_native_table[i];
    Ctx c0; ctx_from_cpu(c0);
    uint32_t esp0 = c0.r[4];
    // 1) nativ mit Schreibprotokoll
    Ctx cn = c0;
    if (g_wl.capacity() < WL_MAX) g_wl.reserve(WL_MAX);
    g_wl.clear(); g_vlog = 1; g_wl_over = false;
    jmp_buf jb; jmp_buf* prevjb = g_natjmp; bool faulted = false;
    g_natjmp = &jb;
    if (setjmp(jb) == 0) nf.fn(&cn); else faulted = true;
    g_natjmp = prevjb;
    g_vlog = 0;
    for (auto& w : g_wl) { uint32_t v = 0; memcpy(&v, MB + w.a, w.n); w.newv = v; }
    std::vector<WLog>& wl = g_wl;
    for (auto it = wl.rbegin(); it != wl.rend(); ++it) memcpy(MB + it->a, &it->oldv, it->n);
    // 2) original ueber Trampolin
    uint32_t savedRet = RD32(esp0);
    uint32_t s = g_br->sentinel(); memcpy(MB + esp0, &s, 4);
    ctx_to_cpu(c0);
    run_guest_until_sentinel(g_tramp[i]);
    Ctx co; ctx_from_cpu(co);
    memcpy(MB + esp0, &savedRet, 4);
    // 3) vergleichen
    if (g_wl_over) {   // zu viele Schreibzugriffe zum Pruefen: diese Sitzung im Dynarec lassen, nicht als fehlerhaft markieren
        g_bad[i] = 1; g_nat_on[i] = 0; g_wl_over = false;
        rlog("[nativ] %08x schreibt zu viel zum Pruefen -> bleibt vorerst im Dynarec", nf.va);
        co.r[4] = esp0; ctx_to_cpu(co); return co.r[0];
    }
    bool bad = faulted; char why[160] = "";
    if (faulted) snprintf(why, sizeof why, "ungueltiger Speicherzugriff");
    static const char* rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (int r = 0; r < 8 && !bad; r++) {
        uint32_t nv = cn.r[r] + (r == 4 ? 4 + nf.ret : 0);
        if (nv != co.r[r]) { bad = true; snprintf(why, sizeof why, "%s nativ=%08x original=%08x", rn[r], nv, co.r[r]); }
    }
    if (!bad) for (auto& w : wl) {
        if (w.a < esp0 + 4) continue;   // toter Stapel unterhalb + Ruecksprungadresse
        uint32_t v = 0; memcpy(&v, MB + w.a, w.n);
        if (v != w.newv) { bad = true; snprintf(why, sizeof why, "Speicher %08x nativ=%x original=%x", w.a, w.newv, v); break; }
    }
    g_native_verified++;
    if (bad) {
        g_native_mismatch++;
        g_bad[i] = 1; g_nat_on[i] = 0;
        rlog("[nativ] ABWEICHUNG %08x: %s -> Funktion bleibt im Dynarec", nf.va, why);
        append_list("native_bad4.txt", nf.va);
    } else if (++g_okcnt[i] == (uint32_t)g_trust) {
        g_trusted[i] = 1; append_list("native_ok.txt", nf.va);
    }
    co.r[4] = esp0;
    ctx_to_cpu(co);
    return co.r[0];
}

static uint32_t native_entry(int i) {
    g_native_calls++;
    if (g_bad[i]) {   // gesperrt: Original ueber Trampolin
        Ctx c0; ctx_from_cpu(c0); uint32_t esp0 = c0.r[4];
        uint32_t savedRet = RD32(esp0); uint32_t s = g_br->sentinel(); memcpy(MB + esp0, &s, 4);
        run_guest_until_sentinel(g_tramp[i]);
        Ctx co; ctx_from_cpu(co); memcpy(MB + esp0, &savedRet, 4); co.r[4] = esp0; ctx_to_cpu(co); return co.r[0];
    }
    if ((g_mode == 1 || g_mode == 3) && g_native_table[i].pure && !g_trusted[i]) return verify_call(i);
    Ctx c; ctx_from_cpu(c);
    uint32_t prevf = g_cur_native; g_cur_native = g_native_table[i].va;
    g_native_table[i].fn(&c);
    g_cur_native = prevf;
    ctx_to_cpu(c);
    return c.r[0];
}

int native_lookup(uint32_t target) {
    if (g_mode == 0) return -1;
    auto it = g_idx.find(target);
    for (int k = 0; k < 3 && it == g_idx.end() && target >= 0x401000u && target < 0x404000u && MB[target] == 0xE9; k++) {
        int32_t rel; memcpy(&rel, MB + target + 1, 4); target = target + 5 + (uint32_t)rel; it = g_idx.find(target);
    }
    if (it == g_idx.end() || !g_nat_on[it->second]) return -1;
    return (int)it->second;
}
uint32_t native_call_idx(int i) { return native_entry(i); }

// Absturz im Host-Code (vom Fehlerhandler aufgerufen): schuldige native Funktion sperren
extern "C" void civ2_crash_hook(void) {
    if (!g_cur_native) return;
    char path[128]; snprintf(path, sizeof path, "%s/native_bad4.txt", g_root.c_str());
    int fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { char b[16]; int n = snprintf(b, sizeof b, "%08x\n", (unsigned)g_cur_native); sceIoWrite(fd, b, n); sceIoClose(fd); }
}

static uint32_t make_trap(unsigned i) {
    const NativeFn& nf = g_native_table[i];
    d2rt::Shim s; s.argc = nf.ret / 4; s.stdcall_cleanup = true; char tag[32]; snprintf(tag, sizeof tag, "N_%x", nf.va); s.tag = tag;
    s.fn = [i](Cpu&) -> uint32_t { return native_entry((int)i); };
    g_br->register_shim("NATIVE.dll", tag, s);
    return g_br->shim_trap("NATIVE.dll", tag);
}

void native_init() {
    MB = (uint8_t*)g_membase; TIB = g_tib;
    std::string v; extern bool ini_get(const std::string&, const char*, const char*, std::string&);
    std::string ini = g_root + "/civ2vita.ini";
    if (ini_get(ini, "native", "mode", v)) { if (v == "off" || v == "0") g_mode = 0; else if (v == "on" || v == "2") g_mode = 2; else if (v == "full" || v == "3") g_mode = 3; else g_mode = 1; }
    { FILE* f = fopen((g_root + "/steuerung.ini").c_str(), "r");   // Einstellung aus dem Vita-Menue hat Vorrang
      if (f) { char line[128]; int m; while (fgets(line, sizeof line, f)) if (sscanf(line, "nativ=%d", &m) == 1 && m >= 0 && m <= 3) g_mode = m; fclose(f); } }
    { SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1);
      if ((pad.buttons & SCE_CTRL_LTRIGGER) && (pad.buttons & SCE_CTRL_RTRIGGER)) { g_mode = 1; rlog("[nativ] L+R gehalten: sicherer Modus (nur gepruefte Funktionen)"); } }
    if (ini_get(ini, "native", "trust", v)) g_trust = std::max(1, atoi(v.c_str()));
    unsigned n = g_native_count;
    g_okcnt.assign(n, 0); g_trusted.assign(n, 0); g_bad.assign(n, 0); g_tramp.assign(n, 0);
    memset(g_nat_on, 0, sizeof g_nat_on);
    if (g_mode == 0) { rlog("[nativ] abgeschaltet (civ2vita.ini)"); return; }
    auto okset = read_list("native_ok.txt"), badset = read_list("native_bad4.txt");
    // Trampolinbereich
    const uint32_t TR = 0x00200000; uint32_t tp = TR;
    g_cpu->map(TR, 0x00100000, nullptr, d2rt::P_RWX);
    unsigned patched = 0, pures = 0;
    for (unsigned i = 0; i < n; i++) {
        const NativeFn& nf = g_native_table[i];
        g_idx[nf.va] = i;
        if (badset.count(nf.va)) { g_bad[i] = 1; continue; }
        if (okset.count(nf.va)) g_trusted[i] = 1;
        bool use = (g_mode >= 2) || nf.pure;
        if (!use) continue;
        if (nf.plen == 0) {   // Einsprung nicht patchbar: nur von nativem Code aus direkt aufgerufen
            if (g_mode >= 2) { g_nat_on[i] = 1; patched++; }
            continue;
        }
        if (nf.pure) pures++;
        // Trampolin: verdraengte Befehle + jmp zurueck
        if (tp + nf.plen + 5 > TR + 0x00100000) break;
        memcpy(MB + tp, MB + nf.va, nf.plen);
        uint8_t* j = MB + tp + nf.plen; j[0] = 0xE9; int32_t rel = (int32_t)((nf.va + nf.plen) - (tp + nf.plen + 5)); memcpy(j + 1, &rel, 4);
        g_tramp[i] = tp; tp += (nf.plen + 5 + 15) & ~15u;
        uint32_t trap = make_trap(i);
        if (!trap) { rlog("[nativ] kein Trap fuer %08x", nf.va); continue; }
        uint8_t* p = MB + nf.va; p[0] = 0xE9; rel = (int32_t)(trap - (nf.va + 5)); memcpy(p + 1, &rel, 4);
        for (uint32_t k = 5; k < nf.plen; k++) p[k] = 0xCC;
        g_nat_on[i] = 1; patched++;
    }
    // Funktionen ohne patchbaren Einsprung: ihre ILT-Spruenge direkt auf den Trap umbiegen,
    // damit auch Aufrufe aus Gastcode (Rueckrufe, Funktionszeiger) nativ laufen
    unsigned thunks = 0;
    for (uint32_t a = 0x401000; a < 0x404000 && MB[a] == 0xE9; a += 5) {
        int32_t rel; memcpy(&rel, MB + a + 1, 4); uint32_t t = a + 5 + (uint32_t)rel;
        auto it = g_idx.find(t); if (it == g_idx.end()) continue;
        unsigned i = it->second; const NativeFn& nf = g_native_table[i];
        if (nf.plen != 0 || !g_nat_on[i] || g_bad[i]) continue;
        if (!g_tramp[i]) g_tramp[i] = nf.va;   // Original ist unveraendert: dient als Rueckfall/Pruefreferenz
        static std::unordered_map<uint32_t, uint32_t> traps;
        uint32_t trap = traps.count(i) ? traps[i] : (traps[i] = make_trap(i));
        if (!trap) continue;
        rel = (int32_t)(trap - (a + 5)); memcpy(MB + a + 1, &rel, 4); thunks++;
    }
    if (thunks) rlog("[nativ] %u ILT-Spruenge direkt auf nativen Code umgeleitet", thunks);
    rlog("[nativ] Modus %s: %u von %u uebersetzten Funktionen aktiv (%u pruefbar), %u vertraut, %u gesperrt",
         g_mode == 3 ? "VOLL" : g_mode == 2 ? "AN" : "PRUEFEN", patched, n, pures, (unsigned)okset.size(), (unsigned)badset.size());
}
