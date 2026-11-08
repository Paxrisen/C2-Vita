// civ2vita — Start: Arena/Dynarec einrichten, civ2.exe laden, Shims verdrahten, Einsprung ausfuehren
#include "win.h"
#include "runtime/guest_thread.h"
#include <cstdlib>
#include <sys/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
extern "C" uintptr_t civ2_membase;
#include <psp2/ctrl.h>
#include <psp2/apputil.h>
#include <psp2/sysmodule.h>

extern std::vector<uint8_t> slurp(const std::string& p);
extern uint32_t g_tib;
void user32_late_init();
extern "C" {
unsigned int _newlib_heap_size_user = 56 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
}

static void wait_exit(const char* l1, const char* l2) {
    scr_fill({0, 0, SCR_W, SCR_H}, 0x400000);
    scr_text({0, 200, SCR_W, 230}, l1, 0xFFFFFF, 1, true);
    scr_text({0, 240, SCR_W, 270}, l2, 0xFFFFFF, 1);
    scr_text({0, 300, SCR_W, 330}, TR("START druecken zum Beenden", "Press START to exit"), 0xC0C0C0, 1);
    plat_present(true);
    for (;;) { SceCtrlData p; sceCtrlPeekBufferPositive(0, &p, 1); if (p.buttons & SCE_CTRL_START) break; sceKernelDelayThread(20000); plat_present(true); }
    sceKernelExitProcess(0);
}
void fatal_screen(const char* msg) { wait_exit(TR("Civilization II: Fehler", "Civilization II: Error"), msg); }

static void status(const char* s) { scr_fill({0, 250, SCR_W, 290}, 0); scr_text({0, 250, SCR_W, 290}, s, 0xFFFFFF, 1); plat_present(true); rlog("%s", s); }

