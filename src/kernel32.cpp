// civ2vita — KERNEL32 (+ ADVAPI32-Registry)
#include "rt.h"
#include <map>
#include <set>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <psp2/rtc.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

uint32_t g_tib = 0;

// ============================================================ Handle-Tabelle
struct KObj {
    int kind = 0;              // 1=Datei 2=Find 3=Mapping 4=Mutex/sonstiges 5=Std
    int fd = -1;
    std::string path;          // Host-Pfad
    std::vector<std::string> found; size_t fi = 0; std::string dir;
    uint32_t mapFile = 0;      // fuer Mapping: Datei-Handle
    bool writable = false;
};
static std::map<uint32_t, KObj> g_h;
static uint32_t g_hnext = 0x1000;
static uint32_t hnew(KObj o) { uint32_t h = g_hnext; g_hnext += 4; g_h[h] = std::move(o); return h; }
static KObj* hget(uint32_t h) { auto it = g_h.find(h); return it == g_h.end() ? nullptr : &it->second; }

// ============================================================ Codepage 1252
static const uint16_t cp1252_hi[32] = {
 0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
 0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178 };
uint16_t cp1252_to_u(uint8_t ch) { return (ch >= 0x80 && ch < 0xA0) ? cp1252_hi[ch - 0x80] : ch; }
uint8_t u_to_cp1252(uint16_t u) {
    if (u < 0x80 || (u >= 0xA0 && u < 0x100)) return (uint8_t)u;
    for (int i = 0; i < 32; i++) if (cp1252_hi[i] == u) return 0x80 + i;
    return '?';
}
static uint8_t up1252(uint8_t ch) {
    if (ch >= 'a' && ch <= 'z') return ch - 32;
    if (ch >= 0xE0 && ch != 0xF7 && ch != 0xFF) return ch - 32;
    if (ch == 0x9A) return 0x8A; if (ch == 0x9C) return 0x8C; if (ch == 0x9E) return 0x8E; if (ch == 0xFF) return 0x9F;
    return ch;
}
static uint8_t lo1252(uint8_t ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + 32;
    if (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7) return ch + 32;
    if (ch == 0x8A) return 0x9A; if (ch == 0x8C) return 0x9C; if (ch == 0x8E) return 0x9E; if (ch == 0x9F) return 0xFF;
    return ch;
}
uint8_t cp_upper(uint8_t c) { return up1252(c); }
uint8_t cp_lower(uint8_t c) { return lo1252(c); }
static uint16_t ctype1(uint16_t u) {
    // C1_UPPER 1, C1_LOWER 2, C1_DIGIT 4, C1_SPACE 8, C1_PUNCT 0x10, C1_CNTRL 0x20, C1_BLANK 0x40, C1_XDIGIT 0x80, C1_ALPHA 0x100
    uint16_t t = 0;
    if (u < 0x20 || u == 0x7F) t |= 0x20;
    if (u == ' ' || u == '\t') t |= 0x40;
    if (u == ' ' || (u >= 9 && u <= 13)) t |= 8;
    if (u >= '0' && u <= '9') t |= 4 | 0x80;
    if ((u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')) t |= 0x80;
    bool up = (u >= 'A' && u <= 'Z') || (u >= 0xC0 && u <= 0xDE && u != 0xD7);
    bool lo = (u >= 'a' && u <= 'z') || (u >= 0xDF && u <= 0xFF && u != 0xF7);
    if (up) t |= 1 | 0x100; if (lo) t |= 2 | 0x100;
    if (!t && u > 0x20 && u < 0x7F) t |= 0x10;
    if (!t && u >= 0xA0) t |= 0x10;
    return t;
}

// ============================================================ Ressourcen
static uint32_t res_root(uint32_t base, uint32_t* size = nullptr) {
    uint32_t pe = base + rd32(base + 0x3c);
    if (rd32(pe) != 0x4550) return 0;
    uint32_t rva = rd32(pe + 24 + 96 + 2 * 8);
    if (size) *size = rd32(pe + 24 + 96 + 2 * 8 + 4);
    return rva ? base + rva : 0;
}
static bool res_name_eq(uint32_t root, uint32_t ent, uint32_t want) {
    uint32_t nm = rd32(ent);
    if (is_intres(want)) return !(nm & 0x80000000u) && (nm & 0xFFFF) == want;
    const char* s = GS(want);
    if (s[0] == '#') { uint32_t id = (uint32_t)atoi(s + 1); return !(nm & 0x80000000u) && (nm & 0xFFFF) == id; }
    if (!(nm & 0x80000000u)) return false;
    uint32_t str = root + (nm & 0x7FFFFFFFu);
    uint16_t len = rd16(str);
    if (strlen(s) != len) return false;
    for (int i = 0; i < len; i++) {
        uint16_t u = rd16(str + 2 + 2 * i);
        if (toupper((u < 256) ? u : '?') != toupper((uint8_t)s[i])) return false;
    }
    return true;
}
static uint32_t res_find_in(uint32_t root, uint32_t dir, uint32_t want, bool any) {
    uint16_t nn = rd16(dir + 12), ni = rd16(dir + 14);
    for (int i = 0; i < nn + ni; i++) {
        uint32_t ent = dir + 16 + 8 * i;
        if (any || res_name_eq(root, ent, want)) return ent;
    }
    return 0;
}
// liefert VA des IMAGE_RESOURCE_DATA_ENTRY (= HRSRC)
uint32_t res_find(uint32_t base, uint32_t name, uint32_t type) {
    if (!base) base = g_exe_base;
    uint32_t root = res_root(base); if (!root) return 0;
    uint32_t e = res_find_in(root, root, type, false); if (!e) return 0;
    uint32_t off = rd32(e + 4); if (!(off & 0x80000000u)) return 0;
    uint32_t d2 = root + (off & 0x7FFFFFFFu);
    e = res_find_in(root, d2, name, false); if (!e) return 0;
    off = rd32(e + 4);
    if (off & 0x80000000u) {   // Sprachebene: erste nehmen
        uint32_t d3 = root + (off & 0x7FFFFFFFu);
        e = res_find_in(root, d3, 0, true); if (!e) return 0;
        off = rd32(e + 4);
        if (off & 0x80000000u) return 0;
    }
    return root + off;
}
uint32_t res_data(uint32_t base, uint32_t hrsrc, uint32_t* size) {
    if (!hrsrc) return 0;
    if (!base) base = g_exe_base;
    if (size) *size = rd32(hrsrc + 4);
    return base + rd32(hrsrc);
}
// Modul zu einer Adresse
static std::map<uint32_t, std::string> g_mods;     // base -> Name (Kleinschreibung)
static std::map<uint32_t, std::string> g_sysmods;  // Pseudo-Handle -> System-DLL
uint32_t module_of(uint32_t va) {
    uint32_t best = g_exe_base;
    for (auto& kv : g_mods) if (kv.first <= va && kv.first >= best) best = kv.first;
    return best;
}
uint32_t res_find_any(uint32_t hinst, uint32_t name, uint32_t type, uint32_t* base_out) {
    uint32_t b = hinst ? hinst : g_exe_base;
    uint32_t r = res_find(b, name, type);
    if (!r && b != g_exe_base) { b = g_exe_base; r = res_find(b, name, type); }
    if (base_out) *base_out = b;
    return r;
}

// ============================================================ INI
struct Ini { std::vector<std::pair<std::string, std::vector<std::pair<std::string,std::string>>>> sec; };
static std::string ieq(std::string s){ for (auto& ch : s) ch = toupper(ch); return s; }
static std::string trim(const std::string& s){ size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
static Ini ini_load(const std::string& p) {
    Ini ini; FILE* f = fopen(p.c_str(), "rb"); if (!f) return ini;
    char line[1024]; int cur = -1;
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == ';') continue;
        if (l[0] == '[') { size_t e = l.find(']'); ini.sec.push_back({l.substr(1, e == std::string::npos ? std::string::npos : e - 1), {}}); cur = ini.sec.size() - 1; continue; }
        size_t eq = l.find('='); if (eq == std::string::npos || cur < 0) continue;
        ini.sec[cur].second.push_back({trim(l.substr(0, eq)), trim(l.substr(eq + 1))});
    }
    fclose(f); return ini;
}
static void ini_save(const std::string& p, const Ini& ini) {
    FILE* f = fopen(p.c_str(), "wb"); if (!f) return;
    for (auto& s : ini.sec) { fprintf(f, "[%s]\r\n", s.first.c_str()); for (auto& kv : s.second) fprintf(f, "%s=%s\r\n", kv.first.c_str(), kv.second.c_str()); fprintf(f, "\r\n"); }
    fclose(f);
}
static std::string ini_path(uint32_t file) {
    const char* f = file ? GS(file) : "WIN.INI";
    if (!strchr(f, '\\') && !strchr(f, '/') && !strchr(f, ':')) return g_root + "/" + f;
    return win2host(f);
}
bool ini_get(const std::string& path, const char* app, const char* key, std::string& out) {
    Ini ini = ini_load(path);
    for (auto& s : ini.sec) if (ieq(s.first) == ieq(app))
        for (auto& kv : s.second) if (ieq(kv.first) == ieq(key)) { out = kv.second; return true; }
    return false;
}

// ============================================================ Registry (persistiert als Textdatei)
static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> g_reg;  // key -> (name -> [type(4) + data])
static std::map<uint32_t, std::string> g_hkeys;
static uint32_t g_hknext = 0x2000;
static void reg_load() {
    FILE* f = fopen((g_root + "/registry.dat").c_str(), "rb"); if (!f) return;
    char line[4096]; std::string key;
    while (fgets(line, sizeof line, f)) {
        std::string l = trim(line); if (l.empty()) continue;
        if (l[0] == '[') { key = l.substr(1, l.size() - 2); g_reg[key]; continue; }
        size_t eq = l.find('='); if (eq == std::string::npos) continue;
        std::string nm = l.substr(0, eq), hex = l.substr(eq + 1);
        std::vector<uint8_t> v;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) v.push_back((uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
        g_reg[key][nm] = v;
    }
    fclose(f);
}
static void reg_save() {
    FILE* f = fopen((g_root + "/registry.dat").c_str(), "wb"); if (!f) return;
    for (auto& k : g_reg) { fprintf(f, "[%s]\n", k.first.c_str());
        for (auto& v : k.second) { fprintf(f, "%s=", v.first.c_str()); for (auto b : v.second) fprintf(f, "%02x", b); fprintf(f, "\n"); } }
    fclose(f);
}
static std::string hkey_name(uint32_t h) {
    switch (h) { case 0x80000000: return "HKCR"; case 0x80000001: return "HKCU"; case 0x80000002: return "HKLM"; case 0x80000003: return "HKU"; }
    auto it = g_hkeys.find(h); return it == g_hkeys.end() ? "" : it->second;
}

// ============================================================ Find-Hilfen
static bool wild(const char* p, const char* s) {
    if (!*p) return !*s;
    if (*p == '*') { for (;;) { if (wild(p + 1, s)) return true; if (!*s) return false; s++; } }
    if (!*s) return (*p == '.' && p[1] == '*' && !p[2]); // "*.*" passt auch ohne Punkt
    if (*p == '?' || toupper((uint8_t)*p) == toupper((uint8_t)*s)) return wild(p + 1, s + 1);
    return false;
}
static void fill_find(uint32_t fd, const std::string& dir, const std::string& name) {
    memset(G(fd), 0, 320);
    struct stat st; std::string full = dir + "/" + name;
    bool isdir = false; uint32_t sz = 0;
    if (stat(full.c_str(), &st) == 0) { isdir = S_ISDIR(st.st_mode); sz = (uint32_t)st.st_size; }
    wr32(fd, isdir ? 0x10 : 0x20);
    uint64_t ft = 125911584000000000ull; // ca. 1999
    memcpy(G(fd + 4), &ft, 8); memcpy(G(fd + 12), &ft, 8); memcpy(G(fd + 20), &ft, 8);
    wr32(fd + 32, sz);
    strncpy(GS(fd + 44), name.c_str(), 259);
    std::string alt = name.size() <= 12 ? name : name.substr(0, 12);
    for (auto& ch : alt) ch = toupper(ch);
    strncpy(GS(fd + 304), alt.c_str(), 13);
}

static std::string norm_win(const char* p) {   // vollen Windows-Pfad bilden
    std::string s(p ? p : "");
    for (auto& ch : s) if (ch == '/') ch = '\\';
    if (s.size() >= 2 && s[1] == ':') return s;
    if (!s.empty() && s[0] == '\\') return "C:" + s;
    std::string r = g_cwd_win;
    if (r.back() != '\\') r += "\\";
    r += s;
    // .\ und ..\ aufloesen
    std::vector<std::string> parts; std::string cur;
    for (size_t i = 3; i <= r.size(); ++i) { char ch = i < r.size() ? r[i] : '\\';
        if (ch == '\\') { if (cur == "..") { if (!parts.empty()) parts.pop_back(); } else if (!cur.empty() && cur != ".") parts.push_back(cur); cur.clear(); } else cur += ch; }
    std::string o = r.substr(0, 2) + "\\";
    for (size_t i = 0; i < parts.size(); ++i) { if (i) o += "\\"; o += parts[i]; }
    return o;
}

// ============================================================ Datei-Funktionen
static uint32_t file_open(const char* winpath, uint32_t access, uint32_t disp) {
    std::string p = win2host(winpath);
    int fl = 0;
    bool rd = access & 0x80000000u, wrt = access & 0x40000000u;
    if (rd && wrt) fl = O_RDWR; else if (wrt) fl = O_WRONLY; else fl = O_RDONLY;
    switch (disp) {
        case 1: fl |= O_CREAT | O_EXCL; break;
        case 2: fl |= O_CREAT | O_TRUNC; break;
        case 3: break;
        case 4: fl |= O_CREAT; break;
        case 5: fl |= O_TRUNC; break;
    }
    if ((fl & (O_CREAT|O_TRUNC)) && !(fl & (O_WRONLY|O_RDWR))) fl |= O_RDWR;
    int fd = open(p.c_str(), fl, 0666);
    if (fd < 0) {
        struct stat st;
        g_lasterr = (disp == 1 && stat(p.c_str(), &st) == 0) ? 80 : 2;
        rlog("CreateFile FEHLT: %s (%s) disp=%u", winpath, p.c_str(), disp);
        return 0xFFFFFFFFu;
    }
    KObj o; o.kind = 1; o.fd = fd; o.path = p; o.writable = wrt;
    g_lasterr = 0;
    return hnew(o);
}

// ============================================================ Mappings
struct View { uint32_t va; uint32_t len; uint32_t off; std::string path; bool w; };
static std::map<uint32_t, View> g_views;

// ============================================================ Module laden
static std::string base_lc(const char* p) {
    std::string s(p); size_t k = s.find_last_of("\\/"); if (k != std::string::npos) s = s.substr(k + 1);
    for (auto& ch : s) ch = tolower(ch);
    if (s.find('.') == std::string::npos) s += ".dll";
    return s;
}
static const char* kSysDlls[] = { "kernel32.dll","user32.dll","gdi32.dll","winmm.dll","advapi32.dll","comctl32.dll",
    "ddraw.dll","msvfw32.dll","avifil32.dll","comdlg32.dll","xdaemon.dll","shell32.dll","ole32.dll","version.dll","dsound.dll","ntdll.dll", nullptr };
static uint32_t sysmod_handle(const std::string& n) {
    for (int i = 0; kSysDlls[i]; i++) if (n == kSysDlls[i]) { uint32_t h = 0x7E000000u + 0x10000u * i; g_sysmods[h] = n; return h; }
    return 0;
}
std::vector<uint8_t> slurp(const std::string& p) {
    std::vector<uint8_t> b; FILE* f = fopen(p.c_str(), "rb"); if (!f) return b;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    b.resize(n); if (n > 0 && fread(b.data(), 1, n, f) != (size_t)n) b.clear(); fclose(f); return b;
}
static uint32_t load_library(const char* name, bool asData) {
    std::string n = base_lc(name);
    if (uint32_t h = sysmod_handle(n)) return h;
    for (auto& kv : g_mods) if (kv.second == n) return kv.first;
    if (n == "civ2uia.dll") { rlog("LoadLibrary %s -> uebersprungen", name); return 0; }
    std::string p = win2host(name);
    auto bytes = slurp(p);
    if (bytes.empty()) { std::string p2 = g_root + "/" + n; bytes = slurp(p2); }
    if (bytes.empty()) { rlog("LoadLibrary %s: Datei fehlt", name); g_lasterr = 126; return 0; }
    std::string err;
    d2rt::PeImage* pi = g_br->load_library_runtime(n, bytes, err);
    if (!pi) { rlog("LoadLibrary %s: %s", name, err.c_str()); g_lasterr = 193; return 0; }
    uint32_t base = pi->load_base();
    g_mods[base] = n;
    rlog("LoadLibrary %s -> %08x (%u KiB)", n.c_str(), base, pi->image_size() >> 10);
    static const char* kResOnly[] = { "tiles.dll", "civ2art.dll", "intro.dll", "mk.dll", "cv.dll", "pv.dll", "ss.dll", "wonder.dll", "timerdll.dll" };
    bool skipMain = false; for (auto* r : kResOnly) if (n == r) skipMain = true;   // reine Ressourcen-DLLs bzw. nativ ersetzt: kein x86-DllMain
    if (!asData && pi->entry_rva() && !skipMain) {
        uint32_t r = gcall(pi->entry_va(), { base, 1, 0 });
        rlog("  DllMain -> %u", r);
    }
    return base;
}

// ============================================================ Install
void install_kernel32() {
    reg_load();
    const char* K = "KERNEL32.dll";
    g_mods[g_exe_base] = "civ2.exe";

    // ---- Prozess/Umgebung
    static uint32_t cmdline = 0, envA = 0, envW = 0;
    cmdline = gstrdup("C:\\CIV2\\CIV2.EXE");
    envA = galloc(8); envW = galloc(8);
    reg(K, "GetCommandLineA", 0, [](Cpu&) { return cmdline; });
    reg(K, "GetVersion", 0, [](Cpu&) { return 0xC0000A04u; });
    reg(K, "GetStartupInfoA", 1, [](Cpu& c) { memset(G(A(0)), 0, 68); wr32(A(0), 68); return 0u; });
    reg(K, "GetEnvironmentStrings", 0, [](Cpu&) { return envA; });
    reg(K, "GetEnvironmentStringsW", 0, [](Cpu&) { return envW; });
    reg(K, "FreeEnvironmentStringsA", 1, [](Cpu&) { return 1u; });
    reg(K, "FreeEnvironmentStringsW", 1, [](Cpu&) { return 1u; });
    reg(K, "SetEnvironmentVariableA", 2, [](Cpu&) { return 1u; });
    reg(K, "GetStdHandle", 1, [](Cpu& c) { uint32_t n = A(0); return (n == 0xFFFFFFF6u) ? 0x10u : (n == 0xFFFFFFF5u) ? 0x14u : 0x18u; });
    reg(K, "SetStdHandle", 2, [](Cpu&) { return 1u; });
    reg(K, "SetHandleCount", 1, [](Cpu& c) { return A(0); });
    reg(K, "GetFileType", 1, [](Cpu& c) { uint32_t h = A(0); return (h == 0x10 || h == 0x14 || h == 0x18) ? 2u : hget(h) ? 1u : 0u; });
    reg(K, "SetErrorMode", 1, [](Cpu&) { return 0u; });
    reg(K, "GetLastError", 0, [](Cpu&) { return g_lasterr; });
    reg(K, "SetLastError", 1, [](Cpu& c) { g_lasterr = A(0); return 0u; });
    reg(K, "GetCurrentProcess", 0, [](Cpu&) { return 0xFFFFFFFFu; });
    reg(K, "GetCurrentProcessId", 0, [](Cpu&) { return 0xAE4u; });
    reg(K, "GetCurrentThreadId", 0, [](Cpu&) { return 0xAF0u; });
    reg(K, "ExitProcess", 1, [](Cpu& c) -> uint32_t {
        rlog("ExitProcess(%u) von %08x", A(0), c.trap_retaddr());
        uint32_t esp = c.reg(d2rt::R_ESP); std::string chain;
        for (int i = 0; i < 400 && chain.size() < 300; i++) { uint32_t v = rd32(esp + 4 * i); if (v > 0x401000 && v < 0x61C000) { char b[12]; snprintf(b, 12, "%08x ", v); chain += b; } }
        rlog("  Stapel: %s", chain.c_str()); rlog_flush(); sceKernelExitProcess(0); return 0; });
    reg(K, "TerminateProcess", 2, [](Cpu& c) -> uint32_t { rlog("TerminateProcess(%u)", A(1)); rlog_flush(); sceKernelExitProcess(0); return 0; });
    reg(K, "WinExec", 2, [](Cpu& c) { rlog("WinExec(%s) ignoriert", GS(A(0))); return 2u; });
    reg(K, "SetConsoleCtrlHandler", 2, [](Cpu&) { return 1u; });
    reg(K, "SetUnhandledExceptionFilter", 1, [](Cpu&) { return 0u; });
    reg(K, "UnhandledExceptionFilter", 1, [](Cpu& c) {
        uint32_t ep = A(0), er = rd32(ep);
        rlog("UnhandledException code=%08x addr=%08x", rd32(er), rd32(er + 12)); rlog_flush(); return 1u; });
    reg(K, "IsBadReadPtr", 2, [](Cpu& c) { return A(0) ? 0u : 1u; });
    reg(K, "IsBadWritePtr", 2, [](Cpu& c) { return A(0) ? 0u : 1u; });
    reg(K, "IsBadCodePtr", 1, [](Cpu& c) { return A(0) ? 0u : 1u; });
    reg(K, "IsBadHugeReadPtr", 2, [](Cpu& c) { return A(0) ? 0u : 1u; });
    reg(K, "OutputDebugStringA", 1, [](Cpu& c) { rlog("[dbg] %s", GS(A(0))); return 0u; });
    reg(K, "DebugBreak", 0, [](Cpu&) { rlog("DebugBreak"); return 0u; });
    reg(K, "InterlockedIncrement", 1, [](Cpu& c) { uint32_t v = rd32(A(0)) + 1; wr32(A(0), v); return v; });
    reg(K, "InterlockedDecrement", 1, [](Cpu& c) { uint32_t v = rd32(A(0)) - 1; wr32(A(0), v); return v; });
    reg(K, "RtlUnwind", 4, [](Cpu& c) { uint32_t tf = A(0); if (tf && g_tib) wr32(g_tib, tf); return 0u; });
    // ---- Threads/TLS/Sync (einthreadig)
    static uint32_t tls[128]; static bool tlsused[128];
    reg(K, "TlsAlloc", 0, [](Cpu&) { for (int i = 0; i < 128; i++) if (!tlsused[i]) { tlsused[i] = true; tls[i] = 0; return (uint32_t)i; } return 0xFFFFFFFFu; });
    reg(K, "TlsFree", 1, [](Cpu& c) { if (A(0) < 128) tlsused[A(0)] = false; return 1u; });
    reg(K, "TlsGetValue", 1, [](Cpu& c) { g_lasterr = 0; return A(0) < 128 ? tls[A(0)] : 0u; });
    reg(K, "TlsSetValue", 2, [](Cpu& c) { if (A(0) < 128) tls[A(0)] = A(1); return 1u; });
    reg(K, "GetCurrentThread", 0, [](Cpu&) { return 0xFFFFFFFEu; });
    reg(K, "InitializeCriticalSection", 1, [](Cpu& c) { memset(G(A(0)), 0, 24); return 0u; });
    reg(K, "DeleteCriticalSection", 1, [](Cpu&) { return 0u; });
    reg(K, "EnterCriticalSection", 1, [](Cpu&) { return 0u; });
    reg(K, "LeaveCriticalSection", 1, [](Cpu&) { return 0u; });
    reg(K, "FatalAppExitA", 2, [](Cpu& c) -> uint32_t { fatal("FatalAppExit: %s", A(1) ? GS(A(1)) : ""); return 0; });
    reg(K, "GetUserDefaultLCID", 0, [](Cpu&) { return 0x0407u; });
    reg(K, "GetSystemDefaultLCID", 0, [](Cpu&) { return 0x0407u; });
    reg(K, "GetThreadLocale", 0, [](Cpu&) { return 0x0407u; });
    reg(K, "IsValidCodePage", 1, [](Cpu&) { return 1u; });
    reg(K, "IsValidLocale", 2, [](Cpu&) { return 1u; });
    auto locinfo = [](uint32_t type) -> std::string {
        switch (type & 0xFFFF) {
            case 0x1004: return "1252"; case 0x000B: return "850"; case 0x0001: return "0407"; case 0x0002: return "Deutsch";
            case 0x1001: return "German"; case 0x0003: return "DEU"; case 0x0007: return "DE"; case 0x1002: return "Germany"; case 0x0005: return "49";
            case 0x000E: return ","; case 0x000F: return "."; case 0x001D: return "."; case 0x001E: return ":"; case 0x0014: return "DM";
            case 0x0021: return "1"; case 0x0023: return "1"; case 0x0011: return "2"; case 0x0010: return "3;0";
        }
        return ""; };
    reg(K, "GetLocaleInfoA", 4, [locinfo](Cpu& c) {
        std::string v = locinfo(A(1)); if (v.empty()) return 0u;
        if (!A(3)) return (uint32_t)v.size() + 1; if (A(3) <= v.size()) return 0u;
        strcpy(GS(A(2)), v.c_str()); return (uint32_t)v.size() + 1; });
    reg(K, "GetLocaleInfoW", 4, [locinfo](Cpu& c) {
        std::string v = locinfo(A(1)); if (v.empty()) return 0u;
        if (!A(3)) return (uint32_t)v.size() + 1; if (A(3) <= v.size()) return 0u;
        for (size_t i = 0; i <= v.size(); i++) wr16(A(2) + 2 * i, (uint8_t)v.c_str()[i]); return (uint32_t)v.size() + 1; });
    reg(K, "CreateMutexA", 3,[](Cpu&) { KObj o; o.kind = 4; g_lasterr = 0; return hnew(o); });
    reg(K, "ReleaseMutex", 1, [](Cpu&) { return 1u; });
    reg(K, "WaitForSingleObject", 2, [](Cpu&) { return 0u; });
    reg(K, "Sleep", 1, [](Cpu& c) { uint32_t ms = A(0); pump_internal(); if (ms) plat_sleep_ms(ms > 100 ? 100 : ms); return 0u; });
    reg(K, "GetTickCount", 0, [](Cpu&) { timers_poll(); return now_ms(); });
    reg(K, "QueryPerformanceCounter", 1, [](Cpu& c) { uint64_t v = now_us(); memcpy(G(A(0)), &v, 8); return 1u; });
    reg(K, "QueryPerformanceFrequency", 1, [](Cpu& c) { uint64_t v = 1000000; memcpy(G(A(0)), &v, 8); return 1u; });
    auto systime = [](uint32_t p) {
        SceDateTime t; sceRtcGetCurrentClockLocalTime(&t);
        uint16_t* s = (uint16_t*)G(p);
        s[0] = t.year; s[1] = t.month; s[3] = t.day; s[4] = t.hour; s[5] = t.minute; s[6] = t.second; s[7] = t.microsecond / 1000;
        s[2] = (uint16_t)sceRtcGetDayOfWeek(t.year, t.month, t.day);
    };
    reg(K, "GetLocalTime", 1, [systime](Cpu& c) { systime(A(0)); return 0u; });
    reg(K, "GetSystemTime", 1, [systime](Cpu& c) { systime(A(0)); return 0u; });
    reg(K, "GetTimeZoneInformation", 1, [](Cpu& c) { memset(G(A(0)), 0, 172); return 0u; });

    // ---- Speicher
    static uint32_t vbump = 0x04800000;
    struct VReg { uint32_t lo, hi; };
    static std::vector<VReg> vregs;
    g_cpu->map(0x04800000, 0x00800000, nullptr, d2rt::P_RW);
    reg(K, "VirtualAlloc", 4, [](Cpu& c) -> uint32_t {
        uint32_t addr = A(0), size = A(1), type = A(2);
        if (addr) {
            for (auto& r : vregs) if (addr >= r.lo && addr < r.hi) return addr & ~0xFFFu;
            if (!(type & 0x2000)) { rlog("VirtualAlloc: unbekannte Adresse %08x", addr); }
        }
        uint32_t sz = (size + 0xFFFF) & ~0xFFFFu;
        if (vbump + sz > 0x05000000) { rlog("VirtualAlloc: voll"); return 0; }
        uint32_t a = vbump; vbump += sz;
        memset(G(a), 0, sz);
        vregs.push_back({a, a + sz});
        return a; });
    reg(K, "VirtualFree", 3, [](Cpu&) { return 1u; });
    reg(K, "HeapCreate", 3, [](Cpu&) { static uint32_t n = 0x00A00000; return n += 0x10; });
    reg(K, "HeapDestroy", 1, [](Cpu&) { return 1u; });
    reg(K, "HeapAlloc", 3, [](Cpu& c) { return galloc(A(2), true); });
    reg(K, "HeapFree", 3, [](Cpu& c) { gfree(A(2)); return 1u; });
    reg(K, "HeapReAlloc", 4, [](Cpu& c) {
        if (A(1) & 0x10) { return A(3) <= rd32(A(2) - 8) ? A(2) : 0u; } // HEAP_REALLOC_IN_PLACE_ONLY
        return grealloc(A(2), A(3)); });
    reg(K, "HeapSize", 3, [](Cpu& c) { return gsize(A(2)); });
    reg(K, "HeapValidate", 3, [](Cpu&) { return 1u; });
    reg(K, "GlobalAlloc", 2, [](Cpu& c) { return galloc(A(1) ? A(1) : 1, true); });
    reg(K, "GlobalLock", 1, [](Cpu& c) { return A(0); });
    reg(K, "GlobalUnlock", 1, [](Cpu&) { return 0u; });
    reg(K, "GlobalHandle", 1, [](Cpu& c) { return A(0); });
    reg(K, "GlobalSize", 1, [](Cpu& c) { return gsize(A(0)); });
    reg(K, "GlobalFree", 1, [](Cpu& c) { gfree(A(0)); return 0u; });
    reg(K, "GlobalCompact", 1, [](Cpu&) { return 0x01000000u; });
    reg(K, "GlobalReAlloc", 3, [](Cpu& c) { return grealloc(A(0), A(1)); });
    reg(K, "GlobalMemoryStatus", 1, [](Cpu& c) { uint32_t p = A(0); memset(G(p), 0, 32); wr32(p, 32); wr32(p+8, 64u<<20); wr32(p+12, 48u<<20); wr32(p+16, 128u<<20); wr32(p+20,96u<<20); wr32(p+24,0x7FFE0000); wr32(p+28,0x7F000000); return 0u; });
    reg(K, "LocalAlloc", 2, [](Cpu& c) { return galloc(A(1) ? A(1) : 1, true); });
    reg(K, "LocalLock", 1, [](Cpu& c) { return A(0); });
    reg(K, "LocalUnlock", 1, [](Cpu&) { return 0u; });
    reg(K, "LocalFree", 1, [](Cpu& c) { gfree(A(0)); return 0u; });

    // ---- Zeichenketten
    reg(K, "lstrlenA", 1, [](Cpu& c) { return A(0) ? (uint32_t)strlen(GS(A(0))) : 0u; });
    reg(K, "lstrcpyA", 2, [](Cpu& c) { strcpy(GS(A(0)), GS(A(1))); return A(0); });
    reg(K, "lstrcatA", 2, [](Cpu& c) { strcat(GS(A(0)), GS(A(1))); return A(0); });
    reg(K, "lstrcmpiA", 2, [](Cpu& c) { return (uint32_t)strcasecmp(GS(A(0)), GS(A(1))); });
    reg(K, "lstrcmpA", 2, [](Cpu& c) { return (uint32_t)strcmp(GS(A(0)), GS(A(1))); });
    reg(K, "GetACP", 0, [](Cpu&) { return 1252u; });
    reg(K, "GetOEMCP", 0, [](Cpu&) { return 850u; });
    reg(K, "GetCPInfo", 2, [](Cpu& c) { uint32_t p = A(1); memset(G(p), 0, 20); wr32(p, 1); wr8(p + 4, '?'); return 1u; });
    reg(K, "MultiByteToWideChar", 6, [](Cpu& c) {
        uint32_t src = A(2); int n = (int)A(3); uint32_t dst = A(4); int dn = (int)A(5);
        if (n < 0) n = strlen(GS(src)) + 1;
        if (!dn) return (uint32_t)n;
        int k = n < dn ? n : dn;
        for (int i = 0; i < k; i++) wr16(dst + 2 * i, cp1252_to_u(*(uint8_t*)G(src + i)));
        return (uint32_t)k; });
    reg(K, "WideCharToMultiByte", 8, [](Cpu& c) {
        uint32_t src = A(2); int n = (int)A(3); uint32_t dst = A(4); int dn = (int)A(5);
        if (n < 0) { n = 0; while (rd16(src + 2 * n)) n++; n++; }
        if (!dn) return (uint32_t)n;
        int k = n < dn ? n : dn;
        for (int i = 0; i < k; i++) wr8(dst + i, u_to_cp1252(rd16(src + 2 * i)));
        if (A(7)) wr32(A(7), 0);
        return (uint32_t)k; });
    reg(K, "GetStringTypeW", 4, [](Cpu& c) {
        uint32_t src = A(1); int n = (int)A(2); uint32_t out = A(3);
        if (n < 0) { n = 0; while (rd16(src + 2 * n)) n++; n++; }
        for (int i = 0; i < n; i++) wr16(out + 2 * i, A(0) == 1 ? ctype1(rd16(src + 2 * i)) : 0);
        return 1u; });
    reg(K, "GetStringTypeA", 5, [](Cpu& c) {
        uint32_t src = A(2); int n = (int)A(3); uint32_t out = A(4);
        if (n < 0) n = strlen(GS(src)) + 1;
        for (int i = 0; i < n; i++) wr16(out + 2 * i, A(1) == 1 ? ctype1(*(uint8_t*)G(src + i)) : 0);
        return 1u; });
    reg(K, "LCMapStringA", 6, [](Cpu& c) {
        uint32_t fl = A(1), src = A(2); int n = (int)A(3); uint32_t dst = A(4); int dn = (int)A(5);
        if (n < 0) n = strlen(GS(src)) + 1;
        if (!dn) return (uint32_t)n;
        int k = n < dn ? n : dn;
        for (int i = 0; i < k; i++) { uint8_t ch = *(uint8_t*)G(src + i); wr8(dst + i, (fl & 0x200) ? up1252(ch) : (fl & 0x100) ? lo1252(ch) : ch); }
        return (uint32_t)k; });
    reg(K, "LCMapStringW", 6, [](Cpu& c) {
        uint32_t fl = A(1), src = A(2); int n = (int)A(3); uint32_t dst = A(4); int dn = (int)A(5);
        if (n < 0) { n = 0; while (rd16(src + 2 * n)) n++; n++; }
        if (!dn) return (uint32_t)n;
        int k = n < dn ? n : dn;
        for (int i = 0; i < k; i++) { uint16_t u = rd16(src + 2 * i);
            if (u < 256) u = (fl & 0x200) ? up1252(u) : (fl & 0x100) ? lo1252(u) : u;
            wr16(dst + 2 * i, u); }
        return (uint32_t)k; });
    reg(K, "CompareStringA", 6, [](Cpu& c) {
        uint32_t fl = A(1), s1 = A(2); int n1 = (int)A(3); uint32_t s2 = A(4); int n2 = (int)A(5);
        if (n1 < 0) n1 = strlen(GS(s1)); if (n2 < 0) n2 = strlen(GS(s2));
        int n = n1 < n2 ? n1 : n2;
        for (int i = 0; i < n; i++) { uint8_t a = *(uint8_t*)G(s1 + i), b = *(uint8_t*)G(s2 + i);
            if (fl & 1) { a = up1252(a); b = up1252(b); }
            if (a != b) return a < b ? 1u : 3u; }
        return n1 == n2 ? 2u : n1 < n2 ? 1u : 3u; });
    reg(K, "CompareStringW", 6, [](Cpu& c) {
        uint32_t fl = A(1), s1 = A(2); int n1 = (int)A(3); uint32_t s2 = A(4); int n2 = (int)A(5);
        if (n1 < 0) { n1 = 0; while (rd16(s1 + 2 * n1)) n1++; } if (n2 < 0) { n2 = 0; while (rd16(s2 + 2 * n2)) n2++; }
        int n = n1 < n2 ? n1 : n2;
        for (int i = 0; i < n; i++) { uint16_t a = rd16(s1 + 2 * i), b = rd16(s2 + 2 * i);
            if ((fl & 1) && a < 256 && b < 256) { a = up1252(a); b = up1252(b); }
            if (a != b) return a < b ? 1u : 3u; }
        return n1 == n2 ? 2u : n1 < n2 ? 1u : 3u; });

    // ---- Module & Ressourcen
    reg(K, "GetModuleHandleA", 1, [](Cpu& c) -> uint32_t {
        if (!A(0)) return g_exe_base;
        std::string n = base_lc(GS(A(0)));
        if (n == "civ2.exe") return g_exe_base;
        for (auto& kv : g_mods) if (kv.second == n) return kv.first;
        return sysmod_handle(n); });
    reg(K, "GetModuleFileNameA", 3, [](Cpu& c) {
        uint32_t h = A(0); std::string n = "C:\\CIV2\\CIV2.EXE";
        auto it = g_mods.find(h);
        if (h && it != g_mods.end() && h != g_exe_base) n = "C:\\CIV2\\" + it->second;
        uint32_t m = A(2); if (!m) return 0u;
        strncpy(GS(A(1)), n.c_str(), m); GS(A(1))[m - 1] = 0;
        return (uint32_t)strlen(GS(A(1))); });
    reg(K, "LoadLibraryA", 1, [](Cpu& c) { return load_library(GS(A(0)), false); });
    reg(K, "LoadLibraryExA", 3, [](Cpu& c) { return load_library(GS(A(0)), (A(2) & 2) != 0); });
    reg(K, "FreeLibrary", 1, [](Cpu&) { return 1u; });
    reg(K, "GetProcAddress", 2, [](Cpu& c) -> uint32_t {
        uint32_t h = A(0), nm = A(1);
        auto sm = g_sysmods.find(h);
        if (sm != g_sysmods.end()) {
            if (is_intres(nm)) { rlog("GetProcAddress(%s, #%u) nicht unterstuetzt", sm->second.c_str(), nm); return 0; }
            std::string dll = sm->second; for (auto& ch : dll) ch = toupper(ch);
            dll = dll.substr(0, dll.size() - 4) + ".dll";
            if (!shim_known(dll, GS(nm))) { rlog("GetProcAddress(%s, %s) -> fehlt", dll.c_str(), GS(nm)); return 0; }
            return g_br->shim_trap(dll, GS(nm));
        }
        auto it = g_mods.find(h ? h : g_exe_base);
        if (it == g_mods.end()) return 0;
        if (!is_intres(nm) && it->second != "civ2.exe") {   // nativ ersetzte DLL-Funktionen (z.B. timerdll.dll)
            std::string dll = it->second; for (auto& ch : dll) ch = toupper(ch); dll = dll.substr(0, dll.size() - 4) + ".dll";
            if (shim_known(dll, GS(nm))) return g_br->shim_trap(dll, GS(nm)); }
        d2rt::PeImage* pi = g_br->module(it->second == "civ2.exe" ? "civ2.exe" : it->second);
        if (!pi) return 0;
        uint32_t va = is_intres(nm) ? pi->export_va_ordinal(nm) : pi->export_va(GS(nm));
        if (!va) rlog("GetProcAddress(%s, %s) -> fehlt", it->second.c_str(), is_intres(nm) ? "#" : GS(nm));
        return va; });
    reg(K, "FindResourceA", 3, [](Cpu& c) {
        uint32_t r = res_find(A(0), A(1), A(2));
        if (!r) { char nb[32], tb[32]; if (is_intres(A(1))) snprintf(nb, 32, "#%u", A(1)); else snprintf(nb, 32, "%.30s", GS(A(1)));
            if (is_intres(A(2))) snprintf(tb, 32, "#%u", A(2)); else snprintf(tb, 32, "%.30s", GS(A(2)));
            rlog("FindResource(%08x, %s, %s) fehlt", A(0), nb, tb); }
        return r; });
    reg(K, "LoadResource", 2, [](Cpu& c) { return res_data(A(0), A(1), nullptr); });
    reg(K, "LockResource", 1, [](Cpu& c) { return A(0); });
    reg(K, "FreeResource", 1, [](Cpu&) { return 0u; });
    reg(K, "SizeofResource", 2, [](Cpu& c) { return A(1) ? rd32(A(1) + 4) : 0u; });

    // ---- Dateien
    reg(K, "CreateFileA", 7, [](Cpu& c) { return file_open(GS(A(0)), A(1), A(4)); });
    reg(K, "CloseHandle", 1, [](Cpu& c) {
        KObj* o = hget(A(0)); if (!o) return 1u;
        if (o->fd >= 0) close(o->fd);
        g_h.erase(A(0)); return 1u; });
    reg(K, "_lclose", 1, [](Cpu& c) { KObj* o = hget(A(0)); if (o) { if (o->fd >= 0) close(o->fd); g_h.erase(A(0)); } return 0u; });
    reg(K, "ReadFile", 5, [](Cpu& c) {
        KObj* o = hget(A(0)); if (!o || o->fd < 0) { if (A(3)) wr32(A(3), 0); return 0u; }
        int r = A(2) ? read(o->fd, G(A(1)), A(2)) : 0;
        if (A(3)) wr32(A(3), r < 0 ? 0 : r);
        return r < 0 ? 0u : 1u; });
    reg(K, "WriteFile", 5, [](Cpu& c) {
        uint32_t h = A(0);
        if (h == 0x14 || h == 0x18) { std::string s(GS(A(1)), A(2)); rlog("[stdout] %s", s.c_str()); if (A(3)) wr32(A(3), A(2)); return 1u; }
        KObj* o = hget(h); if (!o || o->fd < 0) return 0u;
        int r = A(2) ? write(o->fd, G(A(1)), A(2)) : 0;
        if (A(3)) wr32(A(3), r < 0 ? 0 : r);
        return r < 0 ? 0u : 1u; });
    reg(K, "_hread", 3, [](Cpu& c) { KObj* o = hget(A(0)); if (!o) return 0xFFFFFFFFu; int r = read(o->fd, G(A(1)), A(2)); return (uint32_t)r; });
    reg(K, "_lread", 3, [](Cpu& c) { KObj* o = hget(A(0)); if (!o) return 0xFFFFFFFFu; int r = read(o->fd, G(A(1)), A(2)); return (uint32_t)r; });
    reg(K, "_llseek", 3, [](Cpu& c) { KObj* o = hget(A(0)); if (!o) return 0xFFFFFFFFu; return (uint32_t)lseek(o->fd, (int32_t)A(1), (int)A(2)); });
    reg(K, "SetFilePointer", 4, [](Cpu& c) {
        KObj* o = hget(A(0)); if (!o) return 0xFFFFFFFFu;
        int64_t off = (int32_t)A(1);
        if (A(2)) { off = ((int64_t)(int32_t)rd32(A(2)) << 32) | A(1); wr32(A(2), 0); }
        off_t r = lseek(o->fd, (off_t)off, (int)A(3));
        return r < 0 ? 0xFFFFFFFFu : (uint32_t)r; });
    reg(K, "GetFileSize", 2, [](Cpu& c) {
        KObj* o = hget(A(0)); if (!o) return 0xFFFFFFFFu;
        struct stat st; if (fstat(o->fd, &st) != 0) { off_t cur = lseek(o->fd, 0, SEEK_CUR); off_t e = lseek(o->fd, 0, SEEK_END); lseek(o->fd, cur, SEEK_SET); st.st_size = e; }
        if (A(1)) wr32(A(1), 0);
        return (uint32_t)st.st_size; });
    reg(K, "SetEndOfFile", 1, [](Cpu& c) { KObj* o = hget(A(0)); if (!o) return 0u; off_t cur = lseek(o->fd, 0, SEEK_CUR); return ftruncate(o->fd, cur) == 0 ? 1u : 0u; });
    reg(K, "FlushFileBuffers", 1, [](Cpu&) { return 1u; });
    reg(K, "DeleteFileA", 1, [](Cpu& c) { return unlink(win2host(GS(A(0))).c_str()) == 0 ? 1u : 0u; });
    reg(K, "MoveFileA", 2, [](Cpu& c) { return rename(win2host(GS(A(0))).c_str(), win2host(GS(A(1))).c_str()) == 0 ? 1u : 0u; });
    reg(K, "CopyFileA", 3, [](Cpu& c) {
        auto b = slurp(win2host(GS(A(0)))); if (b.empty()) return 0u;
        FILE* f = fopen(win2host(GS(A(1))).c_str(), "wb"); if (!f) return 0u;
        fwrite(b.data(), 1, b.size(), f); fclose(f); return 1u; });
    reg(K, "CreateDirectoryA", 2, [](Cpu& c) { return mkdir(win2host(GS(A(0))).c_str(), 0777) == 0 ? 1u : 0u; });
    reg(K, "GetFileAttributesA", 1, [](Cpu& c) {
        struct stat st; std::string p = win2host(GS(A(0)));
        if (stat(p.c_str(), &st) != 0) { g_lasterr = 2; return 0xFFFFFFFFu; }
        return S_ISDIR(st.st_mode) ? 0x10u : 0x20u; });
    reg(K, "OpenFile", 3, [](Cpu& c) -> uint32_t {
        const char* name = GS(A(0)); uint32_t of = A(1), style = A(2);
        std::string full = norm_win(name);
        if (of) { memset(G(of), 0, 136); wr8(of, 136); strncpy(GS(of + 8), full.c_str(), 127); }
        std::string p = win2host(name);
        if (style & 0x200) { unlink(p.c_str()); return 1; }          // OF_DELETE
        if (style & 0x100) return 0;                                  // OF_PARSE
        if (style & 0x4000) { struct stat st; if (stat(p.c_str(), &st) == 0) return 1; if (of) wr16(of + 2, 2); return 0xFFFFFFFFu; } // OF_EXIST
        uint32_t acc = (style & 3) == 0 ? 0x80000000u : (style & 3) == 1 ? 0x40000000u : 0xC0000000u;
        uint32_t disp = (style & 0x1000) ? 2 : 3;
        uint32_t h = file_open(name, acc, disp);
        if (h == 0xFFFFFFFFu && of) wr16(of + 2, 2);
        return h; });
    reg(K, "FindFirstFileA", 2, [](Cpu& c) -> uint32_t {
        std::string pat = GS(A(0)); for (auto& ch : pat) if (ch == '/') ch = '\\';
        size_t k = pat.find_last_of('\\');
        std::string dirw = k == std::string::npos ? "" : pat.substr(0, k), mask = k == std::string::npos ? pat : pat.substr(k + 1);
        std::string dir = win2host(dirw.empty() ? "." : dirw.c_str());
        KObj o; o.kind = 2; o.dir = dir;
        DIR* d = opendir(dir.c_str());
        if (d) { while (dirent* e = readdir(d)) { if (wild(mask.c_str(), e->d_name)) o.found.push_back(e->d_name); } closedir(d); }
        std::sort(o.found.begin(), o.found.end());
        if (o.found.empty()) { g_lasterr = 2; return 0xFFFFFFFFu; }
        fill_find(A(1), dir, o.found[0]); o.fi = 1;
        return hnew(o); });
    reg(K, "FindNextFileA", 2, [](Cpu& c) {
        KObj* o = hget(A(0)); if (!o || o->fi >= o->found.size()) { g_lasterr = 18; return 0u; }
        fill_find(A(1), o->dir, o->found[o->fi++]); return 1u; });
    reg(K, "FindClose", 1, [](Cpu& c) { g_h.erase(A(0)); return 1u; });
    reg(K, "GetFullPathNameA", 4, [](Cpu& c) {
        std::string full = norm_win(GS(A(0)));
        if (A(1) <= full.size()) return (uint32_t)full.size() + 1;
        strcpy(GS(A(2)), full.c_str());
        if (A(3)) { size_t k = full.find_last_of('\\'); wr32(A(3), A(2) + (uint32_t)(k + 1)); }
        return (uint32_t)full.size(); });
    reg(K, "GetCurrentDirectoryA", 2, [](Cpu& c) {
        if (A(0) <= g_cwd_win.size()) return (uint32_t)g_cwd_win.size() + 1;
        strcpy(GS(A(1)), g_cwd_win.c_str()); return (uint32_t)g_cwd_win.size(); });
    reg(K, "SetCurrentDirectoryA", 1, [](Cpu& c) {
        std::string n = norm_win(GS(A(0)));
        while (n.size() > 3 && n.back() == '\\') n.pop_back();
        struct stat st;
        if (stat(win2host(n.c_str()).c_str(), &st) != 0) { rlog("SetCurrentDirectory %s fehlt", n.c_str()); g_lasterr = 3; return 0u; }
        g_cwd_win = n; return 1u; });
    reg(K, "GetDriveTypeA", 1, [](Cpu& c) {
        const char* r = A(0) ? GS(A(0)) : "C:\\";
        return (toupper(r[0]) == 'C') ? 3u : 1u; });
    reg(K, "GetLogicalDrives", 0, [](Cpu&) { return 4u; });
    reg(K, "GetLogicalDriveStringsA", 2, [](Cpu& c) {
        if (A(0) < 5) return 5u; memcpy(G(A(1)), "C:\\\0\0", 5); return 4u; });
    reg(K, "GetDiskFreeSpaceA", 5, [](Cpu& c) { if (A(1)) wr32(A(1), 8); if (A(2)) wr32(A(2), 512); if (A(3)) wr32(A(3), 200000); if (A(4)) wr32(A(4), 400000); return 1u; });
    reg(K, "GetWindowsDirectoryA", 2, [](Cpu& c) { strncpy(GS(A(0)), "C:\\WINDOWS", A(1)); return 10u; });
    reg(K, "GetSystemDirectoryA", 2, [](Cpu& c) { strncpy(GS(A(0)), "C:\\WINDOWS\\SYSTEM", A(1)); return 17u; });

    // ---- Mappings
    reg(K, "CreateFileMappingA", 6, [](Cpu& c) -> uint32_t {
        KObj* f = hget(A(0)); if (!f) return 0;
        KObj o; o.kind = 3; o.path = f->path; o.writable = (A(2) & 0x04) != 0; o.mapFile = A(0);
        return hnew(o); });
    reg(K, "MapViewOfFile", 5, [](Cpu& c) -> uint32_t {
        KObj* m = hget(A(0)); if (!m) return 0;
        auto b = slurp(m->path);
        uint32_t off = A(2), n = A(4);
        if (off > b.size()) return 0;
        if (!n || off + n > b.size()) n = b.size() - off;
        uint32_t va = galloc(n ? n : 1, false);
        if (n) memcpy(G(va), b.data() + off, n);
        g_views[va] = { va, n, off, m->path, (A(1) & 2) != 0 && m->writable };
        return va; });
    reg(K, "UnmapViewOfFile", 1, [](Cpu& c) {
        auto it = g_views.find(A(0)); if (it == g_views.end()) return 0u;
        if (it->second.w) { int fd = open(it->second.path.c_str(), O_WRONLY); if (fd >= 0) { lseek(fd, it->second.off, SEEK_SET); write(fd, G(it->second.va), it->second.len); close(fd); } }
        gfree(it->second.va); g_views.erase(it); return 1u; });

    // ---- INI
    reg(K, "GetPrivateProfileIntA", 4, [](Cpu& c) {
        std::string v; if (ini_get(ini_path(A(3)), GS(A(0)), GS(A(1)), v)) return (uint32_t)atoi(v.c_str());
        return A(2); });
    reg(K, "GetPrivateProfileStringA", 6, [](Cpu& c) -> uint32_t {
        std::string v; const char* def = A(2) ? GS(A(2)) : "";
        if (!A(0) || !A(1) || !ini_get(ini_path(A(5)), GS(A(0)), GS(A(1)), v)) v = def;
        uint32_t n = A(4); if (!n) return 0;
        strncpy(GS(A(3)), v.c_str(), n); GS(A(3))[n - 1] = 0;
        return (uint32_t)strlen(GS(A(3))); });
    reg(K, "WritePrivateProfileStringA", 4, [](Cpu& c) {
        std::string p = ini_path(A(3));
        Ini ini = ini_load(p);
        if (!A(0)) return 1u;
        std::string app = GS(A(0));
        int si = -1;
        for (size_t i = 0; i < ini.sec.size(); i++) if (ieq(ini.sec[i].first) == ieq(app)) si = i;
        if (!A(1)) { if (si >= 0) ini.sec.erase(ini.sec.begin() + si); ini_save(p, ini); return 1u; }
        if (si < 0) { ini.sec.push_back({app, {}}); si = ini.sec.size() - 1; }
        auto& kv = ini.sec[si].second; std::string key = GS(A(1));
        bool done = false;
        for (size_t i = 0; i < kv.size(); i++) if (ieq(kv[i].first) == ieq(key)) {
            if (A(2)) kv[i].second = GS(A(2)); else kv.erase(kv.begin() + i);
            done = true; break; }
        if (!done && A(2)) kv.push_back({key, GS(A(2))});
        ini_save(p, ini); return 1u; });

    // ================= ADVAPI32: Registry
    const char* AD = "ADVAPI32.dll";
    auto open_key = [](uint32_t parent, uint32_t sub, uint32_t outp, bool create) -> uint32_t {
        std::string base = hkey_name(parent); if (base.empty()) return 6;
        std::string k = base; if (sub && *GS(sub)) k += "\\" + std::string(GS(sub));
        std::string ku = ieq(k);
        bool exists = false; std::string real = k;
        for (auto& e : g_reg) if (ieq(e.first) == ku) { exists = true; real = e.first; }
        if (!exists && !create) return 2;
        if (!exists) g_reg[real];
        uint32_t h = g_hknext; g_hknext += 4; g_hkeys[h] = real;
        if (outp) wr32(outp, h);
        return 0;
    };
    reg(AD, "RegOpenKeyExA", 5, [open_key](Cpu& c) { return open_key(A(0), A(1), A(4), false); });
    reg(AD, "RegOpenKeyA", 3, [open_key](Cpu& c) { return open_key(A(0), A(1), A(2), false); });
    reg(AD, "RegCreateKeyExA", 9, [open_key](Cpu& c) { uint32_t r = open_key(A(0), A(1), A(7), true); if (A(8)) wr32(A(8), 2); return r; });
    reg(AD, "RegCreateKeyA", 3, [open_key](Cpu& c) { return open_key(A(0), A(1), A(2), true); });
    reg(AD, "RegCloseKey", 1, [](Cpu& c) { g_hkeys.erase(A(0)); return 0u; });
    reg(AD, "RegQueryValueExA", 6, [](Cpu& c) -> uint32_t {
        std::string k = hkey_name(A(0)); auto it = g_reg.find(k); if (it == g_reg.end()) return 2;
        std::string nm = A(1) ? GS(A(1)) : "";
        auto v = it->second.find(nm); if (v == it->second.end()) return 2;
        const auto& d = v->second; uint32_t type = 0; memcpy(&type, d.data(), 4);
        uint32_t len = d.size() - 4;
        if (A(3)) wr32(A(3), type);
        if (A(5)) { uint32_t cap = rd32(A(5)); wr32(A(5), len);
            if (A(4)) { if (cap < len) return 234; memcpy(G(A(4)), d.data() + 4, len); } }
        return 0; });
    reg(AD, "RegSetValueExA", 6, [](Cpu& c) -> uint32_t {
        std::string k = hkey_name(A(0)); if (k.empty()) return 6;
        std::string nm = A(1) ? GS(A(1)) : "";
        std::vector<uint8_t> d(4 + A(5)); uint32_t t = A(3); memcpy(d.data(), &t, 4);
        if (A(5)) memcpy(d.data() + 4, G(A(4)), A(5));
        g_reg[k][nm] = d; reg_save(); return 0; });
}
