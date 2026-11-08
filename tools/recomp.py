#!/usr/bin/env python3
# civ2recomp — statischer x86 -> C Recompiler fuer civ2.exe (Stufe 1)
# Eingabe: objdump -d -M intel --no-show-raw-insn Ausgabe + civ2.exe (fuer Importe/Bytes)
# Ausgabe: gen/native_*.c + gen/native_table.c + gen/report.txt
import re, sys, struct, collections, os

ASM, EXE, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
BASE = 0x400000

# ------------------------------------------------------------------ PE einlesen
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = pe + 24
sh = opt + struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, sh + 40 * i + 8) for i in range(nsec)]
def off(rva):
    for vs, va, rs, raw in secs:
        if va <= rva < va + max(vs, rs): return rva - va + raw
    return None
def byte_at(va):
    o = off(va - BASE); return d[o] if o is not None else None
TEXT_LO, TEXT_HI = 0x401000, 0x401000 + 0x218e52
# Importe: IAT-Adresse -> (dll, name)
iat = {}
irva = struct.unpack_from('<I', d, opt + 96 + 8)[0]; p = off(irva)
while True:
    oft, _, _, nm, ft = struct.unpack_from('<IIIII', d, p); p += 20
    if not nm: break
    dll = d[off(nm):d.index(b'\0', off(nm))].decode()
    t = off(oft or ft); i = 0
    while True:
        v = struct.unpack_from('<I', d, t + 4 * i)[0]
        if not v: break
        name = ('#%d' % (v & 0xffff)) if v & 0x80000000 else d[off(v) + 2:d.index(b'\0', off(v) + 2)].decode()
        iat[BASE + ft + 4 * i] = (dll, name); i += 1

# ------------------------------------------------------------------ Disassembly einlesen
ins = {}          # addr -> (mnem, ops, text)
order = []
rx = re.compile(r'^\s*([0-9a-f]+):\t(.*)$')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x86dis
CODE_SECS = []   # (va, buf) aller Code-Abschnitte
for i in range(nsec):
    vs, va, rs, raw = secs[i]
    ch = struct.unpack_from('<I', d, sh + 40 * i + 36)[0]
    if ch & 0x20: CODE_SECS.append((BASE + va, d[raw:raw + min(vs, rs)]))
if ASM == '-':   # eingebauter Disassembler (kein objdump noetig)
    for va, buf in CODE_SECS:
        for a, t in x86dis.sweep(buf, va, 0, len(buf)):
            order.append(a); ins[a] = t
else:
    for line in open(ASM):
        m = rx.match(line)
        if not m: continue
        a = int(m.group(1), 16); t = m.group(2).strip()
        if not t: continue
        order.append(a); ins[a] = t
nexta = {}
for i in range(len(order) - 1): nexta[order[i]] = order[i + 1]
import subprocess, glob
IMP_ARGC = {}
for _f in glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', '*.cpp')):
    for _m in re.finditer(r'reg\(\s*\w+\s*,\s*"([A-Za-z0-9_]+)"\s*,\s*(\d+)', open(_f).read()): IMP_ARGC[_m.group(1)] = int(_m.group(2))
_ij = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'imp_argc.json')
if not IMP_ARGC and os.path.exists(_ij):
    import json; IMP_ARGC.update(json.load(open(_ij)))
IMP_CDECL = {'wsprintfA', 'wsprintfW'}
IMP_ARGC.setdefault('DirectDrawCreate', 3)
_redone = set()
def redecode(a):
    """Luecke im linearen Disassembly (z. B. nach eingebetteten Sprungtabellen): ab a neu dekodieren."""
    if a in _redone: return a in ins
    _redone.add(a)
    lines = []
    for va, buf in CODE_SECS:
        if va <= a < va + len(buf):
            lines = x86dis.sweep(buf, va, a - va, min(len(buf), a - va + 0x1000))
    prev = None
    for b, t in lines:
        t = t.strip()
        if not t or t.startswith('(bad)'): break
        new = b not in ins
        ins[b] = t
        if prev is not None: nexta[prev] = b
        prev = b
        if not new and b != a and b in nexta: break   # wieder im Gleichlauf mit dem linearen Disassembly
    return a in ins

PREFIXES = ('rep', 'repz', 'repnz', 'repe', 'repne', 'lock', 'cs', 'ds', 'es', 'ss')
def split_ins(t):
    t = re.sub(r'\s+', ' ', t)
    parts = t.split(' ', 1)
    pre = []
    while parts[0] in PREFIXES and len(parts) > 1:
        pre.append(parts[0]); parts = parts[1].split(' ', 1)
    mn = parts[0]; ops = []
    if len(parts) > 1:
        s = parts[1]
        s = re.sub(r'\s*<[^>]*>', '', s)   # Symbolhinweise entfernen
        depth = 0; cur = ''
        for ch in s:
            if ch == '[': depth += 1
            if ch == ']': depth -= 1
            if ch == ',' and depth == 0: ops.append(cur.strip()); cur = ''
            else: cur += ch
        if cur.strip(): ops.append(cur.strip())
    return pre, mn, ops

# ------------------------------------------------------------------ Operanden
R32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
R16 = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di']
R8 = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh']
class Unsup(Exception): pass

def reg_info(r):
    if r in R32: return (r, 32, 0)
    if r in R16: return (R32[R16.index(r)], 16, 0)
    if r in R8:
        i = R8.index(r); return (R32[i & 3], 8, 8 if i >= 4 else 0)
    return None

def num(s):
    s = s.strip()
    neg = s.startswith('-')
    if neg: s = s[1:]
    v = int(s, 16) if s.startswith('0x') else int(s)
    return (-v) & 0xffffffff if neg else v & 0xffffffff

