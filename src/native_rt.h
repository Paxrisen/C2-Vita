/* civ2vita — Laufzeit fuer statisch rekompilierten (nativen) civ2.exe-Code */
#pragma once
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t r[8]; } Ctx;   /* eax ecx edx ebx esp ebp esi edi */
typedef struct { uint32_t va; void (*fn)(Ctx*); uint32_t ret; uint32_t plen; uint32_t pure; } NativeFn;
extern const NativeFn g_native_table[];
extern const unsigned g_native_count;
extern uint8_t* MB;            /* Gast-Speicherbasis */
extern uint32_t TIB;           /* fs:-Basis */
extern volatile int g_vlog;    /* Schreibprotokoll (Pruefmodus) */
extern uint8_t g_nat_on[];
void vlog_write(uint32_t a, uint32_t n);
void nat_fault(uint32_t a);
void CALL_GUEST(Ctx* c, uint32_t target);
void CALL_ANY(Ctx* c, uint32_t target);
void DIVZERO(uint32_t a);
void BADJMP(uint32_t a);
#ifdef __cplusplus
}
#endif

#define NAT_ON(i) (g_vlog || g_nat_on[i])   /* beim Pruefen immer nativ (Schreibprotokoll vollstaendig) */
#define GUEST_LIMIT 0x07400000u
void nat_fault(uint32_t a);
#define CHK(a, n) do { if (__builtin_expect((uint32_t)(a) > GUEST_LIMIT - (n), 0)) nat_fault(a); } while (0)
static inline uint32_t RD32(uint32_t a) { uint32_t v; CHK(a, 4); memcpy(&v, MB + a, 4); return v; }
static inline uint32_t RD16(uint32_t a) { uint16_t v; CHK(a, 2); memcpy(&v, MB + a, 2); return v; }
static inline uint32_t RD8(uint32_t a) { CHK(a, 1); return MB[a]; }
#define WR32(a, v) do { uint32_t _wa = (a), _wv = (uint32_t)(v); CHK(_wa, 4); if (g_vlog) vlog_write(_wa, 4); memcpy(MB + _wa, &_wv, 4); } while (0)
#define WR16(a, v) do { uint32_t _wa = (a); uint16_t _wv = (uint16_t)(v); CHK(_wa, 2); if (g_vlog) vlog_write(_wa, 2); memcpy(MB + _wa, &_wv, 2); } while (0)
#define WR8(a, v)  do { uint32_t _wa = (a); uint8_t _wv = (uint8_t)(v); CHK(_wa, 1); if (g_vlog) vlog_write(_wa, 1); MB[_wa] = _wv; } while (0)

/* ---- x87 (geteilt mit dem Emulator) ---- */
#ifdef __cplusplus
extern "C" {
#endif
double* civ2_fst(int i); void civ2_fpush(double v); void civ2_fpop(void); void civ2_fxch(int i);
uint32_t civ2_fsw(void); uint32_t civ2_fcw(void); void civ2_fsetcw(uint32_t v); void civ2_fcom(double b);
void civ2_fclex(void); void civ2_finit(void);
#ifdef __cplusplus
}
#endif
#include <math.h>
#define FST(i) (*civ2_fst(i))
static inline double RDF64(uint32_t a) { double v; CHK(a, 8); memcpy(&v, MB + a, 8); return v; }
static inline double RDF32(uint32_t a) { float v; CHK(a, 4); memcpy(&v, MB + a, 4); return v; }
static inline int64_t RDI64(uint32_t a) { int64_t v; CHK(a, 8); memcpy(&v, MB + a, 8); return v; }
static inline void WRF64(uint32_t a, double d) { CHK(a, 8); if (g_vlog) vlog_write(a, 4), vlog_write(a + 4, 4); memcpy(MB + a, &d, 8); }
static inline void WRF32(uint32_t a, double d) { float f = (float)d; CHK(a, 4); if (g_vlog) vlog_write(a, 4); memcpy(MB + a, &f, 4); }
static inline void WRI64(uint32_t a, int64_t v) { CHK(a, 8); if (g_vlog) vlog_write(a, 4), vlog_write(a + 4, 4); memcpy(MB + a, &v, 8); }
/* Runden nach Steuerwort (RC Bits 10-11) */
static inline double FRND(double d) {
    if (!isfinite(d)) return d;
    switch ((civ2_fcw() >> 10) & 3) { case 0: return nearbyint(d); case 1: return floor(d); case 2: return ceil(d); default: return trunc(d); }
}
static inline int64_t FIST(double d, int bits) {
    double r = FRND(d);
    double lim = bits == 64 ? 9223372036854775807.0 : bits == 32 ? 2147483647.0 : 32767.0;
    if (!(r >= -lim - 1.0 && r <= lim)) return bits == 64 ? (int64_t)0x8000000000000000ull : bits == 32 ? (int64_t)(int32_t)0x80000000u : (int64_t)(int16_t)0x8000;
    return (int64_t)r;
}

