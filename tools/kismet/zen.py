import struct,sys
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def i32(b,o): return struct.unpack_from('<i',b,o)[0]
def u64(b,o): return struct.unpack_from('<Q',b,o)[0]
class Zen:
    def __init__(s,path):
        b=s.b=open(path,'rb').read()
        s.hdr=u32(b,4); s.cooked=u32(b,0x14)
        s.imp_off=u32(b,0x1c); s.exp_off=u32(b,0x20); s.bundle_off=u32(b,0x24)
        o=0x3c; n=u32(b,o); o+=16+8*n
        lens=[struct.unpack_from('>h',b,o+2*i)[0] for i in range(n)]; o+=2*n
        s.names=[]
        for L in lens:
            if L<0: L=-L; s.names.append(b[o:o+2*L].decode('utf-16le')); o+=2*L
            else: s.names.append(b[o:o+L].decode('latin1')); o+=L
        s.imports=[u64(b,s.imp_off+8*i) for i in range((s.exp_off-s.imp_off)//8)]
        s.exports=[]
        for i in range((s.bundle_off-s.exp_off)//72):
            e=s.exp_off+72*i
            s.exports.append(dict(off=u64(b,e),size=u64(b,e+8),name=s.nm(u32(b,e+16),u32(b,e+20)),
              outer=u64(b,e+24),cls=u64(b,e+32),sup=u64(b,e+40),tmpl=u64(b,e+48),flags=u32(b,e+64)))
    def nm(s,i,n=0):
        if i>=len(s.names): return f'?name{i}'
        return s.names[i]+(f'_{n-1}' if n else '')
if __name__=='__main__':
    z=Zen(sys.argv[1])
    print('hdr',hex(z.hdr),'cooked',hex(z.cooked),'names',len(z.names),'imports',len(z.imports),'size',len(z.b))
    for i,e in enumerate(z.exports): print(i,e['name'],hex(e['off']),e['size'],hex(e['cls']),hex(e['outer']))