SIZEW = {'BYTE': 8, 'WORD': 16, 'DWORD': 32}
def parse_op(o):
    """-> dict(kind=reg|imm|mem, size, ...)"""
    o = o.strip()
    ri = reg_info(o)
    if ri: return {'k': 'reg', 'r': ri[0], 'sz': ri[1], 'sh': ri[2]}
    if re.fullmatch(r'-?(0x[0-9a-f]+|\d+)', o): return {'k': 'imm', 'v': num(o), 'sz': 0}
    size = 0
    m = re.match(r'(BYTE|WORD|DWORD|QWORD|TBYTE|FWORD|XMMWORD) PTR (.*)$', o)
    if m:
        if m.group(1) not in SIZEW: raise Unsup('size ' + m.group(1))
        size = SIZEW[m.group(1)]; o = m.group(2)
    seg = None
    m = re.match(r'(cs|ds|es|ss|fs|gs):(.*)$', o)
    if m: seg = m.group(1); o = m.group(2)
    if seg in ('gs', 'cs'): raise Unsup('seg ' + seg)
    if seg == 'fs':
        if re.fullmatch(r'0x[0-9a-f]+', o): return {'k': 'mem', 'sz': size, 'addr': '(uint32_t)(TIB + 0x%xu)' % num(o)}
        m2 = re.fullmatch(r'\[(.*)\]', o)
        if not m2: raise Unsup('fsop')
        inner = parse_op('[' + m2.group(1) + ']')
        return {'k': 'mem', 'sz': size, 'addr': '(uint32_t)(TIB + %s)' % inner['addr']}
    if re.fullmatch(r'0x[0-9a-f]+', o): return {'k': 'mem', 'sz': size, 'addr': '0x%xu' % num(o), 'abs': num(o)}
    m = re.fullmatch(r'\[(.*)\]', o)
    if not m: raise Unsup('op ' + o)
    e = m.group(1).replace('-', '+-')
    terms = [t for t in e.split('+') if t]
    parts = []; disp = 0
    for t in terms:
        if '*' in t:
            r, sc = t.split('*'); ri = reg_info(r)
            if not ri or ri[1] != 32: raise Unsup('idx')
            parts.append('%s*%d' % (r, int(sc)))
        else:
            ri = reg_info(t)
            if ri:
                if ri[1] != 32: raise Unsup('base16')
                parts.append(t)
            else: disp = (disp + num(t)) & 0xffffffff
    if disp: parts.append('0x%xu' % disp)
    return {'k': 'mem', 'sz': size, 'addr': '(uint32_t)(' + ('+'.join(parts) if parts else '0') + ')'}

CT = {8: 'uint8_t', 16: 'uint16_t', 32: 'uint32_t'}
ST = {8: 'int8_t', 16: 'int16_t', 32: 'int32_t'}
MK = {8: '0xffu', 16: '0xffffu', 32: '0xffffffffu'}
def rd(o, sz):
    if o['k'] == 'imm': return '0x%xu' % (o['v'] & int(MK[sz][:-1], 16))
    if o['k'] == 'reg':
        if o['sz'] == 32: return o['r']
        if o['sz'] == 16: return '(%s & 0xffffu)' % o['r']
        return '((%s >> %d) & 0xffu)' % (o['r'], o['sh'])
    return 'RD%d(%s)' % (sz, o['addr'])
def wr(o, sz, v):
    if o['k'] == 'reg':
        if o['sz'] == 32: return '%s = (uint32_t)(%s);' % (o['r'], v)
        if o['sz'] == 16: return '%s = (%s & 0xffff0000u) | ((uint32_t)(%s) & 0xffffu);' % (o['r'], o['r'], v)
        if o['sh']: return '%s = (%s & 0xffff00ffu) | (((uint32_t)(%s) & 0xffu) << 8);' % (o['r'], o['r'], v)
        return '%s = (%s & 0xffffff00u) | ((uint32_t)(%s) & 0xffu);' % (o['r'], o['r'], v)
    if o['k'] == 'mem': return 'WR%d(%s, %s);' % (sz, o['addr'], v)
    raise Unsup('write imm')
def opsize(a, b=None):
    s = a.get('sz', 0)
    if not s and b: s = b.get('sz', 0)
    if not s: s = 32
    return s

# Flag-Arten (lazy): FK(op, size)
OPS = {'ADD': 1, 'SUB': 2, 'LOG': 3, 'INC': 4, 'DEC': 5, 'NEG': 6, 'SHL': 7, 'SHR': 8, 'SAR': 9, 'ADC': 10, 'SBB': 11, 'MUL': 12}
def setf(op, sz, r, a, b, c='lc'):
    return 'lk = FK(%d,%d); lr = (uint32_t)(%s); la = (uint32_t)(%s); lb = (uint32_t)(%s); lcf = %s;' % (OPS[op], sz, r, a, b, c)
CCN = {'o': 0, 'no': 1, 'b': 2, 'c': 2, 'nae': 2, 'ae': 3, 'nb': 3, 'nc': 3, 'e': 4, 'z': 4, 'ne': 5, 'nz': 5, 'be': 6, 'na': 6, 'a': 7, 'nbe': 7,
       's': 8, 'ns': 9, 'p': 10, 'pe': 10, 'np': 11, 'po': 11, 'l': 12, 'nge': 12, 'ge': 13, 'nl': 13, 'le': 14, 'ng': 14, 'g': 15, 'nle': 15}

# ------------------------------------------------------------------ Funktionen finden
call_targets = set(); jmp_targets = set()
for a in order:
    pre, mn, ops = split_ins(ins[a])
    if mn == 'call' and ops and re.fullmatch(r'0x[0-9a-f]+', ops[0]): call_targets.add(int(ops[0], 16))
# ILT-Thunks aufloesen (jmp target direkt)
def resolve(t, depth=0):
    if depth > 4 or t not in ins: return t
    pre, mn, ops = split_ins(ins[t])
    if mn == 'jmp' and ops and re.fullmatch(r'0x[0-9a-f]+', ops[0]): return resolve(int(ops[0], 16), depth + 1)
    return t
entries = set()
for t in call_targets:
    r = resolve(t)
    if TEXT_LO <= r < TEXT_HI and r in ins: entries.add(r)
# zusaetzlich: Zeiger auf Funktionen in Datenbereichen (Fensterprozeduren, vtables) – Werte, die auf 'push ebp; mov ebp,esp' zeigen
for vs, va, rs, raw in secs:
    if BASE + va >= TEXT_LO and BASE + va < TEXT_HI: continue
    for o in range(raw, raw + rs - 3, 4):
        v = struct.unpack_from('<I', d, o)[0]
        if TEXT_LO <= v < TEXT_HI and v in ins and ins[v].startswith('push') and 'ebp' in ins[v]:
            entries.add(v)
# auch Ziele von push imm (Rueckrufe) mit Prolog
for a in order:
    t = ins[a]
    if t.startswith('push') or t.startswith('mov'):
        for m in re.finditer(r'0x([0-9a-f]{6})\b', t):
            v = int(m.group(1), 16)
            if TEXT_LO <= v < TEXT_HI and v in ins and re.match(r'push\s+ebp', ins[v]) and nexta.get(v) and re.match(r'mov\s+ebp,esp', ins[nexta[v]]):
                entries.add(v)

