# usage: python3 -I pakls.py PAK REGEX  -- list entries of a v11 .pak (path, offset, raw size, stored size, method, blocks, block size).
# Index offset/size are this build's pakchunk0-Windows.pak footer values. Compressed entries are Oodle: decode with ../iostore/oodec.exe.
import sys,struct,re
f=open(sys.argv[1],'rb'); rx=re.compile(sys.argv[2])
def rd(o,n): f.seek(o); return f.read(n)
idx=rd(143183304,89874); q=0
def u(fmt):
    global q,buf; v=struct.unpack_from(fmt,buf,q); q+=struct.calcsize(fmt); return v[0]
def fs():
    global q; n=u('<i')
    if n<0: s=buf[q:q-2*n].decode('utf-16le'); q+=-2*n
    else: s=buf[q:q+n].decode('latin1'); q+=n
    return s.rstrip('\0')
buf=idx; mount=fs(); n=u('<i'); seed=u('<Q')
if u('<I'): po=u('<q'); ps=u('<q'); q+=20
assert u('<I'); do=u('<q'); ds=u('<q'); q+=20
esz=u('<I'); enc=buf[q:q+esz]; q+=esz
print('mount',mount,'files',n)
buf=rd(do,ds); q=0
for _ in range(u('<I')):
    d=fs()
    for _ in range(u('<I')):
        fn=fs(); eo=u('<i')
        p=d+fn
        if not rx.search(p): continue
        bits,=struct.unpack_from('<I',enc,eo); o=eo+4
        if bits&0x3f==0x3f: o+=4
        if bits&(1<<31): off,=struct.unpack_from('<I',enc,o); o+=4
        else: off,=struct.unpack_from('<Q',enc,o); o+=8
        if bits&(1<<30): usz,=struct.unpack_from('<I',enc,o); o+=4
        else: usz,=struct.unpack_from('<Q',enc,o); o+=8
        cm=(bits>>23)&0x3f; nb=(bits>>6)&0xffff; bsz=bits&0x3f
        if cm:
            if bits&(1<<29): csz,=struct.unpack_from('<I',enc,o); o+=4
            else: csz,=struct.unpack_from('<Q',enc,o); o+=8
        else: csz=usz
        print(p,off,usz,csz,cm,nb,bsz<<11 if bsz!=0x3f else '?')
