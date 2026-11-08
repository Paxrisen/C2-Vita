// timerdll.dll nativ (statt die x86-DLL zu emulieren): exakte Nachbildung der 6 Exporte
#include "rt.h"
#include "win.h"
static uint32_t g_tid[16], g_tnotified[16];
static uint32_t t_index(uint32_t id) { for (int i = 0; i < 16; i++) if (g_tid[i] == id) return i; return 0; }
void post_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);
void timerdll_callback(uint32_t uid, uint32_t user) {
    uint32_t idx = t_index(uid);
    if (!idx || g_tnotified[idx]) return;
    post_msg(user, 0x52C, idx, now_ms());
    g_tnotified[idx] = 1;
}
void install_timerdll() {
    const char* T = "TIMERDLL.dll";
    reg(T, "TimerCallBack", 5, [](Cpu& c) { timerdll_callback(A(0), A(2)); return 0u; });
    reg(T, "WEP", 1, [](Cpu&) { for (int i = 0; i < 16; i++) g_tid[i] = g_tnotified[i] = 0; return 1u; });
    reg(T, "SetTimerID", 2, [](Cpu& c) { g_tid[A(0) & 15] = A(1); return 0u; });
    reg(T, "GetTimerID", 1, [](Cpu& c) { return g_tid[A(0) & 15]; });
    reg(T, "GetTimerIndex", 1, [](Cpu& c) { return t_index(A(0)); });
    reg(T, "ResetTimerNotified", 1, [](Cpu& c) { g_tnotified[A(0) & 15] = 0; return 0u; });
}