int main(int, char**) {
    plat_init();
    mkdir(g_root.c_str(), 0777);
    rlog("C2-Vita 1.0 — Start");
    fonts_init();
    status(TR("C2-Vita wird geladen ...", "Loading C2-Vita ..."));
    { extern const unsigned g_native_count;
      if (g_native_count == 0) {   // oeffentliche Version: Hinweis auf den Emulationsmodus
          Rect r1{0, 300, SCR_W, 330}, r2{0, 330, SCR_W, 360}, r3{0, 360, SCR_W, 390};
          scr_text(r1, TR("Emulationsmodus: Diese Version enthaelt keinen nativen Code.", "Emulation mode: this version contains no native code."), 0xFFD060, 1);
          scr_text(r2, TR("Das Spiel laeuft vollstaendig, aber ueber den x86-Emulator.", "The game is fully playable, but runs through the x86 emulator."), 0xC0C0C0, 1);
          scr_text(r3, TR("Fuer die native Version: C2-Vita-Patcher auf dem PC verwenden.", "For the native version: use the C2-Vita patcher on your PC."), 0xC0C0C0, 1);
          plat_present(true); rlog("Emulationsmodus (kein nativer Code enthalten)");
          sceKernelDelayThread(4000000);
      } }
    auto exe = slurp(g_root + "/civ2.exe");
    if (exe.empty()) wait_exit(TR("civ2.exe nicht gefunden", "civ2.exe not found"), TR("Bitte den kompletten Spielordner nach ux0:data/civ2/ kopieren.", "Please copy the complete game folder to ux0:data/civ2/."));
    rlog("civ2.exe: %u Bytes", (unsigned)exe.size());

    // Speicherlayout des Gastes (siehe rt.h / kernel32.cpp)
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), 0x10000);   // Spiel (Hauptthread) auf Kern 0, Ton auf Kern 1, Musik/Waechter auf Kern 2
    setenv("WX86_ARENA", "0x08400000", 1);   // 116 MiB Gast-Adressraum + 16 MiB Ausrichtungsreserve
    setenv("WX86_MODBASE", "0x05000000", 1);
    setenv("WX86_STACKBASE", "0x06F00000", 1);
    setenv("WX86_TRAPBASE", "0x07200000", 1);
    setenv("WX86_JITPOOL_SEGS", "4", 0);    // bis 64 MiB uebersetzter Code (kubridge RWX)
    Cpu* cpu = d2rt::make_cpu_box86();
    if (!cpu) wait_exit(TR("Dynarec konnte nicht starten", "Dynarec failed to start"), "");
    g_cpu = cpu;
    { extern int box86_dynarec_callret; box86_dynarec_callret = 1; }
    if (!cpu->map(0x00010000, 0x003F0000, nullptr, d2rt::P_RW)) rlog("Niedrigbereich-Map fehlgeschlagen");
    void* hp = cpu->hostptr(0x00010000, 4);
    if (!hp) wait_exit(TR("Arena nicht verfuegbar", "Arena not available"), "hostptr == NULL");
    g_membase = (uintptr_t)hp - 0x00010000; civ2_membase = g_membase;
    rlog("membase = %p", (void*)g_membase);
    mem_init();

    static Bridge br(cpu);
    g_br = &br;
    br.set_module_limit(0x06E00000);
    std::string err;
    if (!br.add_module("civ2.exe", exe, err)) wait_exit(TR("civ2.exe konnte nicht geladen werden", "civ2.exe could not be loaded"), err.c_str());

    install_kernel32();
    { extern void install_timerdll(); install_timerdll(); }
    install_gdi32();
    install_user32();
    install_winmm();
    install_misc();
    br.set_default_shim([](Cpu& c, const std::string& tag) -> uint32_t {
        rlog("NICHT IMPLEMENTIERT: %s (Rueckkehr %08x)", tag.c_str(), c.trap_retaddr());
        return 0; });
    if (!br.commit(err)) wait_exit(TR("commit fehlgeschlagen", "commit failed"), err.c_str());
    user32_late_init();
    if (!br.link(err)) wait_exit(TR("link fehlgeschlagen", "link failed"), err.c_str());
    // fehlende Importe protokollieren
    for (const auto& imp : br.module("civ2.exe")->imports())
        if (!imp.name.empty() && !shim_known(imp.dll, imp.name)) rlog("Import ohne Shim: %s!%s", imp.dll.c_str(), imp.name.c_str());
        else if (imp.name.empty()) rlog("Import per Ordinal: %s!#%u", imp.dll.c_str(), imp.ordinal);
    rlog("unaufgeloest: %u", br.unresolved_count());

    // TIB/PEB
    g_tib = 0x00100000;
    memset(G(g_tib), 0, 0x2000);
    wr32(g_tib + 0x00, 0xFFFFFFFFu);
    wr32(g_tib + 0x04, 0x06F00000 + 0x00200000);
    wr32(g_tib + 0x08, 0x06F00000);
    wr32(g_tib + 0x18, g_tib);
    wr32(g_tib + 0x20, 0xAE4); wr32(g_tib + 0x24, 0xAF0);
    wr32(g_tib + 0x30, g_tib + 0x1000);
    wr32(g_tib + 0x1000 + 8, g_exe_base);
    cpu->set_fs_base(g_tib);
    extern void native_init(); native_init();

    scr_fill({0, 0, SCR_W, SCR_H}, 0); plat_present(true);
    extern void watchdog_start(); watchdog_start();
    uint32_t entry = br.module("civ2.exe")->entry_va();
    rlog("Einsprung %08x", entry);
    cpu->set_reg(d2rt::R_ESP, 0x06F00000 + 0x00200000 - 0x40);
    cpu->set_reg(d2rt::R_EBP, 0);
    uint32_t r = gcall(entry, {});
    rlog("Programmende (%u)", r);
    rlog_flush();
    sceKernelExitProcess(0);
    return 0;
}
// Fortschrittsprotokoll der winx86-Laufzeit (Arena, JIT-Pool, kubridge)
extern "C" const char* const wx86_vita_progress_path = "ux0:data/civ2/winx86.log";