# auch nicht referenzierte Funktionen mit Standard-Prolog (z. B. nur ueber berechnete Zeiger erreichbar)
for i in range(1, len(order) - 1):
    a = order[i]
    if a in entries or not re.match(r'push\s+ebp$', ins[a]) or not re.match(r'mov\s+ebp,esp$', ins[order[i + 1]]): continue
    pm = split_ins(ins[order[i - 1]])[1]
    if pm in ('int3', 'ret', 'retn', 'nop', 'jmp') and TEXT_LO <= a < TEXT_HI: entries.add(a)

# ------------------------------------------------------------------ Lifter
class Func:
    pass

def lift(entry):
    f = Func(); f.entry = entry; f.calls_guest = False; f.pure = True; f.ret = None
    body = {}; work = [entry]; targets = set([entry]); prevmap = {}
    while work:
        a = work.pop(); prev_a = None
        while True:
            if a in body: break
            if a not in ins and not redecode(a): raise Unsup('no insn %x' % a)
            if not (TEXT_LO <= a < TEXT_HI): raise Unsup('outside')
            prev_mn = body[prev_a][1] if prev_a in body else None
            pre, mn, ops = split_ins(ins[a])
            body[a] = (pre, mn, ops)
            if mn in ('ret', 'retn'):
                n = num(ops[0]) if ops else 0
                if not ops and getattr(f, 'pushret', None): break   # evtl. lokaler Ruecksprung (push adr; jmp ...; ret)
                if f.ret is None: f.ret = n
                elif f.ret != n: raise Unsup('ret mixed')
                break
            if mn == 'push' and ops and re.fullmatch(r'0x[0-9a-f]+', ops[0]) and a in nexta:
                pv = int(ops[0], 16); nx_ = nexta[a]
                if TEXT_LO <= pv < TEXT_HI and pv in ins and pv not in entries and nx_ in ins and split_ins(ins[nx_])[1] == 'jmp':
                    f.pushret = getattr(f, 'pushret', set()); f.pushret.add(pv); targets.add(pv); work.append(pv)
            if mn == 'jmp':
                if not ops: raise Unsup('ijmp')
                m4 = re.fullmatch(r'DWORD PTR ds:0x([0-9a-f]+)', ops[0])
                if m4 and int(m4.group(1), 16) in iat:   # Endsprung in einen Import
                    ia = int(m4.group(1), 16); nm = iat[ia][1]
                    if nm.startswith('?') or nm in IMP_CDECL: n = 0
                    elif nm in IMP_ARGC: n = IMP_ARGC[nm] * 4
                    else: raise Unsup('ijmp imp')
                    if f.ret is None: f.ret = n
                    elif f.ret != n: raise Unsup('ret mixed')
                    f.tailimp = getattr(f, 'tailimp', {}); f.tailimp[a] = (ia, n)
                    break
                if not re.fullmatch(r'0x[0-9a-f]+', ops[0]):
                    m3 = re.fullmatch(r'DWORD PTR (?:ds:)?\[(e[a-z]{2})\*4\+0x([0-9a-f]+)\]', ops[0])
                    if not m3: raise Unsup('ijmp')
                    tab = int(m3.group(2), 16); tl = []
                    lim = 512   # Tabellengroesse aus vorangehendem "cmp r, N; ja" bestimmen
                    pa = prev_a; steps = 0
                    while pa is not None and steps < 4:
                        pp, pm, po = body.get(pa, (None, None, None))
                        if pm == 'cmp' and len(po) == 2 and re.fullmatch(r'0x[0-9a-f]+', po[1]): lim = num(po[1]) + 1; break
                        pa = prevmap.get(pa); steps += 1
                    for k in range(lim):
                        o2 = off(tab + 4 * k - BASE)
                        if o2 is None: break
                        v = struct.unpack_from('<I', d, o2)[0]
                        if not (TEXT_LO <= v < TEXT_HI): break
                        if v not in ins and not redecode(v): break
                        tl.append(v)
                    if not tl: raise Unsup('ijmp')
                    f.jtabs = getattr(f, 'jtabs', {}); f.jtabs[a] = tl
                    for v in tl: targets.add(v); work.append(v)
                    break
                t = int(ops[0], 16)
                if t in entries and t != entry: raise Unsup('tailjmp')
                targets.add(t); work.append(t); break
            if mn.startswith('j') and mn != 'jmp':
                t = int(ops[0], 16); targets.add(t); work.append(t)
            if mn in ('int3', 'hlt'): break   # Debug-Haltepunkt / hinter nicht zurueckkehrendem Aufruf: Pfadende
            if mn in ('int3', 'hlt', '(bad)'): raise Unsup('bad')
            if a not in nexta: raise Unsup('end')
            prevmap[nexta[a]] = a; prev_a = a; a = nexta[a]
    if f.ret is None: f.ret = 0   # z.B. ExitProcess-Pfad ohne ret
    f.body = body; f.targets = targets
    return f


# ------------------------------------------------------------------ x87 (geteilter FPU-Stapel mit dem Emulator)
FPU_MN = {'fld','fld1','fldz','fldpi','fldl2e','fldln2','fldlg2','fldl2t','fild','fst','fstp','fist','fistp','fisttp',
          'fadd','fsub','fsubr','fmul','fdiv','fdivr','faddp','fsubp','fsubrp','fmulp','fdivp','fdivrp',
          'fiadd','fisub','fisubr','fimul','fidiv','fidivr','fcom','fcomp','fcompp','fucom','fucomp','fucompp','ficom','ficomp','ftst',
          'fxch','fchs','fabs','fsqrt','frndint','fnstsw','fstsw','fnstcw','fstcw','fldcw','fnclex','fclex','fninit','finit','fxam'}
def fst_idx(o):
    o = o.strip()
    if o == 'st' or o == 'st(0)': return 0
    m = re.fullmatch(r'st\((\d)\)', o)
    return int(m.group(1)) if m else None
def fmem(o):
    m = re.match(r'(BYTE|WORD|DWORD|QWORD|TBYTE|FWORD) PTR (.*)$', o.strip())
    if not m: raise Unsup('fop ' + o)
    sz = {'WORD': 16, 'DWORD': 32, 'QWORD': 64}.get(m.group(1))
    if not sz: raise Unsup('fsize ' + m.group(1))
    return sz, parse_op('DWORD PTR ' + m.group(2))['addr']
