// Differenztest: Original-x86 (32-Bit-Kompatibilitaetsmodus) gegen nativ uebersetzten C-Code
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <ucontext.h>
#include "native_rt.h"

uint8_t* MB = 0; uint32_t TIB = 0x00100000; volatile int g_vlog = 0; uint8_t g_nat_on[8192];
void vlog_write(uint32_t a, uint32_t n) { (void)a; (void)n; }
extern void call_guest(uint32_t* regs, uint32_t target);
extern char stub_lo_start[], stub_lo_end[], sig_entry[], landing_host[];
extern uint64_t h_rsp, h_fs, h_land;

#define LO 0x00010000u
#define HI 0x05000000u
#define SCR0 0x01000000u
#define SCR1 0x01800000u
#define STK0 0x04E00000u
#define ESP0 0x04EF0000u
static uint8_t* pristine; static uint8_t* gres;
static sigjmp_buf jb; static volatile int in_test = 0, why = 0;   // why: 1=Fault 2=Zeit 3=Div0 4=badjmp
static volatile uint32_t fault_pc;

void sig_handler_c(int sig, siginfo_t* si, void* uc) {
    (void)uc;
    if (!in_test) { _exit(99); }
    why = sig == SIGALRM ? 2 : sig == SIGFPE ? 3 : 1;
    fault_pc = (uint32_t)(uintptr_t)si->si_addr;
    if (getenv("DBG")) { ucontext_t* u = uc; fprintf(stderr, "sig %d addr %p rip %llx cs %llx eax %llx esp %llx\n", sig, si->si_addr, (unsigned long long)u->uc_mcontext.gregs[REG_RIP], (unsigned long long)u->uc_mcontext.gregs[REG_CSGSFS] & 0xffff, (unsigned long long)u->uc_mcontext.gregs[REG_RAX], (unsigned long long)u->uc_mcontext.gregs[REG_RSP]); }
    siglongjmp(jb, 1);
}
void nat_fault(uint32_t a) { why = 1; fault_pc = a; siglongjmp(jb, 1); }
void DIVZERO(uint32_t a) { why = 3; siglongjmp(jb, 1); }
void BADJMP(uint32_t a) { why = 4; fault_pc = a; siglongjmp(jb, 1); }

static int idx_of(uint32_t va) {
    int lo = 0, hi = (int)g_native_count - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; uint32_t v = g_native_table[m].va; if (v == va) return m; if (v < va) lo = m + 1; else hi = m - 1; }
    return -1;
}
static uint32_t sentinel_ret = 0x00200100;
void CALL_GUEST(Ctx* c, uint32_t target) {
    uint32_t esp = c->r[4];
    memcpy(MB + esp, &sentinel_ret, 4);
    call_guest(c->r, target);
}
void CALL_ANY(Ctx* c, uint32_t target) {
    int i = idx_of(target);
    if (i >= 0) { g_native_table[i].fn(c); c->r[4] += 4 + g_native_table[i].ret; return; }
    CALL_GUEST(c, target);
}

static uint64_t rs;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)rs; }
static uint32_t rval() {
    uint32_t r = rnd() % 8;
    if (r < 5) return SCR0 + (rnd() % (SCR1 - SCR0 - 0x1000)) & ~3u;
    if (r < 7) return rnd() % 64;
    return 0x624000 + (rnd() % 0xc2000) & ~3u;   // in den Datenbereich des Programms
}
static void make_state(uint64_t seed, uint32_t* regs) {
    memcpy(MB + LO, pristine, 0x200000 - LO);
    memcpy(MB + 0x201000, pristine + (0x201000 - LO), 0x210000 - 0x201000);
    memcpy(MB + 0x212000, pristine + (0x212000 - LO), 0x401000 - 0x212000);
    memcpy(MB + 0x61c000, pristine + (0x61c000 - LO), HI - 0x61c000);
    rs = seed * 2654435761u + 1;
    for (uint32_t a = SCR0; a < SCR1; a += 4) { uint32_t v = rval(); memcpy(MB + a, &v, 4); }
    for (uint32_t a = ESP0 - 0x400; a < ESP0 + 0x400; a += 4) { uint32_t v = rval(); memcpy(MB + a, &v, 4); }
    memcpy(MB + ESP0, &sentinel_ret, 4);
    for (int i = 0; i < 8; i++) regs[i] = rval();
    regs[1] = SCR0 + (rnd() % (SCR1 - SCR0 - 0x1000)) & ~3u;   // this-Zeiger
    regs[4] = ESP0; regs[5] = rval();
}
static void itimer(int ms) { struct itimerval t = {{0, 0}, {0, ms * 1000}}; setitimer(ITIMER_REAL, &t, 0); }

