# usage: python3 -I utoc_paths.py FILE.utoc OUT.txt  -- dump full paths from the IoStore directory index
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:16] == b'-==--==--==--==-'
ver = d[16]
(hsize, n, nblk, blksz, ncm, cmlen, cbs, dirsz, nparts, cid) = struct.unpack_from('<IIIIIIIIIQ', d, 20)
flags = d[0x48 + 16] if False else None
# FGuid at 0x40, flags at 0x50, seeds count 0x54, partition size 0x58, without-hash count 0x60
flags = d[0x50]
nseeds, = struct.unpack_from('<I', d, 0x54)
nwo, = struct.unpack_from('<I', d, 0x60)
print('ver', ver, 'hsize', hsize, 'entries', n, 'blocks', nblk, 'flags', hex(flags), 'dirsz', dirsz, 'seeds', nseeds, 'nwo', nwo, file=sys.stderr)
p = hsize + n * 12 + n * 10
if ver >= 3: p += nseeds * 4
if ver >= 4: p += nwo * 4
p += nblk * blksz + ncm * cmlen
if flags & 4:  # signed
    hs, = struct.unpack_from('<i', d, p); p += 4 + hs * 2 + nblk * 20
if flags & 2: sys.exit('directory index encrypted')
b = d[p:p + dirsz]; q = 0
def u32():
    global q; v, = struct.unpack_from('<I', b, q); q += 4; return v
def fstr():
    global q; ln, = struct.unpack_from('<i', b, q); q += 4
    if ln < 0: s = b[q:q - ln * 2].decode('utf-16le'); q += -ln * 2
    else: s = b[q:q + ln].decode('latin1'); q += ln
    return s.rstrip('\0')
mount = fstr()
dirs = [tuple(u32() for _ in range(4)) for _ in range(u32())]
files = [tuple(u32() for _ in range(3)) for _ in range(u32())]
st = [fstr() for _ in range(u32())]
N = 0xFFFFFFFF
out = []
def walk(i, path):
    while i != N:
        name, child, sib, f = dirs[i]
        cur = path + (st[name] + '/' if name != N else '')
        while f != N:
            out.append(cur + st[files[f][0]]); f = files[f][1]
        walk(child, cur); i = sib
walk(0, mount)
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print(len(out), 'paths', file=sys.stderr)