def fexpr(k, x, y):
    return {'add': '(%s) + (%s)', 'sub': '(%s) - (%s)', 'mul': '(%s) * (%s)', 'div': '(%s) / (%s)'}[k] % (x, y) if k in ('add', 'sub', 'mul', 'div') else \
           {'subr': '(%s) - (%s)', 'divr': '(%s) / (%s)'}[k] % (y, x)
def gen_fpu(a, mn, ops):
    S = []
    if mn in ('fld1', 'fldz', 'fldpi', 'fldl2e', 'fldln2', 'fldlg2', 'fldl2t'):
        v = {'fld1': '1.0', 'fldz': '0.0', 'fldpi': '3.14159265358979323846', 'fldl2e': '1.4426950408889634', 'fldln2': '0.69314718055994531',
             'fldlg2': '0.3010299956639812', 'fldl2t': '3.3219280948873623'}[mn]
        return ['civ2_fpush(%s);' % v]
    if mn == 'fld':
        i = fst_idx(ops[0])
        if i is not None: return ['{ double _v = FST(%d); civ2_fpush(_v); }' % i]
        sz, ad = fmem(ops[0])
        if sz == 16: raise Unsup('fld16')
        return ['civ2_fpush(RDF%d(%s));' % (sz, ad)]
    if mn == 'fild':
        sz, ad = fmem(ops[0])
        r = {16: '(double)(int16_t)RD16(%s)', 32: '(double)(int32_t)RD32(%s)', 64: '(double)RDI64(%s)'}[sz] % ad
        return ['civ2_fpush(%s);' % r]
    if mn in ('fst', 'fstp'):
        i = fst_idx(ops[0])
        if i is not None: S.append('FST(%d) = FST(0);' % i)
        else:
            sz, ad = fmem(ops[0])
            if sz == 16: raise Unsup('fst16')
            S.append('WRF%d(%s, FST(0));' % (sz, ad))
        if mn == 'fstp': S.append('civ2_fpop();')
        return S
    if mn in ('fist', 'fistp', 'fisttp'):
        sz, ad = fmem(ops[0])
        v = '(int64_t)trunc(FST(0))' if mn == 'fisttp' else 'FIST(FST(0), %d)' % sz
        S.append({16: 'WR16(%s, (uint32_t)(uint16_t)(%s));', 32: 'WR32(%s, (uint32_t)(%s));', 64: 'WRI64(%s, %s);'}[sz] % (ad, v))
        if mn != 'fist': S.append('civ2_fpop();')
        return S
    base = mn[1:].rstrip('p') if mn.endswith('p') and mn not in ('fcomp', 'fucomp', 'ficomp', 'fcompp', 'fucompp') else mn[1:]
    if mn in ('fadd', 'fsub', 'fsubr', 'fmul', 'fdiv', 'fdivr', 'faddp', 'fsubp', 'fsubrp', 'fmulp', 'fdivp', 'fdivrp'):
        if len(ops) == 1 and fst_idx(ops[0]) is None:   # Speicheroperand: ST0 = ST0 op m
            sz, ad = fmem(ops[0])
            if sz == 16: raise Unsup('farith16')
            return ['FST(0) = %s;' % fexpr(base, 'FST(0)', 'RDF%d(%s)' % (sz, ad))]
        # Registerformen aus den Bytes dekodieren (objdump vertauscht fsub/fsubr bei ST(i)-Zielen)
        op = byte_at(a); mr = byte_at(a + 1)
        if op not in (0xd8, 0xdc, 0xde) or (mr >> 6) != 3: raise Unsup('fregform')
        r = (mr >> 3) & 7; i = mr & 7
        K = {0: 'add', 1: 'mul', 4: 'sub', 5: 'subr', 6: 'div', 7: 'divr'}
        if op == 0xd8:
            if r not in K: raise Unsup('fd8')
            return ['FST(0) = %s;' % fexpr(K[r], 'FST(0)', 'FST(%d)' % i)]
        K2 = {0: 'add', 1: 'mul', 4: 'subr', 5: 'sub', 6: 'divr', 7: 'div'}   # ST(i) = ST(i) op ST0
        if r not in K2: raise Unsup('fdc')
        S.append('FST(%d) = %s;' % (i, fexpr(K2[r], 'FST(%d)' % i, 'FST(0)')))
        if op == 0xde: S.append('civ2_fpop();')
        return S
    if mn in ('fiadd', 'fisub', 'fisubr', 'fimul', 'fidiv', 'fidivr'):
        sz, ad = fmem(ops[0])
        v = '(double)(int16_t)RD16(%s)' % ad if sz == 16 else '(double)(int32_t)RD32(%s)' % ad
        return ['FST(0) = %s;' % fexpr(mn[2:], 'FST(0)', v)]
    if mn in ('fcom', 'fcomp', 'fucom', 'fucomp', 'ficom', 'ficomp', 'ftst', 'fcompp', 'fucompp'):
        if mn == 'ftst': b = '0.0'
        elif mn in ('fcompp', 'fucompp') or not ops: b = 'FST(1)'
        elif fst_idx(ops[0]) is not None: b = 'FST(%d)' % fst_idx(ops[0])
        else:
            sz, ad = fmem(ops[0])
            if mn.startswith('fi'): b = '(double)(int16_t)RD16(%s)' % ad if sz == 16 else '(double)(int32_t)RD32(%s)' % ad
            else:
                if sz == 16: raise Unsup('fcom16')
                b = 'RDF%d(%s)' % (sz, ad)
        S.append('civ2_fcom(%s);' % b)
        if mn.endswith('pp'): S.append('civ2_fpop(); civ2_fpop();')
        elif mn.endswith('p'): S.append('civ2_fpop();')
        return S
    if mn == 'fxch':
        i = fst_idx(ops[0]) if ops else 1
        return ['civ2_fxch(%d);' % i]
    if mn == 'fchs': return ['FST(0) = -FST(0);']
    if mn == 'fabs': return ['FST(0) = fabs(FST(0));']
    if mn == 'fsqrt': return ['FST(0) = sqrt(FST(0));']
    if mn == 'frndint': return ['FST(0) = FRND(FST(0));']
    if mn in ('fnstsw', 'fstsw'):
        if ops and ops[0] == 'ax': return ['eax = (eax & 0xffff0000u) | (civ2_fsw() & 0xffffu);']
        sz, ad = fmem(ops[0]); return ['WR16(%s, civ2_fsw());' % ad]
    if mn in ('fnstcw', 'fstcw'):
        sz, ad = fmem(ops[0]); return ['WR16(%s, civ2_fcw());' % ad]
    if mn == 'fldcw':
        sz, ad = fmem(ops[0]); return ['civ2_fsetcw(RD16(%s));' % ad]
    if mn in ('fnclex', 'fclex'): return ['civ2_fclex();']
    if mn in ('fninit', 'finit'): return ['civ2_finit();']
    raise Unsup('mn ' + mn)

