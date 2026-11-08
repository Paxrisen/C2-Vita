import struct, re, glob, sys
exe = open('/mnt/user-data/uploads/Civilization 2/civ2.exe','rb').read()
pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe+6)[0]; opt = pe+24; optsz = struct.unpack_from('<H', exe, pe+20)[0]
base = struct.unpack_from('<I', exe, opt+28)[0]; imgsz = struct.unpack_from('<I', exe, opt+56)[0]
dd = opt+96
imp_rva, imp_sz = struct.unpack_from('<II', exe, dd+8)
img = bytearray(imgsz)
img[:0x1000] = exe[:0x1000]
sh = opt+optsz
secs=[]
for i in range(nsec):
    name, vsz, va, rsz, raw = struct.unpack_from('<8sIIII', exe, sh+40*i)
    img[va:va+min(rsz,vsz if vsz else rsz)] = exe[raw:raw+min(rsz,vsz if vsz else rsz)]
    secs.append((name.rstrip(b'\0').decode(), va, vsz))
print(hex(base), hex(imgsz), secs)
# argc table from shims
argc = {}
for f in glob.glob('/home/claude/civ2vita/src/*.cpp'):
    for m in re.finditer(r'reg\(\s*\w+\s*,\s*"([A-Za-z0-9_]+)"\s*,\s*(\d+)', open(f).read()): argc[m.group(1)] = int(m.group(2))
cdecl = {'wsprintfA','wsprintfW','_snprintf','sprintf'}
def rva2s(r):
    e = img.find(b'\0', r); return img[r:e].decode('latin1')
out = []
d = imp_rva
while True:
    olt, ts, fc, nm, iat = struct.unpack_from('<IIIII', img, d)
    if nm == 0: break
    dll = rva2s(nm); t = olt or iat; k = 0
    while True:
        v = struct.unpack_from('<I', img, t+4*k)[0]
        if v == 0: break
        fn = ('#%d' % (v & 0xffff)) if v & 0x80000000 else rva2s(v+2)
        a = argc.get(fn, -1)
        out.append((base+iat+4*k, dll, fn, a, 1 if fn in cdecl else 0))
        k += 1
    d += 20
open('image.bin','wb').write(img)
with open('imports.txt','w') as f:
    for o in out: f.write('%x %s %s %d %d\n' % o)
print(len(out), 'imports,', sum(1 for o in out if o[3] < 0), 'ohne argc')
print([o[2] for o in out if o[3] < 0][:40])
