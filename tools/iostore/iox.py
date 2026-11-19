# usage: python3 -I iox.py UTOC UCAS REGEX OUTDIR [HEAD_BYTES]  -- extract raw (zen) packages whose path matches REGEX
# HEAD_BYTES (optional) keeps only the first N bytes of each package: enough for the name map and imports of large maps.
import struct, sys, re, os, subprocess
utoc, ucas, rx, outdir = sys.argv[1:5]
head = int(sys.argv[5]) if len(sys.argv) > 5 else 0
S = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(S))
GAME_EXE = 'Z:' + REPO + '/game/StarshipTroopers_UBW/Binaries/Win64/StarshipTroopers_UBW-Win64-Shipping.exe'
WPFX = os.environ.get('UBW_IOX_WINEPREFIX', os.path.expanduser('~/.cache/ubw-iox-wpfx'))
d = open(utoc, 'rb').read()
(hsize, n, nblk, blksz, ncm, cmlen, cbs, dirsz, nparts, cid) = struct.unpack_from('<IIIIIIIIIQ', d, 20)
nseeds, = struct.unpack_from('<I', d, 0x54)
p_ids = hsize; p_ol = p_ids + n * 12; p_blk = p_ol + n * 10 + nseeds * 4
p_dir = p_blk + nblk * blksz + ncm * cmlen
def chunk_ol(i):
    b = d[p_ol + i * 10: p_ol + i * 10 + 10]
    return int.from_bytes(b[:5], 'big'), int.from_bytes(b[5:], 'big')
def blk(i):
    b = d[p_blk + i * 12: p_blk + i * 12 + 12]
    return int.from_bytes(b[:5], 'little'), int.from_bytes(b[5:8], 'little'), int.from_bytes(b[8:11], 'little'), b[11]
# directory index -> (path, toc index)
b = d[p_dir:p_dir + dirsz]; q = 0
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
N = 0xFFFFFFFF; hits = []; R = re.compile(rx)
stack = [(0, mount)]
while stack:
    i, path = stack.pop()
    while i != N:
        name, child, sib, f = dirs[i]
        cur = path + (st[name] + '/' if name != N else '')
        while f != N:
            full = cur + st[files[f][0]]
            if R.search(full): hits.append((full, files[f][2]))
            f = files[f][1]
        stack.append((child, cur)); i = sib
if not hits: sys.exit('no match')
os.makedirs(outdir, exist_ok=True)
jobs = os.path.join(outdir, 'jobs.bin'); raw = os.path.join(outdir, 'raw.bin')
sizes = []
with open(ucas, 'rb') as u, open(jobs, 'wb') as j:
    for full, ti in hits:
        off, ln = chunk_ol(ti)
        if head: ln = min(ln, head)
        first = off // cbs; last = (off + ln - 1) // cbs; total = 0
        for bi in range(first, last + 1):
            co, cs, us, meth = blk(bi); u.seek(co); data = u.read(cs)
            j.write(struct.pack('<II', cs, us if meth else cs)); j.write(data); total += us
        sizes.append((full, off % cbs, ln, total))
env = dict(os.environ, WINEPREFIX=WPFX, WINEDEBUG='-all')
r = subprocess.run(['wine', os.path.join(S, 'oodec.exe'), GAME_EXE, jobs, raw], env=env, capture_output=True, text=True)
print(r.stderr.strip()[-400:], file=sys.stderr)
if r.returncode: sys.exit('oodec failed')
rawd = open(raw, 'rb').read(); pos = 0
for full, skip, ln, total in sizes:
    name = full.split('/')[-1]
    open(os.path.join(outdir, name), 'wb').write(rawd[pos + skip: pos + skip + ln]); pos += total
    print(full, ln)
os.remove(jobs); os.remove(raw)