def gen(f, native_entries):
    f.has_std = any(v[1] == 'std' for v in f.body.values())
    out = []
    E = out.append
    E('void N_%x(Ctx* c) {' % f.entry)
    E('  uint32_t eax=c->r[0],ecx=c->r[1],edx=c->r[2],ebx=c->r[3],esp=c->r[4],ebp=c->r[5],esi=c->r[6],edi=c->r[7];')
    E('  uint32_t lk=0,lr=0,la=0,lb=0,lcf=0,df=0; (void)lk;(void)lr;(void)la;(void)lb;(void)lcf;(void)df;')
    flush = 'c->r[0]=eax;c->r[1]=ecx;c->r[2]=edx;c->r[3]=ebx;c->r[4]=esp;c->r[5]=ebp;c->r[6]=esi;c->r[7]=edi;'
    load = 'eax=c->r[0];ecx=c->r[1];edx=c->r[2];ebx=c->r[3];esp=c->r[4];ebp=c->r[5];esi=c->r[6];edi=c->r[7];'
    addrs = sorted(f.body)
    f.targets.add(f.entry)
    if addrs[0] != f.entry: E('  goto L_%x;' % f.entry)
    for idx, a in enumerate(addrs):
        pre, mn, ops = f.body[a]
        if a in f.targets: E('L_%x:;' % a)
        nx = nexta.get(a)
        s = []
        if mn in FPU_MN:
            f.pure = False; f.fpu = True
            E('  ' + ' '.join(gen_fpu(a, mn, ops)))
            if nx is not None:
                nxt_in_order = addrs[idx + 1] if idx + 1 < len(addrs) else None
                if nxt_in_order != nx: E('  goto L_%x;' % nx); f.targets.add(nx)
            continue
        P = [parse_op(o) for o in ops] if mn not in ('call', 'jmp') and not mn.startswith('j') else []
        lc = 'CF_NOW(lk,lr,la,lb,lcf)'
        if mn == 'nop' or mn == 'fwait' or mn == 'wait': pass
        elif mn == 'cld': s.append('df = 0;')
        elif mn == 'std': s.append('df = 1;')
        elif mn in ('jecxz', 'jcxz'):
            s.append('if (%s == 0) goto L_%x;' % ('ecx' if mn == 'jecxz' else '(ecx & 0xffffu)', int(ops[0], 16)))
        elif mn in ('rcr', 'rcl'):
            sz = opsize(P[0]); m = MK[sz]
            cnt = rd(P[1], 8) if len(P) > 1 else '1'
            if mn == 'rcr': step = '_nb = _a & 1u; _a = ((_a >> 1) | (_cf << %d)) & %s; _cf = _nb;' % (sz - 1, m)
            else: step = '_nb = (_a >> %d) & 1u; _a = ((_a << 1) | _cf) & %s; _cf = _nb;' % (sz - 1, m)
            s.append('{ uint32_t _n = ((%s) & 31u) %% %du, _a = %s, _cf = fCF(lk,lr,la,lb,lcf), _nb; if (_n) { while (_n--) { %s } %s lk = FK(4,%d); lcf = _cf; } }' % (cnt, sz + 1, rd(P[0], sz), step, wr(P[0], sz, '_a'), sz))
        elif mn == 'mov':
            sz = opsize(P[0], P[1]); s.append(wr(P[0], sz, rd(P[1], sz)))
        elif mn in ('movzx', 'movsx'):
            ssz = P[1]['sz'] or 8
            v = rd(P[1], ssz)
            if mn == 'movsx': v = '(uint32_t)(int32_t)(%s)(%s)' % (ST[ssz], v)
            s.append(wr(P[0], P[0]['sz'], v))
        elif mn == 'lea':
            if P[1]['k'] != 'mem': raise Unsup('lea')
            s.append(wr(P[0], P[0]['sz'], P[1]['addr']))
        elif mn == 'push':
            if P[0]['k'] == 'reg' and P[0]['sz'] != 32: raise Unsup('push16')
            s.append('{ uint32_t _v = %s; esp -= 4; WR32(esp, _v); }' % rd(P[0], 32))
        elif mn == 'pop':
            if P[0]['k'] == 'reg' and P[0]['sz'] != 32: raise Unsup('pop16')
            s.append('{ uint32_t _v = RD32(esp); esp += 4; %s }' % wr(P[0], 32, '_v'))
        elif mn in ('add', 'sub', 'and', 'or', 'xor', 'cmp', 'test', 'adc', 'sbb'):
            sz = opsize(P[0], P[1]); m = MK[sz]
            if mn in ('xor', 'sub') and P[0]['k'] == 'reg' and P[1]['k'] == 'reg' and ops[0] == ops[1]:
                s.append(wr(P[0], sz, '0')); s.append(setf('LOG', sz, '0', '0', '0', '0'))
            else:
                s.append('{ uint32_t _a = %s, _b = %s, _r;' % (rd(P[0], sz), rd(P[1], sz)))
                if mn == 'add': s.append('_r = (_a + _b) & %s; %s' % (m, setf('ADD', sz, '_r', '_a', '_b', '0')))
                elif mn in ('sub', 'cmp'): s.append('_r = (_a - _b) & %s; %s' % (m, setf('SUB', sz, '_r', '_a', '_b', '0')))
                elif mn == 'adc': s.append('{ uint32_t _c = %s; _r = (_a + _b + _c) & %s; %s }' % (lc, m, setf('ADC', sz, '_r', '_a', '_b', '_c')))
                elif mn == 'sbb': s.append('{ uint32_t _c = %s; _r = (_a - _b - _c) & %s; %s }' % (lc, m, setf('SBB', sz, '_r', '_a', '_b', '_c')))
                else:
                    opc = {'and': '&', 'test': '&', 'or': '|', 'xor': '^'}[mn]
                    s.append('_r = (_a %s _b) & %s; %s' % (opc, m, setf('LOG', sz, '_r', '_a', '_b', '0')))
                if mn not in ('cmp', 'test'): s.append(wr(P[0], sz, '_r'))
                s.append('}')
        elif mn in ('inc', 'dec', 'neg', 'not'):
            sz = opsize(P[0]); m = MK[sz]
            s.append('{ uint32_t _a = %s, _r;' % rd(P[0], sz))
            if mn == 'inc': s.append('{ uint32_t _c = %s; _r = (_a + 1) & %s; %s }' % (lc, m, setf('INC', sz, '_r', '_a', '1', '_c')))
            elif mn == 'dec': s.append('{ uint32_t _c = %s; _r = (_a - 1) & %s; %s }' % (lc, m, setf('DEC', sz, '_r', '_a', '1', '_c')))
            elif mn == 'neg': s.append('_r = (0u - _a) & %s; %s' % (m, setf('NEG', sz, '_r', '0', '_a', '0')))
            else: s.append('_r = (~_a) & %s;' % m)
            s.append(wr(P[0], sz, '_r')); s.append('}')
        elif mn in ('shl', 'sal', 'shr', 'sar', 'rol', 'ror'):
            sz = opsize(P[0]); m = MK[sz]
            cnt = rd(P[1], 8) if len(P) > 1 else '1'
            s.append('{ uint32_t _n = (%s) & 31u; if (_n) { uint32_t _a = %s, _r;' % (cnt, rd(P[0], sz)))
            if mn in ('shl', 'sal'): s.append('_r = (_n >= %d) ? 0 : ((_a << _n) & %s); %s' % (sz, m, setf('SHL', sz, '_r', '_a', '_n', '0')))
            elif mn == 'shr': s.append('_r = (_n >= %d) ? 0 : (_a >> _n); %s' % (sz, setf('SHR', sz, '_r', '_a', '_n', '0')))
            elif mn == 'sar': s.append('_r = (uint32_t)(((int32_t)(%s)_a) >> (_n >= %d ? %d : _n)) & %s; %s' % (ST[sz], sz, sz - 1, m, setf('SAR', sz, '_r', '_a', '_n', '0')))
            elif mn == 'rol': s.append('{ uint32_t _k = _n %% %d; _r = _k ? (((_a << _k) | (_a >> (%d - _k))) & %s) : _a; lk = FK(13,%d); lr=_r; lcf = _r & 1; }' % (sz, sz, m, sz))
            else: s.append('{ uint32_t _k = _n %% %d; _r = _k ? (((_a >> _k) | (_a << (%d - _k))) & %s) : _a; lk = FK(13,%d); lr=_r; lcf = (_r >> %d) & 1; }' % (sz, sz, m, sz, sz - 1))
            s.append(wr(P[0], sz, '_r')); s.append('} }')
        elif mn == 'imul':
            if len(P) == 1:
                sz = opsize(P[0])
                if sz != 32: raise Unsup('imul8')
                s.append('{ int64_t _p = (int64_t)(int32_t)eax * (int64_t)(int32_t)%s; eax = (uint32_t)_p; edx = (uint32_t)(_p >> 32); lk = FK(12,32); lcf = (_p != (int64_t)(int32_t)eax); }' % rd(P[0], 32))
            else:
                sz = opsize(P[0])
                a, b = (P[0], P[1]) if len(P) == 2 else (P[1], P[2])
                s.append('{ int64_t _p = (int64_t)(%s)%s * (int64_t)(%s)%s; %s lk = FK(12,%d); lcf = (_p != (int64_t)(%s)_p); }' % (ST[sz], rd(a, sz), ST[sz], rd(b, sz), wr(P[0], sz, '(uint32_t)_p'), sz, ST[sz]))
        elif mn == 'mul':
            sz = opsize(P[0])
            if sz != 32: raise Unsup('mul8')
            s.append('{ uint64_t _p = (uint64_t)eax * (uint64_t)%s; eax = (uint32_t)_p; edx = (uint32_t)(_p >> 32); lk = FK(12,32); lcf = (edx != 0); }' % rd(P[0], 32))
        elif mn in ('div', 'idiv'):
            sz = opsize(P[0])
            if sz == 32:
                if mn == 'idiv': s.append('{ int64_t _n = (int64_t)(((uint64_t)edx << 32) | eax); int32_t _d = (int32_t)%s; if (!_d) DIVZERO(0x%x); int64_t _q = _n / _d, _m = _n %% _d; eax = (uint32_t)_q; edx = (uint32_t)_m; }' % (rd(P[0], 32), a))
                else: s.append('{ uint64_t _n = ((uint64_t)edx << 32) | eax; uint32_t _d = %s; if (!_d) DIVZERO(0x%x); eax = (uint32_t)(_n / _d); edx = (uint32_t)(_n %% _d); }' % (rd(P[0], 32), a))
            elif sz == 16:
                if mn == 'idiv': s.append('{ int32_t _n = (int32_t)(((edx & 0xffffu) << 16) | (eax & 0xffffu)); int16_t _d = (int16_t)%s; if (!_d) DIVZERO(0x%x); %s %s }' % (rd(P[0], 16), a, wr(reg_op('ax'), 16, '(uint32_t)(_n / _d)'), wr(reg_op('dx'), 16, '(uint32_t)(_n % _d)')))
                else: s.append('{ uint32_t _n = ((edx & 0xffffu) << 16) | (eax & 0xffffu); uint16_t _d = %s; if (!_d) DIVZERO(0x%x); %s %s }' % (rd(P[0], 16), a, wr(reg_op('ax'), 16, '(_n / _d)'), wr(reg_op('dx'), 16, '(_n % _d)')))
            else:
                if mn == 'idiv': s.append('{ int16_t _n = (int16_t)(eax & 0xffffu); int8_t _d = (int8_t)%s; if (!_d) DIVZERO(0x%x); eax = (eax & 0xffff0000u) | ((uint32_t)(uint8_t)(_n / _d)) | ((uint32_t)(uint8_t)(_n %% _d) << 8); }' % (rd(P[0], 8), a))
                else: s.append('{ uint16_t _n = (uint16_t)(eax & 0xffffu); uint8_t _d = %s; if (!_d) DIVZERO(0x%x); eax = (eax & 0xffff0000u) | ((uint32_t)(uint8_t)(_n / _d)) | ((uint32_t)(uint8_t)(_n %% _d) << 8); }' % (rd(P[0], 8), a))
        elif mn == 'cdq': s.append('edx = (uint32_t)((int32_t)eax >> 31);')
        elif mn == 'cwde': s.append('eax = (uint32_t)(int32_t)(int16_t)(eax & 0xffffu);')
        elif mn == 'cbw': s.append('eax = (eax & 0xffff0000u) | ((uint32_t)(int16_t)(int8_t)(eax & 0xffu) & 0xffffu);')
        elif mn == 'cwd': s.append('edx = (edx & 0xffff0000u) | (((int16_t)(eax & 0xffffu) < 0) ? 0xffffu : 0u);')
        elif mn == 'xchg':
            sz = opsize(P[0], P[1])
            s.append('{ uint32_t _a = %s, _b = %s; %s %s }' % (rd(P[0], sz), rd(P[1], sz), wr(P[0], sz, '_b'), wr(P[1], sz, '_a')))
        elif mn == 'leave': s.append('esp = ebp; ebp = RD32(esp); esp += 4;')
        elif mn in ('ret', 'retn'):
            pr = sorted(getattr(f, 'pushret', set()))
            if pr and not ops:
                cases = ' '.join('case 0x%xu: esp += 4; goto L_%x;' % (v, v) for v in pr)
                s.append('switch (RD32(esp)) { %s default: break; }' % cases)
            s.append(flush); s.append('return;')
        elif mn in ('int3', 'hlt'):
            s.append(flush); s.append('BADJMP(0x%x); return;' % a)
        elif mn == 'jmp' and a in getattr(f, 'tailimp', {}):
            ia, n = f.tailimp[a]
            s.append(flush); s.append('CALL_GUEST(c, RD32(0x%xu)); c->r[4] -= %du; return;' % (ia, 4 + n))
        elif mn == 'jmp':
            if a in getattr(f, 'jtabs', {}):
                po = parse_op(ops[0])
                cases = ' '.join('case 0x%xu: goto L_%x;' % (v, v) for v in sorted(set(f.jtabs[a])))
                s.append('switch (%s) { %s default: BADJMP(0x%x); }' % (rd(po, 32), cases, a))
            else:
                s.append('goto L_%x;' % int(ops[0], 16))
        elif mn.startswith('j'):
            cc = CCN.get(mn[1:])
            if cc is None: raise Unsup(mn)
            s.append('if (CC(%d,lk,lr,la,lb,lcf)) goto L_%x;' % (cc, int(ops[0], 16)))
        elif mn.startswith('set'):
            cc = CCN.get(mn[3:])
            if cc is None: raise Unsup(mn)
            s.append(wr(P[0], 8, 'CC(%d,lk,lr,la,lb,lcf)' % cc))
        elif mn == 'call':
            o = ops[0]
            if re.fullmatch(r'0x[0-9a-f]+', o):
                t = resolve(int(o, 16))
                s.append('esp -= 4; WR32(esp, 0x%xu); %s' % (nx, flush))
                if t in native_entries:
                    s.append('if (NAT_ON(%d)) { N_%x(c); %s esp += %du; } else { CALL_GUEST(c, 0x%xu); %s }' % (index_of[t], t, load, 4 + native_entries[t], t, load))
                    f.pure_calls = getattr(f, 'pure_calls', set()); f.pure_calls.add(t)
                else:
                    f.pure = False; f.calls_guest = True
                    s.append('CALL_GUEST(c, 0x%xu); %s' % (t, load))
            else:
                f.pure = False; f.calls_guest = True
                po = parse_op(o)
                tv = rd(po, 32)
                s.append('{ uint32_t _t = %s; esp -= 4; WR32(esp, 0x%xu); %s CALL_ANY(c, _t); %s }' % (tv, nx, flush, load))
        elif mn in ('movs', 'stos', 'lods', 'scas', 'cmps'):
            sz = 0
            for o in P:
                if o['k'] == 'mem' and o['sz']: sz = o['sz']
                if o['k'] == 'reg': sz = o['sz']
            if not sz: raise Unsup('strsz')
            n = sz // 8
            rep = 'rep' in pre or 'repz' in pre or 'repe' in pre
            repnz = 'repnz' in pre or 'repne' in pre
            n = '(df ? (uint32_t)-%d : %du)' % (n, n) if getattr(f, 'has_std', False) else n
            if mn == 'movs': body = 'WR%d(edi, RD%d(esi)); esi += %s; edi += %s;' % (sz, sz, n, n)
            elif mn == 'stos': body = 'WR%d(edi, %s); edi += %s;' % (sz, rd(reg_op({8: 'al', 16: 'ax', 32: 'eax'}[sz]), sz), n)
            elif mn == 'lods': body = '%s esi += %s;' % (wr(reg_op({8: 'al', 16: 'ax', 32: 'eax'}[sz]), sz, 'RD%d(esi)' % sz), n)
            elif mn == 'scas': body = '{ uint32_t _a = %s, _b = RD%d(edi), _r = (_a - _b) & %s; %s edi += %s; }' % (rd(reg_op({8: 'al', 16: 'ax', 32: 'eax'}[sz]), sz), sz, MK[sz], setf('SUB', sz, '_r', '_a', '_b', '0'), n)
            else: body = '{ uint32_t _a = RD%d(esi), _b = RD%d(edi), _r = (_a - _b) & %s; %s esi += %s; edi += %s; }' % (sz, sz, MK[sz], setf('SUB', sz, '_r', '_a', '_b', '0'), n, n)
            if mn in ('scas', 'cmps'):
                if rep: s.append('while (ecx) { %s ecx--; if (!CC(4,lk,lr,la,lb,lcf)) break; }' % body)
                elif repnz: s.append('while (ecx) { %s ecx--; if (CC(4,lk,lr,la,lb,lcf)) break; }' % body)
                else: s.append(body)
            else:
                if rep or repnz: s.append('while (ecx) { %s ecx--; }' % body)
                else: s.append(body)
        else:
            raise Unsup('mn ' + mn)
        if s: E('  ' + ' '.join(s))
        # Fallthrough am Funktions-/Blockende: wenn naechste Adresse nicht im Koerper -> Sprung dahin
        if mn not in ('ret', 'retn', 'jmp', 'int3', 'hlt') and nx is not None:
            nxt_in_order = addrs[idx + 1] if idx + 1 < len(addrs) else None
            if nxt_in_order != nx:
                E('  goto L_%x;' % nx); f.targets.add(nx)
    E('}')
    return '\n'.join(out)

