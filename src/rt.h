// civ2vita — gemeinsame Laufzeit-Hilfen (Win32-Schicht fuer civ2.exe auf PS Vita)
#pragma once
#include "runtime/bridge.h"
#include "runtime/cpu.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
#include <initializer_list>

using d2rt::Cpu;
using d2rt::Bridge;

extern Cpu*    g_cpu;
extern Bridge* g_br;
extern uintptr_t g_membase;          // host = guest + g_membase
extern std::string g_root;           // "ux0:data/civ2"
extern uint32_t g_exe_base;

// ---- Logging ----
void rlog(const char* fmt, ...) __attribute__((format(printf,1,2)));
void rlog_flush();
void fatal(const char* fmt, ...) __attribute__((format(printf,1,2)));

// ---- Guest memory ----
static inline void* G(uint32_t va) { return (void*)((uintptr_t)va + g_membase); }
static inline char* GS(uint32_t va) { return va ? (char*)G(va) : nullptr; }
static inline uint32_t GV(const void* host) { return (uint32_t)((uintptr_t)host - g_membase); }
static inline uint32_t rd32(uint32_t va){ uint32_t v; memcpy(&v,G(va),4); return v; }
static inline uint16_t rd16(uint32_t va){ uint16_t v; memcpy(&v,G(va),2); return v; }
static inline void wr32(uint32_t va,uint32_t v){ memcpy(G(va),&v,4); }
static inline void wr16(uint32_t va,uint16_t v){ memcpy(G(va),&v,2); }
static inline void wr8(uint32_t va,uint8_t v){ *(uint8_t*)G(va)=v; }
static inline bool is_intres(uint32_t p){ return p < 0x10000; }

uint32_t galloc(uint32_t n, bool zero = true);   // guest heap
void     gfree(uint32_t va);
uint32_t gsize(uint32_t va);
uint32_t grealloc(uint32_t va, uint32_t n);
uint32_t gstrdup(const char* s);
void     mem_init();

// ---- Shim registration ----
using ShimFn = std::function<uint32_t(Cpu&)>;
void reg(const char* dll, const char* name, uint32_t argc, ShimFn fn, bool cdecl_ = false);
void reg_ord(const char* dll, uint32_t ord, uint32_t argc, ShimFn fn);
bool shim_known(const std::string& dll, const std::string& name);
#define A(i) c.arg(i)

// ---- Guest calls (nested, re-entrant) ----
uint32_t gcall(uint32_t proc, std::initializer_list<uint32_t> args);
uint32_t gcallv(uint32_t proc, const uint32_t* args, int n);

// ---- Last error ----
extern uint32_t g_lasterr;

// ---- Paths ----
std::string win2host(const char* winpath);     // "C:\\CIV2\\X.SAV" -> "ux0:data/civ2/X.SAV"
extern std::string g_cwd_win;                  // "C:\\CIV2"

// ---- Install functions ----
void install_kernel32();
void install_advapi32();
void install_user32();
void install_gdi32();
void install_winmm();
void install_misc();

// ---- Platform (vita.cpp) ----
extern int g_lang;   // 0 Deutsch, 1 English
#define TR(de, en) (g_lang == 1 ? (en) : (de))
void plat_init();
void plat_present(bool force = false);   // composes screen + cursor to display
void plat_poll();                        // reads input -> enqueues events
uint64_t now_us();
uint32_t now_ms();
void plat_sleep_ms(uint32_t ms);
extern uint32_t* g_screen;               // 32bpp ABGR(Vita: 0xAABBGGRR) back buffer
extern int SCR_W, SCR_H;
extern volatile bool g_screen_dirty;

struct InputEv { int type; int x, y; int key; int down; uint32_t ch; };
enum { EV_MOUSEMOVE=1, EV_LDOWN, EV_LUP, EV_RDOWN, EV_RUP, EV_KEY, EV_CHAR };
bool plat_next_event(InputEv& e);
extern int g_mouse_x, g_mouse_y;
extern bool g_lbutton, g_rbutton;
extern uint8_t g_keystate[256];          // high bit = down
int  plat_ime(const char* title, const char* initial, char* out, int maxlen); // 1=ok
void plat_draw_cursor(bool on);
struct CursorImg { int w = 0, h = 0, hx = 0, hy = 0; std::vector<uint32_t> px; };   // px: 0 = durchsichtig, 1 = invertieren, sonst Farbe
void plat_set_cursor(const CursorImg* c);

// ---- Shared helpers used by several modules ----
void timers_poll();                     // fires due timeSetEvent callbacks
void pump_internal();                   // poll + timers + present (used by Sleep etc.)