/* ---- Flags (lazy): lk = op<<8 | Breite ---- */
#define FK(op, sz) (((op) << 8) | (sz))
static inline uint32_t f_m(uint32_t sz) { return sz == 32 ? 0xffffffffu : ((1u << sz) - 1); }
static inline uint32_t f_top(uint32_t v, uint32_t sz) { return (v >> (sz - 1)) & 1u; }
static inline uint32_t fCF(uint32_t k, uint32_t r, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t sz = k & 0xff, op = k >> 8, m = f_m(sz ? sz : 32);
    switch (op) {
        case 1: return (r & m) < (a & m);
        case 10: return c ? ((r & m) <= (a & m)) : ((r & m) < (a & m));
        case 2: return (a & m) < (b & m);
        case 11: return c ? ((a & m) <= (b & m)) : ((a & m) < (b & m));
        case 4: case 5: case 12: case 13: return c;
        case 6: return (b & m) != 0;
        case 7: return b <= sz ? ((a >> (sz - b)) & 1u) : 0;
        case 8: return (a >> (b - 1)) & 1u;
        case 9: { int32_t s = (int32_t)(a << (32 - sz)) >> (32 - sz); return b >= sz ? (uint32_t)(s < 0) : (uint32_t)((s >> (b - 1)) & 1); }
    }
    return 0;
}
static inline uint32_t fOF(uint32_t k, uint32_t r, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t sz = k & 0xff, op = k >> 8;
    if (!sz) return 0;
    switch (op) {
        case 1: case 10: return f_top((a ^ r) & (b ^ r), sz);
        case 2: case 11: return f_top((a ^ b) & (a ^ r), sz);
        case 4: return (r & f_m(sz)) == (1u << (sz - 1));
        case 5: return (r & f_m(sz)) == ((1u << (sz - 1)) - 1);
        case 6: return (b & f_m(sz)) == (1u << (sz - 1));
        case 7: return b == 1 ? (f_top(r, sz) ^ fCF(k, r, a, b, c)) : 0;
        case 8: return b == 1 ? f_top(a, sz) : 0;
        case 12: return c;
    }
    return 0;
}
static inline uint32_t fZF(uint32_t k, uint32_t r) { uint32_t sz = k & 0xff; return (r & f_m(sz ? sz : 32)) == 0; }
static inline uint32_t fSF(uint32_t k, uint32_t r) { uint32_t sz = k & 0xff; return f_top(r, sz ? sz : 32); }
static inline uint32_t fPF(uint32_t r) { return !__builtin_parity(r & 0xffu); }
#define CF_NOW(k, r, a, b, c) fCF(k, r, a, b, c)
static inline int CC(int cc, uint32_t k, uint32_t r, uint32_t a, uint32_t b, uint32_t c) {
    switch (cc) {
        case 0: return fOF(k, r, a, b, c);
        case 1: return !fOF(k, r, a, b, c);
        case 2: return fCF(k, r, a, b, c);
        case 3: return !fCF(k, r, a, b, c);
        case 4: return fZF(k, r);
        case 5: return !fZF(k, r);
        case 6: return fCF(k, r, a, b, c) || fZF(k, r);
        case 7: return !fCF(k, r, a, b, c) && !fZF(k, r);
        case 8: return fSF(k, r);
        case 9: return !fSF(k, r);
        case 10: return fPF(r);
        case 11: return !fPF(r);
        case 12: return fSF(k, r) != fOF(k, r, a, b, c);
        case 13: return fSF(k, r) == fOF(k, r, a, b, c);
        case 14: return fZF(k, r) || (fSF(k, r) != fOF(k, r, a, b, c));
        case 15: return !fZF(k, r) && (fSF(k, r) == fOF(k, r, a, b, c));
    }
    return 0;
}