def reg_op(r):
    ri = reg_info(r); return {'k': 'reg', 'r': ri[0], 'sz': ri[1], 'sh': ri[2]}

# ------------------------------------------------------------------ Ablauf
funcs = {}; fails = collections.Counter(); failwhy = {}
todo = sorted(entries)
while todo:
    for e in todo:
        try:
            funcs[e] = lift(e)
        except Unsup as x:
            fails[str(x).split(' ')[0] + ' ' + (str(x).split(' ')[1] if ' ' in str(x) and str(x).startswith('mn') else '')] += 1
            failwhy[e] = str(x)
    # weitere Aufrufziele aus neu dekodierten Bereichen
    new = set()
    for e in todo:
        f = funcs.get(e)
        if not f: continue
        for a, (pre, mn, ops) in f.body.items():
            if mn == 'call' and ops and re.fullmatch(r'0x[0-9a-f]+', ops[0]):
                t = resolve(int(ops[0], 16))
                if TEXT_LO <= t < TEXT_HI and t not in entries and (t in ins or redecode(t)): new.add(t)
    entries |= new
    todo = sorted(new)
# Prolog-Pruefung fuer Einstieg (>=5 Bytes ganzer, positionsunabhaengiger Befehle, keine Sprungziele darin)
def patch_len(f):
    a = f.entry; n = 0
    while n < 5:
        if a not in f.body: return 0
        pre, mn, ops = f.body[a]
        if mn in ('call', 'jmp', 'ret', 'retn') or mn.startswith('j') or mn.startswith('loop'): return 0
        if a != f.entry and a in f.targets: return 0
        nx = nexta[a]; n += nx - a; a = nx
    if a in f.targets and False: return 0
    for t in f.targets:
        if f.entry < t < f.entry + n: return 0
    return n