int main(int argc, char** argv) {
    int seeds = argc > 1 ? atoi(argv[1]) : 3;
    int only = argc > 2 ? (int)strtoul(argv[2], 0, 16) : 0;
    if (mmap((void*)(uintptr_t)LO, HI - LO, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void*)(uintptr_t)LO) { perror("mmap"); return 1; }
    FILE* f = fopen("image.bin", "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); fread(MB + 0x400000, 1, n, f); fclose(f);
    memcpy(MB + 0x200100, stub_lo_start, stub_lo_end - stub_lo_start);
    uint64_t fsb; __asm__ volatile("rdfsbase %0" : "=r"(fsb)); h_fs = fsb;
    { uint8_t* p = MB + 0x200300; uint64_t a;   // 64-Bit-Landung: fs zuruecksetzen, Host-Stapel, zurueck in call_guest
      *p++ = 0x31; *p++ = 0xC0;                                  // xor eax,eax
      *p++ = 0x8E; *p++ = 0xE0;                                  // mov fs,eax
      *p++ = 0x48; *p++ = 0xB8; a = (uint64_t)&h_fs; memcpy(p, &a, 8); p += 8;   // movabs rax,&h_fs
      *p++ = 0x48; *p++ = 0x8B; *p++ = 0x00;                     // mov rax,[rax]
      *p++ = 0xF3; *p++ = 0x48; *p++ = 0x0F; *p++ = 0xAE; *p++ = 0xD0;   // wrfsbase rax
      *p++ = 0x48; *p++ = 0xB8; a = (uint64_t)&h_rsp; memcpy(p, &a, 8); p += 8;
      *p++ = 0x48; *p++ = 0x8B; *p++ = 0x20;                     // mov rsp,[rax]
      *p++ = 0x48; *p++ = 0xB8; a = (uint64_t)landing_host; memcpy(p, &a, 8); p += 8;
      *p++ = 0xFF; *p++ = 0xE0; }                                // jmp rax
    // TIB
    uint32_t t0 = 0xFFFFFFFF, tself = TIB, sb = ESP0 + 0x10000, sl = STK0;
    memcpy(MB + TIB, &t0, 4); memcpy(MB + TIB + 4, &sb, 4); memcpy(MB + TIB + 8, &sl, 4); memcpy(MB + TIB + 0x18, &tself, 4);
    // Import-Stubs
    f = fopen("imports.txt", "r"); char dll[128], fn[256]; unsigned iat; int ac, cd, k = 0;
    while (fscanf(f, "%x %127s %255s %d %d", &iat, dll, fn, &ac, &cd) == 5) {
        uint32_t st = 0x00210000 + k * 16, rv = 0x01000000 + 0x100 * (k + 1); uint8_t* p = MB + st;
        p[0] = 0xB8; memcpy(p + 1, &rv, 4);
        if (ac < 0) { p[5] = 0x0F; p[6] = 0x0B; }                  // ud2: unbekannt
        else if (cd || ac == 0) p[5] = 0xC3;
        else { p[5] = 0xC2; uint16_t r = ac * 4; memcpy(p + 6, &r, 2); }
        memcpy(MB + iat, &st, 4); k++;
    }
    fclose(f);
    mprotect((void*)(uintptr_t)LO, HI - LO, PROT_READ | PROT_WRITE);       // Daten nicht ausfuehrbar
    mprotect((void*)0x200000, 0x1000, PROT_READ | PROT_EXEC);   // Stub-Seite vor Gastschreibzugriffen schuetzen
    mprotect((void*)0x210000, 0x2000, PROT_READ | PROT_EXEC);   // Import-Stubs
    mprotect((void*)0x401000, 0x61c000 - 0x401000, PROT_READ | PROT_EXEC);   // Programmcode
    pristine = malloc(HI - LO); memcpy(pristine, MB + LO, HI - LO);
    gres = malloc(HI - LO);
    stack_t ss = { malloc(1 << 20), 0, 1 << 20 }; sigaltstack(&ss, 0);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = (void*)sig_entry; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGFPE, &sa, 0); sigaction(SIGALRM, &sa, 0); sigaction(SIGTRAP, &sa, 0);
    memset(g_nat_on, 1, sizeof g_nat_on);
    int nok = 0, nbad = 0, ninc = 0;
    unsigned fstart = getenv("START") ? atoi(getenv("START")) : 0;
    for (unsigned fi = fstart; fi < g_native_count; fi++) {
        if (getenv("TRACE")) { fprintf(stderr, "F %08x\n", g_native_table[fi].va); }
        const NativeFn* nf = &g_native_table[fi];
        if (only && nf->va != (uint32_t)only) continue;
        if (fi % 50 == 0) { fprintf(stderr, "%u/%u ok=%d bad=%d unklar=%d\n", fi, g_native_count, nok, nbad, ninc); }
        int fok = 0, fbad = 0; char msg[256] = "";
        for (int s = 0; s < seeds && !fbad; s++) {
            uint32_t r0[8], rg[8]; Ctx c;
            // 1) Original
            make_state(nf->va * 31 + s, r0); memcpy(rg, r0, sizeof rg);
            why = 0; in_test = 1;
            if (sigsetjmp(jb, 1) == 0) { itimer(300); call_guest(rg, nf->va); itimer(0); }
            else { itimer(0); in_test = 0; if (getenv("DBG")) printf("orig abbruch %08x grund %d pc %08x\n", nf->va, why, fault_pc); continue; }
            in_test = 0;
            memcpy(gres, MB + LO, HI - LO);
            // 2) nativ
            make_state(nf->va * 31 + s, r0); memcpy(c.r, r0, sizeof c.r);
            why = 0; in_test = 1;
            if (sigsetjmp(jb, 1) == 0) { itimer(300); nf->fn(&c); itimer(0); c.r[4] += 4 + nf->ret; }
            else { itimer(0); in_test = 0; fbad = 1; snprintf(msg, sizeof msg, "nativ bricht ab (Grund %d bei %08x), Original nicht", why, fault_pc); break; }
            in_test = 0;
            static const char* rn[8] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
            for (int r = 0; r < 8 && !fbad; r++) if (c.r[r] != rg[r] && r != 1 && r != 2) { fbad = 1; snprintf(msg, sizeof msg, "%s nativ=%08x original=%08x", rn[r], c.r[r], rg[r]); }
            if (!fbad) {
                uint8_t* a = MB + LO; size_t len = HI - LO;
                size_t skip0 = STK0 - LO, skip1 = ESP0 + 4 - LO;   // toter Stapel
                for (size_t off = 0; off < len; off += 4096) {
                    if (off + 4096 > skip0 && off < skip1) { // Seite im Stapelbereich: genau pruefen
                        for (size_t b = off; b < off + 4096; b++) if ((b < skip0 || b >= skip1) && a[b] != gres[b]) { fbad = 1; snprintf(msg, sizeof msg, "Speicher %08x nativ=%02x original=%02x", (unsigned)(b + LO), a[b], gres[b]); break; }
                    } else if (memcmp(a + off, gres + off, 4096)) {
                        for (size_t b = off; b < off + 4096; b++) if (a[b] != gres[b]) { if (b + LO >= 0x202000 && b + LO < 0x202020) continue; fbad = 1; snprintf(msg, sizeof msg, "Speicher %08x nativ=%02x original=%02x", (unsigned)(b + LO), a[b], gres[b]); break; }
                    }
                    if (fbad) break;
                }
            }
            if (!fbad) fok++;
        }
        if (fbad) { nbad++; printf("BAD %08x pure=%u ret=%u: %s\n", nf->va, nf->pure, nf->ret, msg); fflush(stdout); }
        else if (fok) nok++; else { ninc++; if (getenv("UNK")) printf("UNK %08x\n", nf->va); }
    }
    printf("FERTIG ok=%d schlecht=%d unklar=%d\n", nok, nbad, ninc);
    return 0;
}
