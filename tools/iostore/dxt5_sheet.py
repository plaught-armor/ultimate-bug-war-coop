"""Decode top mip of DXT5 UTexture2D zen packages; build per-anim contact sheets."""
import os, re, struct, sys
from collections import defaultdict
import numpy as np
from PIL import Image, ImageDraw

def top_mip(b):
    """Locate mip0 by the (W,H,1) triple that follows its data. Returns (w,h,bytes)."""
    for w in (1024, 512, 256, 128):
        size = (w // 4) * (w // 4) * 16
        pat = struct.pack('<III', w, w, 1)
        pos = b.find(pat)
        while pos >= 0 and pos - size < 0:
            pos = b.find(pat, pos + 1)
        if pos >= 0:
            return w, w, b[pos - size:pos]
    raise ValueError('no mip triple')

def decode_bc3(w, h, data):
    blk = np.frombuffer(data, dtype=np.uint8).reshape(-1, 16)
    a0 = blk[:, 0].astype(np.int32); a1 = blk[:, 1].astype(np.int32)
    abits = np.zeros(len(blk), dtype=np.uint64)
    for k in range(6):
        abits |= blk[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    pal_a = np.zeros((len(blk), 8), dtype=np.int32)
    pal_a[:, 0] = a0; pal_a[:, 1] = a1
    big = a0 > a1
    for i in range(1, 7):
        pal_a[:, 1 + i] = np.where(big, ((7 - i) * a0 + i * a1) // 7, 0)
    for i in range(1, 5):
        pal_a[:, 1 + i] = np.where(big, pal_a[:, 1 + i], ((5 - i) * a0 + i * a1) // 5)
    pal_a[:, 6] = np.where(big, pal_a[:, 6], 0); pal_a[:, 7] = np.where(big, pal_a[:, 7], 255)
    aidx = np.stack([((abits >> np.uint64(3 * p)) & np.uint64(7)).astype(np.int64) for p in range(16)], 1)
    alpha = np.take_along_axis(pal_a, aidx, 1)
    c0 = blk[:, 8].astype(np.int32) | (blk[:, 9].astype(np.int32) << 8)
    c1 = blk[:, 10].astype(np.int32) | (blk[:, 11].astype(np.int32) << 8)
    def rgb(c):
        return np.stack([((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31], 1)
    p0 = rgb(c0); p1 = rgb(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3], 1)
    cbits = blk[:, 12].astype(np.uint32) | (blk[:, 13].astype(np.uint32) << 8) | (blk[:, 14].astype(np.uint32) << 16) | (blk[:, 15].astype(np.uint32) << 24)
    cidx = np.stack([((cbits >> (2 * p)) & 3).astype(np.int64) for p in range(16)], 1)
    col = np.take_along_axis(pal, cidx[:, :, None].repeat(3, 2), 1)
    px = np.concatenate([col, alpha[:, :, None]], 2).astype(np.uint8)
    bw = w // 4
    return px.reshape(h // 4, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, 4)

def main(src, out, cell=128):
    groups = defaultdict(list)
    for f in sorted(os.listdir(src)):
        m = re.match(r'T_(.+)_F_(\d+)_BC\.uasset$', f)
        if m:
            groups[m.group(1)].append((int(m.group(2)), f))
    for name, frames in sorted(groups.items()):
        frames.sort()
        n = len(frames); cols = min(n, 10); rows = (n + cols - 1) // cols
        sheet = Image.new('RGB', (cols * cell, rows * cell + 14), (70, 90, 70))
        dr = ImageDraw.Draw(sheet); dr.text((2, 1), f'{name} ({n})', fill=(255, 255, 0))
        for i, (idx, f) in enumerate(frames):
            try:
                w, h, data = top_mip(open(os.path.join(src, f), 'rb').read())
                im = Image.fromarray(decode_bc3(w, h, data), 'RGBA').resize((cell, cell))
            except Exception as e:
                print('fail', f, e); continue
            bg = Image.new('RGB', (cell, cell), (70, 90, 70)); bg.paste(im, (0, 0), im)
            sheet.paste(bg, ((i % cols) * cell, 14 + (i // cols) * cell))
            dr.text(((i % cols) * cell + 2, 14 + (i // cols) * cell), str(idx), fill=(255, 255, 255))
        sheet.save(os.path.join(out, name + '.png'))
    print('sheets', len(groups))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