native_entries = {}
ok = {}
for e, f in funcs.items():
    pl = patch_len(f)
    f.plen = pl   # 0 = Einsprung nicht patchbar: nur von nativem Code aus direkt aufgerufen
    native_entries[e] = f.ret
    ok[e] = f
# Generieren (zweiter Durchlauf: Aufrufe unter nativen Funktionen direkt)
index_of = {e: i for i, e in enumerate(sorted(ok))}
codes = {}
for e in list(ok):
    f = ok[e]
    try:
        f.targets = set(f.targets)
        codes[e] = gen(f, native_entries)
    except Unsup as x:
        failwhy[e] = str(x); fails['gen ' + str(x).split(' ')[0]] += 1
        del ok[e]; native_entries.pop(e, None)
# Erneut generieren, da native_entries geschrumpft sein kann
index_of = {e: i for i, e in enumerate(sorted(ok))}
codes = {}
for e in list(ok):
    f = ok[e]; f.pure = True; f.calls_guest = False
    codes[e] = gen(f, native_entries)
# Reinheit transitiv: rein = keine Gast-/Import-/indirekten Aufrufe, und alle nativen Aufrufziele rein
callees = {}
for e, f in ok.items():
    cs = set()
    for a, (pre, mn, ops) in f.body.items():
        if mn == 'call' and ops and re.fullmatch(r'0x[0-9a-f]+', ops[0]):
            t = resolve(int(ops[0], 16))
            if t in ok: cs.add(t)
    callees[e] = cs
