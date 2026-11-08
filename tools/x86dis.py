# x86dis.py - kleiner 32-Bit-x86-Disassembler, Ausgabe im Format von
# "objdump -d -M intel --no-show-raw-insn" (GNU binutils 2.42).
# Damit braucht der Patcher kein objdump fuer x86.
import struct

R32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
R16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
R8 = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh']
SEG = ['es', 'cs', 'ss', 'ds', 'fs', 'gs', '?', '?']
CC = ['o', 'no', 'b', 'ae', 'e', 'ne', 'be', 'a', 's', 'ns', 'p', 'np', 'l', 'ge', 'le', 'g']
ALU = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp']
SHF = ['rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'shl', 'sar']
SZ = {1: 'BYTE PTR ', 2: 'WORD PTR ', 4: 'DWORD PTR ', 6: 'FWORD PTR ', 8: 'QWORD PTR ', 10: 'TBYTE PTR ', 0: ''}


class Bad(Exception):
    pass


def hx(v):
    return '0x%x' % v


class Dec:
    def __init__(self, buf, base):
        self.b = buf
        self.base = base

    def u8(self):
        if self.p >= len(self.b): raise Bad()
        v = self.b[self.p]; self.p += 1; return v

    def s8(self):
        v = self.u8(); return v - 256 if v > 127 else v

    def u16(self):
        if self.p + 2 > len(self.b): raise Bad()
        v = struct.unpack_from('<H', self.b, self.p)[0]; self.p += 2; return v

    def u32(self):
        if self.p + 4 > len(self.b): raise Bad()
        v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v

    def s32(self):
        v = self.u32(); return v - (1 << 32) if v & 0x80000000 else v

    # ---------------------------------------------------------------- ModRM
    def modrm(self):
        m = self.u8()
        self.mod, self.reg, self.rm = m >> 6, (m >> 3) & 7, m & 7
        self.mem = None
        if self.mod == 3: return
        seg = self.seg
        if self.a16:
            base = ['bx+si', 'bx+di', 'bp+si', 'bp+di', 'si', 'di', 'bp', 'bx'][self.rm]
            if self.mod == 0 and self.rm == 6:
                d = self.u16(); self.mem = '%s:%s' % (seg or 'ds', hx(d)); return
            disp = self.s8() if self.mod == 1 else (self.u16() if self.mod == 2 else None)
            if self.mod == 2 and disp >= 0x8000: disp -= 0x10000
            s = '[' + base + (('+' + hx(disp)) if disp is not None and disp >= 0 else ('-' + hx(-disp)) if disp is not None else '') + ']'
            self.mem = (seg + ':' if seg else '') + s
            return
        base = idx = None; scale = 1; disp = None; absolute = False
        if self.rm == 4:
            sib = self.u8(); ss, ix, bs = sib >> 6, (sib >> 3) & 7, sib & 7
            scale = 1 << ss
            if ix != 4: idx = R32[ix]
            elif ss != 0 or bs != 4: idx = 'eiz'
            if bs == 5 and self.mod == 0:
                disp = self.s32()
                if idx is None: absolute = True
            else:
                base = R32[bs]
            self.sib = True
        elif self.rm == 5 and self.mod == 0:
            disp = self.s32(); absolute = True
        else:
            base = R32[self.rm]
        if self.mod == 1: disp = self.s8()
        elif self.mod == 2: disp = self.s32()
        if absolute:
            self.mem = '%s:%s' % (seg or 'ds', hx(disp & 0xffffffff)); return
        parts = ''
        if base: parts = base
        if idx: parts += ('+' if parts else '') + '%s*%d' % (idx, scale)
        if disp is not None:
            if not base and idx:
                parts += ('+' + hx(disp)) if disp >= 0 else ('-' + hx(-disp))
            else:
                parts += ('+' + hx(disp)) if disp >= 0 else ('-' + hx(-disp))
        self.mem = (seg + ':' if seg else '') + '[' + parts + ']'

    def E(self, size, nosize=False):
        if self.mod == 3:
            return {1: R8, 2: R16, 4: R32}[size][self.rm] if size in (1, 2, 4) else R32[self.rm]
        return ('' if nosize else SZ[size]) + self.mem

    def G(self, size):
        return {1: R8, 2: R16, 4: R32}[size][self.reg]

    def imm(self, size):
        return self.u8() if size == 1 else self.u16() if size == 2 else self.u32()

    def imm_sx8(self, size):
        v = self.s8() & ((1 << (8 * size)) - 1); return v

    # ---------------------------------------------------------------- Dekodierung
    def decode(self, pos):
        self.p = pos; self.seg = None; self.o16 = False; self.a16 = False; self.rep = None; self.lock = False
        self.sib = False
        prefixes = []
        while True:
            if self.p >= len(self.b): raise Bad()
            c = self.b[self.p]
            if c in (0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65):
                if self.seg is not None: break
                self.seg = {0x26: 'es', 0x2e: 'cs', 0x36: 'ss', 0x3e: 'ds', 0x64: 'fs', 0x65: 'gs'}[c]; prefixes.append(('seg', c)); self.p += 1
            elif c == 0x66:
                if self.o16: break
                self.o16 = True; prefixes.append(('o16', c)); self.p += 1
            elif c == 0x67:
                if self.a16: break
                self.a16 = True; prefixes.append(('a16', c)); self.p += 1
            elif c in (0xf2, 0xf3):
                if self.rep is not None: break
                self.rep = c; prefixes.append(('rep', c)); self.p += 1
            elif c == 0xf0:
                if self.lock: break
                self.lock = True; prefixes.append(('lock', c)); self.p += 1
            else:
                break
        self.used_seg = False; self.used_o16 = False; self.used_a16 = False; self.used_rep = False; self.mem = None
        start = self.p
        mn, ops = self.op()
        return mn, ops

    def osz(self):
        self.used_o16 = True
        return 2 if self.o16 else 4

    def memseg(self):
        if self.seg and self.mem is not None: self.used_seg = True

    def op(self):
        c = self.u8()
        v = None
        if c < 0x40 and (c & 7) < 6:
            mnem = ALU[c >> 3]; k = c & 7
            if k == 0: self.modrm(); self.memseg(); return mnem, [self.E(1), self.G(1)]
            if k == 1: s = self.osz(); self.modrm(); self.memseg(); return mnem, [self.E(s), self.G(s)]
            if k == 2: self.modrm(); self.memseg(); return mnem, [self.G(1), self.E(1)]
            if k == 3: s = self.osz(); self.modrm(); self.memseg(); return mnem, [self.G(s), self.E(s)]
            if k == 4: return mnem, ['al', hx(self.u8())]
            s = self.osz(); return mnem, [R16[0] if s == 2 else 'eax', hx(self.imm(s))]
        simple = {0x06: ('push', ['es']), 0x07: ('pop', ['es']), 0x0e: ('push', ['cs']), 0x16: ('push', ['ss']), 0x17: ('pop', ['ss']),
                  0x1e: ('push', ['ds']), 0x1f: ('pop', ['ds']), 0x27: ('daa', []), 0x2f: ('das', []), 0x37: ('aaa', []), 0x3f: ('aas', []),
                  0x90: ('nop', []), 0x9b: ('fwait', []), 0x9e: ('sahf', []), 0x9f: ('lahf', []), 0xc9: ('leave', []), 0xcc: ('int3', []),
                  0xce: ('into', []), 0xf1: ('int1', []), 0xf4: ('hlt', []), 0xf5: ('cmc', []), 0xf8: ('clc', []), 0xf9: ('stc', []),
                  0xfa: ('cli', []), 0xfb: ('sti', []), 0xfc: ('cld', []), 0xfd: ('std', []), 0xd6: ('(bad)', [])}
        if c in simple:
            mn, o = simple[c]
            if c == 0x90 and self.rep == 0xf3: self.used_rep = True; return 'pause', []
            if c == 0xc9 and self.o16: self.used_o16 = True; return 'leavew', []
            return mn, list(o)
        if 0x40 <= c < 0x60:
            s = self.osz(); r = (R16 if s == 2 else R32)[c & 7]
            return ['inc', 'dec', 'push', 'pop'][(c - 0x40) >> 3], [r]
        if c == 0x60: s = self.osz(); return ('pushaw' if s == 2 else 'pusha'), []
        if c == 0x61: s = self.osz(); return ('popaw' if s == 2 else 'popa'), []
        if c == 0x62:
            s = self.osz(); self.modrm()
            if self.mod == 3: raise Bad()
            self.memseg(); return 'bound', [self.G(s), SZ[8 if s == 4 else 4] + self.mem]
        if c == 0x63: self.modrm(); self.memseg(); return 'arpl', [self.E(2), self.G(2) if self.o16 else R32[self.reg]]
        if c == 0x68: s = self.osz(); return ('pushw' if s == 2 else 'push'), [hx(self.imm(s))]
        if c == 0x6a: s = self.osz(); return ('pushw' if s == 2 else 'push'), [hx(self.imm_sx8(s))]
        if c in (0x69, 0x6b):
            s = self.osz(); self.modrm(); self.memseg()
            i = self.imm(s) if c == 0x69 else self.imm_sx8(s)
            return 'imul', [self.G(s), self.E(s), hx(i)]
        if c in (0x6c, 0x6d):
            s = 1 if c == 0x6c else self.osz(); return self.repname('ins'), [SZ[s] + 'es:[edi]', 'dx']
        if c in (0x6e, 0x6f):
            s = 1 if c == 0x6e else self.osz(); self.used_seg = bool(self.seg); return self.repname('outs'), ['dx', SZ[s] + (self.seg or 'ds') + ':[esi]']
        if 0x70 <= c < 0x80:
            d = self.s8(); return 'j' + CC[c - 0x70], [hx((self.base + self.p + d) & 0xffffffff)]
        if c in (0x80, 0x81, 0x82, 0x83):
            s = 1 if c in (0x80, 0x82) else self.osz(); self.modrm(); self.memseg()
            i = self.u8() if s == 1 else (self.imm(s) if c == 0x81 else self.imm_sx8(s))
            return ALU[self.reg], [self.E(s), hx(i)]
        if c in (0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b):
            s = 1 if c in (0x84, 0x86, 0x88, 0x8a) else self.osz(); self.modrm(); self.memseg()
            mn = {0x84: 'test', 0x85: 'test', 0x86: 'xchg', 0x87: 'xchg'}.get(c, 'mov')
            if c in (0x8a, 0x8b): return mn, [self.G(s), self.E(s)]
            return mn, [self.E(s), self.G(s)]
        if c == 0x8c:
            self.modrm(); self.memseg()
            if self.mod == 3: s = self.osz(); return 'mov', [(R16 if s == 2 else R32)[self.rm], SEG[self.reg]]
            return 'mov', [SZ[2] + self.mem, SEG[self.reg]]
        if c == 0x8d:
            s = self.osz(); self.modrm()
            if self.mod == 3: return 'lea', [self.G(s), '(bad)']
            self.memseg(); return 'lea', [self.G(s), self.mem]
        if c == 0x8e:
            self.modrm(); self.memseg(); return 'mov', [SEG[self.reg], self.E(2) if self.mod != 3 else R16[self.rm] if self.o16 else R32[self.rm]]
        if c == 0x8f:
            s = self.osz(); self.modrm(); self.memseg()
            if self.reg != 0: raise Bad()
            return 'pop', [self.E(s)]
        if 0x91 <= c <= 0x97:
            s = self.osz(); r = R16 if s == 2 else R32; return 'xchg', [r[c & 7], r[0]]
        if c == 0x98: s = self.osz(); return ('cbw' if s == 2 else 'cwde'), []
        if c == 0x99: s = self.osz(); return ('cwd' if s == 2 else 'cdq'), []
        if c in (0x9a, 0xea):
            s = self.osz(); off = self.imm(s); sel = self.u16()
            return ('call' if c == 0x9a else 'jmp'), ['%s:%s' % (hx(sel), hx(off))]
        if c == 0x9c: s = self.osz(); return ('pushfw' if s == 2 else 'pushf'), []
        if c == 0x9d: s = self.osz(); return ('popfw' if s == 2 else 'popf'), []
        if 0xa0 <= c <= 0xa3:
            s = 1 if c in (0xa0, 0xa2) else self.osz()
            a = self.u16() if self.a16 else self.u32(); self.used_seg = bool(self.seg)
            m = '%s:%s' % (self.seg or 'ds', hx(a)); r = 'al' if s == 1 else 'ax' if s == 2 else 'eax'
            return 'mov', [r, m] if c < 0xa2 else [m, r]
        if 0xa4 <= c <= 0xaf and c not in (0xa8, 0xa9):
            s = 1 if c % 2 == 0 else self.osz(); P = SZ[s]; rs = 'al' if s == 1 else 'ax' if s == 2 else 'eax'
            dsi = P + (self.seg or 'ds') + ':[esi]'; edi = P + 'es:[edi]'
            if self.seg: self.used_seg = True
            if c in (0xa4, 0xa5): return self.repname('movs'), [edi, dsi]
            if c in (0xa6, 0xa7): return self.repname('cmps'), [dsi, edi]
            if c in (0xaa, 0xab): return self.repname('stos'), [edi, rs]
            if c in (0xac, 0xad): return self.repname('lods'), [rs, dsi]
            return self.repname('scas'), [rs, edi]
        if c == 0xa8: return 'test', ['al', hx(self.u8())]
        if c == 0xa9: s = self.osz(); return 'test', ['ax' if s == 2 else 'eax', hx(self.imm(s))]
        if 0xb0 <= c < 0xb8: return 'mov', [R8[c & 7], hx(self.u8())]
        if 0xb8 <= c < 0xc0: s = self.osz(); return 'mov', [(R16 if s == 2 else R32)[c & 7], hx(self.imm(s))]
        if c in (0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3):
            s = 1 if c in (0xc0, 0xd0, 0xd2) else self.osz(); self.modrm(); self.memseg()
            src = hx(self.u8()) if c in (0xc0, 0xc1) else '1' if c in (0xd0, 0xd1) else 'cl'
            return SHF[self.reg], [self.E(s), src]
        if c == 0xc2: v = self.u16(); self.used_o16 = True; return ('retw' if self.o16 else 'ret'), [hx(v)]
        if c == 0xc3: self.used_o16 = True; return ('retw' if self.o16 else 'ret'), []
        if c in (0xc4, 0xc5):
            s = self.osz(); self.modrm()
            if self.mod == 3: raise Bad()
            self.memseg(); return ('les' if c == 0xc4 else 'lds'), [self.G(s), SZ[6 if s == 4 else 4] + self.mem]
        if c in (0xc6, 0xc7):
            s = 1 if c == 0xc6 else self.osz(); self.modrm(); self.memseg()
            if self.reg != 0: raise Bad()
            return 'mov', [self.E(s), hx(self.imm(s))]
        if c == 0xc8:
            a = self.u16(); b = self.u8(); self.used_o16 = True; return ('enterw' if self.o16 else 'enter'), [hx(a), hx(b)]
        if c == 0xca: self.used_o16 = True; return ('retfw' if self.o16 else 'retf'), [hx(self.u16())]
        if c == 0xcb: self.used_o16 = True; return ('retfw' if self.o16 else 'retf'), []
        if c == 0xcd: return 'int', [hx(self.u8())]
        if c == 0xcf: self.used_o16 = True; return ('iretw' if self.o16 else 'iret'), []
        if c in (0xd4, 0xd5):
            v = self.u8(); mn = 'aam' if c == 0xd4 else 'aad'
            return mn, ([] if v == 10 else [hx(v)])
        if c == 0xd7: self.used_seg = bool(self.seg); return 'xlat', ['BYTE PTR ' + (self.seg or 'ds') + ':[ebx]']
        if 0xd8 <= c <= 0xdf: return self.fpu(c)
        if 0xe0 <= c <= 0xe3:
            d = self.s8(); t = hx((self.base + self.p + d) & 0xffffffff)
            if c == 0xe3: return ('jcxz' if self.a16 else 'jecxz'), [t]
            return ['loopne', 'loope', 'loop'][c - 0xe0], [t]
        if c in (0xe4, 0xe5): s = 1 if c == 0xe4 else self.osz(); return 'in', ['al' if s == 1 else 'ax' if s == 2 else 'eax', hx(self.u8())]
        if c in (0xe6, 0xe7): s = 1 if c == 0xe6 else self.osz(); v = self.u8(); return 'out', [hx(v), 'al' if s == 1 else 'ax' if s == 2 else 'eax']
        if c in (0xe8, 0xe9):
            if self.o16: self.used_o16 = True; d = self.u16(); d = d - 0x10000 if d & 0x8000 else d; t = (self.base + self.p + d) & 0xffff
            else: d = self.s32(); t = (self.base + self.p + d) & 0xffffffff
            return ('call' if c == 0xe8 else 'jmp') + ('w' if self.o16 else ''), [hx(t)]
        if c == 0xeb: d = self.s8(); return 'jmp', [hx((self.base + self.p + d) & 0xffffffff)]
        if c in (0xec, 0xed): s = 1 if c == 0xec else self.osz(); return 'in', ['al' if s == 1 else 'ax' if s == 2 else 'eax', 'dx']
        if c in (0xee, 0xef): s = 1 if c == 0xee else self.osz(); return 'out', ['dx', 'al' if s == 1 else 'ax' if s == 2 else 'eax']
        if c in (0xf6, 0xf7):
            s = 1 if c == 0xf6 else self.osz(); self.modrm(); self.memseg()
            n = ['test', 'test', 'not', 'neg', 'mul', 'imul', 'div', 'idiv'][self.reg]
            if self.reg < 2: return n, [self.E(s), hx(self.imm(s))]
            return n, [self.E(s)]
        if c == 0xfe:
            self.modrm(); self.memseg()
            if self.reg > 1: raise Bad()
            return ['inc', 'dec'][self.reg], [self.E(1)]
        if c == 0xff:
            self.modrm(); self.memseg(); r = self.reg
            if r in (0, 1, 6): s = self.osz(); return ['inc', 'dec', '', '', '', '', 'push'][r], [self.E(s)]
            if r in (2, 4): s = self.osz(); return ('call' if r == 2 else 'jmp') + ('w' if s == 2 else ''), [self.E(s)]
            if r in (3, 5):
                if self.mod == 3: return ('call' if r == 3 else 'jmp'), ['(bad)']
                s = self.osz(); return ('call' if r == 3 else 'jmp') + ('w' if s == 2 else ''), [SZ[6 if s == 4 else 4] + self.mem]
            raise Bad()
        if c == 0x0f: return self.op0f()
        raise Bad()

    def repname(self, n):
        if self.rep is None: return n
        self.used_rep = True
        if n in ('cmps', 'scas'): return ('repz ' if self.rep == 0xf3 else 'repnz ') + n
        return ('rep ' if self.rep == 0xf3 else 'repnz ') + n

    def op0f(self):
        c = self.u8()
        if 0x80 <= c < 0x90:
            d = self.s32(); return 'j' + CC[c - 0x80], [hx((self.base + self.p + d) & 0xffffffff)]
        if 0x90 <= c < 0xa0:
            self.modrm(); self.memseg(); return 'set' + CC[c - 0x90], [self.E(1)]
        if 0x40 <= c < 0x50:
            s = self.osz(); self.modrm(); self.memseg(); return 'cmov' + CC[c - 0x40], [self.G(s), self.E(s)]
        simple = {0xa2: 'cpuid', 0x31: 'rdtsc', 0x0b: 'ud2', 0x06: 'clts', 0x08: 'invd', 0x09: 'wbinvd', 0x30: 'wrmsr', 0x32: 'rdmsr', 0x33: 'rdpmc',
                  0x34: 'sysenter', 0x35: 'sysexit', 0x05: 'syscall', 0x07: 'sysret', 0x0e: 'femms', 0xaa: 'rsm', 0x77: 'emms'}
        if c in simple: return simple[c], []
        if c in (0xa0, 0xa1, 0xa8, 0xa9):
            s = self.osz(); r = 'fs' if c < 0xa8 else 'gs'
            return ('push' if c in (0xa0, 0xa8) else 'pop') + ('w' if s == 2 else ''), [r]
        if c in (0xa3, 0xab, 0xb3, 0xbb):
            s = self.osz(); self.modrm(); self.memseg(); return {0xa3: 'bt', 0xab: 'bts', 0xb3: 'btr', 0xbb: 'btc'}[c], [self.E(s), self.G(s)]
        if c in (0xa4, 0xa5, 0xac, 0xad):
            s = self.osz(); self.modrm(); self.memseg(); n = 'shld' if c < 0xac else 'shrd'
            return n, [self.E(s), self.G(s), hx(self.u8()) if c in (0xa4, 0xac) else 'cl']
        if c in (0xb2, 0xb4, 0xb5):
            s = self.osz(); self.modrm()
            if self.mod == 3: raise Bad()
            self.memseg(); return {0xb2: 'lss', 0xb4: 'lfs', 0xb5: 'lgs'}[c], [self.G(s), SZ[6 if s == 4 else 4] + self.mem]
        if c == 0xaf: s = self.osz(); self.modrm(); self.memseg(); return 'imul', [self.G(s), self.E(s)]
        if c in (0xb0, 0xb1, 0xc0, 0xc1):
            s = 1 if c in (0xb0, 0xc0) else self.osz(); self.modrm(); self.memseg()
            return ('cmpxchg' if c < 0xc0 else 'xadd'), [self.E(s), self.G(s)]
        if c in (0xb6, 0xb7, 0xbe, 0xbf):
            s = self.osz(); self.modrm(); self.memseg()
            return ('movzx' if c < 0xbe else 'movsx'), [self.G(s), self.E(1 if c in (0xb6, 0xbe) else 2)]
        if c == 0xba:
            s = self.osz(); self.modrm(); self.memseg()
            if self.reg < 4: raise Bad()
            return ['bt', 'bts', 'btr', 'btc'][self.reg - 4], [self.E(s), hx(self.u8())]
        if c in (0xbc, 0xbd): s = self.osz(); self.modrm(); self.memseg(); return ('bsf' if c == 0xbc else 'bsr'), [self.G(s), self.E(s)]
        if 0xc8 <= c < 0xd0: return 'bswap', [R32[c & 7]]
        raise Bad()

    # ---------------------------------------------------------------- x87
    def fpu(self, c):
        self.modrm(); self.memseg(); r = self.reg; i = self.rm
        st = lambda k: 'st(%d)' % k
        if self.mod != 3:
            m = self.mem
            ar = ['fadd', 'fmul', 'fcom', 'fcomp', 'fsub', 'fsubr', 'fdiv', 'fdivr']
            if c == 0xd8: return ar[r], [SZ[4] + m]
            if c == 0xdc: return ar[r], [SZ[8] + m]
            if c == 0xda: return 'fi' + ar[r][1:], [SZ[4] + m]
            if c == 0xde: return 'fi' + ar[r][1:], [SZ[2] + m]
            if c == 0xd9:
                t = [('fld', 4), None, ('fst', 4), ('fstp', 4), ('fldenv', 0), ('fldcw', 2), ('fnstenv', 0), ('fnstcw', 2)][r]
            elif c == 0xdb:
                t = [('fild', 4), ('fisttp', 4), ('fist', 4), ('fistp', 4), None, ('fld', 10), None, ('fstp', 10)][r]
            elif c == 0xdd:
                t = [('fld', 8), ('fisttp', 8), ('fst', 8), ('fstp', 8), ('frstor', 0), None, ('fnsave', 0), ('fnstsw', 2)][r]
            else:
                t = [('fild', 2), ('fisttp', 2), ('fist', 2), ('fistp', 2), ('fbld', 10), ('fild', 8), ('fbstp', 10), ('fistp', 8)][r]
            if t is None: raise Bad()
            return t[0], [SZ[t[1]] + m]
        if c == 0xd8:
            n = ['fadd', 'fmul', 'fcom', 'fcomp', 'fsub', 'fsubr', 'fdiv', 'fdivr'][r]
            return (n, [st(i)]) if r in (2, 3) else (n, ['st', st(i)])
        if c == 0xd9:
            if r == 0: return 'fld', [st(i)]
            if r == 1: return 'fxch', [st(i)]
            t = {0xd0: 'fnop', 0xe0: 'fchs', 0xe1: 'fabs', 0xe4: 'ftst', 0xe5: 'fxam', 0xe8: 'fld1', 0xe9: 'fldl2t', 0xea: 'fldl2e', 0xeb: 'fldpi',
                 0xec: 'fldlg2', 0xed: 'fldln2', 0xee: 'fldz', 0xf0: 'f2xm1', 0xf1: 'fyl2x', 0xf2: 'fptan', 0xf3: 'fpatan', 0xf4: 'fxtract',
                 0xf5: 'fprem1', 0xf6: 'fdecstp', 0xf7: 'fincstp', 0xf8: 'fprem', 0xf9: 'fyl2xp1', 0xfa: 'fsqrt', 0xfb: 'fsincos', 0xfc: 'frndint',
                 0xfd: 'fscale', 0xfe: 'fsin', 0xff: 'fcos'}.get(0xc0 | (r << 3) | i)
            if t is None: raise Bad()
            return t, []
        if c == 0xda:
            if r < 4: return ['fcmovb', 'fcmove', 'fcmovbe', 'fcmovu'][r], ['st', st(i)]
            if r == 5 and i == 1: return 'fucompp', []
            raise Bad()
        if c == 0xdb:
            if r < 4: return ['fcmovnb', 'fcmovne', 'fcmovnbe', 'fcmovnu'][r], ['st', st(i)]
            if r == 4:
                t = {2: 'fnclex', 3: 'fninit', 0: 'fneni(8087 only)', 1: 'fndisi(8087 only)', 4: 'fnsetpm(287 only)', 5: 'frstpm'}.get(i)
                if t is None: raise Bad()
                return t, []
            if r == 5: return 'fucomi', ['st', st(i)]
            if r == 6: return 'fcomi', ['st', st(i)]
            raise Bad()
        if c in (0xdc, 0xde):
            p = 'p' if c == 0xde else ''
            if c == 0xde and r == 3:
                if i == 1: return 'fcompp', []
                raise Bad()
            # binutils (SYSV386-Kompatibilitaet): Namen fuer ST(i)-Ziel vertauscht
            n = ['fadd', 'fmul', 'fcom', 'fcomp', 'fsubr', 'fsub', 'fdivr', 'fdiv'][r]
            if r in (2, 3):
                if c == 0xdc: return n, [st(i)]
                raise Bad()
            return n + p, [st(i), 'st']
        if c == 0xdd:
            t = ['ffree', None, 'fst', 'fstp', 'fucom', 'fucomp', None, None][r]
            if t is None: raise Bad()
            return t, [st(i)]
        if c == 0xdf:
            if r == 4 and i == 0: return 'fnstsw', ['ax']
            if r == 5: return 'fucomip', ['st', st(i)]
            if r == 6: return 'fcomip', ['st', st(i)]
            if r == 0: return 'ffreep', [st(i)]
            raise Bad()
        raise Bad()


def fmt(mn, ops):
    if not ops: return mn
    return '%-6s %s' % (mn, ','.join(ops))


def disasm_one(dec, pos):
    """-> (laenge, text) genau wie objdump (soweit fuer den Rekompilierer noetig)."""
    try:
        # fwait-Verschmelzung (9b + fnstsw/fnstcw/fnclex/fninit/fnstenv/fnsave)
        if dec.b[pos] == 0x9b and pos + 1 < len(dec.b) and dec.b[pos + 1] in (0xd9, 0xdb, 0xdd, 0xdf):
            mn, ops = dec.decode(pos + 1)
            if mn in ('fnstsw', 'fnstcw', 'fnclex', 'fninit', 'fnstenv', 'fnsave') and not dec.seg and not dec.o16:
                return dec.p - pos, fmt('f' + mn[2:], ops)
        mn, ops = dec.decode(pos)
        n = dec.p - pos
        pre = []
        # ungenutzte Praefixe als eigene Woerter (wie objdump)
        b0 = pos
        while b0 < pos + n:
            cbyte = dec.b[b0]
            if cbyte in (0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65):
                if not dec.used_seg: pre.append({0x26: 'es', 0x2e: 'cs', 0x36: 'ss', 0x3e: 'ds', 0x64: 'fs', 0x65: 'gs'}[cbyte])
            elif cbyte == 0x66:
                if not dec.used_o16: pre.append('data16')
            elif cbyte == 0x67:
                if not dec.a16 or True: pass
            elif cbyte == 0xf0: pre.append('lock')
            elif cbyte in (0xf2, 0xf3):
                if not dec.used_rep: pre.append('repz' if cbyte == 0xf3 else 'repnz')
            else:
                break
            b0 += 1
        t = fmt(mn, ops)
        if pre: t = ' '.join(pre) + ' ' + t
        return n, t
    except (Bad, IndexError, KeyError):
        return 1, '(bad)'


def sweep(buf, base, start, end):
    dec = Dec(buf, base)
    pos = start
    out = []
    while pos < end:
        n, t = disasm_one(dec, pos)
        if pos + n > end:   # wie objdump am Abschnittsende
            n = 1; t = '(bad)'
        out.append((base + pos, t))
        pos += n
    return out