pure = {e for e, f in ok.items() if f.pure}
changed = True
while changed:
    changed = False
    for e in list(pure):
        if any(c not in pure for c in callees[e]): pure.discard(e); changed = True

os.makedirs(OUT, exist_ok=True)
hdr = '#include "native_rt.h"\n'
keys = sorted(codes)
CH = 400
nfiles = 0
for i in range(0, len(keys), CH):
    with open(os.path.join(OUT, 'native_%02d.c' % nfiles), 'w') as fo:
        fo.write(hdr)
        for e in keys:  # Prototypen aller Funktionen
            fo.write('void N_%x(Ctx* c);\n' % e)
        for e in keys[i:i + CH]: fo.write(codes[e] + '\n\n')
    nfiles += 1
with open(os.path.join(OUT, 'native_table.c'), 'w') as fo:
    fo.write(hdr)
    for e in keys: fo.write('void N_%x(Ctx* c);\n' % e)
    fo.write('const NativeFn g_native_table[] = {\n')
    for e in keys:
        f = ok[e]
        fo.write('  {0x%xu, N_%x, %du, %du, %du},\n' % (e, e, f.ret, f.plen, 1 if e in pure else 0))
    fo.write('};\nconst unsigned g_native_count = %d;\n' % len(keys))
total = len(entries)
code_ok = sum(len(ok[e].body) for e in ok)
with open(os.path.join(OUT, 'report.txt'), 'w') as fo:
    fo.write('Funktionen gefunden: %d\nnativ uebersetzt: %d (%.1f%%), davon rein (pruefbar): %d\nBefehle nativ: %d\n' % (total, len(ok), 100.0 * len(ok) / total, len(pure), code_ok))
    fo.write('Gruende:\n')
    for k, v in fails.most_common(40): fo.write('  %6d %s\n' % (v, k))
    fo.write('Ohne Einsprung-Patch (nur nativ erreichbar): %d\n' % sum(1 for e in ok if ok[e].plen == 0))
    for e in sorted(failwhy): fo.write('  %x %s\n' % (e, failwhy[e]))
print(open(os.path.join(OUT, 'report.txt')).read())

# ---- Diagnose: nicht abgedeckte Codebereiche
if os.environ.get('GAPS'):
    cov = set()
    for e, f in funcs.items(): cov.update(f.body.keys())
    runs = []; cur = None
    for a in order:
        t = ins[a]; mn = t.split()[0] if t else ''
        if a in cov or mn in ('int3', 'nop', '(bad)'):
            if cur: runs.append(cur); cur = None
            continue
        if cur is None: cur = [a, 0, t]
        cur[1] += 1
    if cur: runs.append(cur)
    runs.sort(key=lambda r: -r[1])
    print('Luecken:', len(runs), 'Befehle:', sum(r[1] for r in runs))
    for r in runs[:40]: print('  %x %5d  %s' % (r[0], r[1], r[2]))
