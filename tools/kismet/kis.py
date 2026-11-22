import struct,sys
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from zen import Zen,u32,i32
z=Zen(sys.argv[1]); RES={}
try:
    for l in open(sys.argv[0].rsplit('/',1)[0]+'/resolved.txt'):
        h,n=l.split(None,1); RES[int(h,16)]=n.strip()
except FileNotFoundError: pass
def objname(idx):
    if idx==0: return 'null'
    if idx>0: return 'export:'+z.exports[idx-1]['name']
    h=z.imports[-idx-1]; return RES.get(h,f'imp{-idx-1}:{h:016x}')
class D:
    def __init__(s,b,o,end): s.b=b;s.o=o;s.end=end;s.mem=0;s.out=[]
    def rd(s,f,n,m=None):
        v=struct.unpack_from(f,s.b,s.o)[0]; s.o+=n; s.mem+= n if m is None else m; return v
    def name(s): i=s.rd('<I',4);n=s.rd('<I',4,8); return z.nm(i,n)
    def obj(s): return objname(s.rd('<i',4,8))
    def path(s):
        c=s.rd('<i',4,0); ps=[s.name() for _ in range(c)]; s.mem-=12*c; s.rd('<i',4,0); s.mem+=8; return '.'.join(ps) or '<nullprop>'
    def params(s):
        a=[]
        while True:
            if s.b[s.o]==0x16: s.o+=1;s.mem+=1; return a
            a.append(s.expr())
    def expr(s):
        op=s.rd('<B',1); E=s.expr
        if op in (0x00,0x01,0x02,0x48,0x6C): return s.path()
        if op==0x04: return 'return '+E()
        if op==0x06: return f'goto {s.rd("<I",4)}'
        if op==0x07: t=s.rd('<I',4); return f'if not ({E()}) goto {t}'
        if op==0x09: s.rd('<H',2);s.rd('<B',1); return 'assert '+E()
        if op==0x0B: return 'nop'
        if op==0x0C: return f'nop{s.rd("<i",4)}'
        if op==0x0F: s.path(); v=E(); return f'{v} = {E()}'
        if op==0x11: p=s.path(); return f'bitfield {p} = {s.rd("<B",1)}'  # UE5 EX_BitFieldConst: property, uint8
        if op==0x12 or op==0x19 or op==0x1A:
            o=E(); s.rd('<I',4); s.path(); return f'{o}.{E()}'
        if op==0x13: c=s.obj(); return f'MetaCast<{c}>({E()})'
        if op in (0x14,0x43,0x44,0x5F,0x60,0x5C,0x62): a=E(); return f'{a} = {E()}' if op in (0x14,0x43,0x44,0x5F,0x60) else f'{a} op{op:x} {E()}'
        if op==0x15: return 'endparm'
        if op==0x17: return 'self'
        if op==0x18: s.rd('<I',4); return 'skip '+E()
        if op in (0x1B,0x45): n=s.name(); return f'{n}({", ".join(s.params())})'
        if op in (0x1C,0x46,0x68,0x63): f=s.obj(); return f'{f}({", ".join(s.params())})'
        if op==0x1D: return str(s.rd('<i',4))
        if op==0x1E: return repr(s.rd('<f',4))
        if op==0x1F:
            e=s.b.index(b'\0',s.o); t=s.b[s.o:e].decode('latin1'); s.mem+=e+1-s.o; s.o=e+1; return repr(t)
        if op==0x34:
            e=s.o
            while s.b[e:e+2]!=b'\0\0': e+=2
            t=s.b[s.o:e].decode('utf-16le'); s.mem+=e+2-s.o; s.o=e+2; return repr(t)
        if op==0x20: return s.obj()
        if op==0x21: return 'name:'+s.name()
        if op in (0x22,0x23): return 'vec'+str([s.rd('<d',8) for _ in range(3)])
        if op==0x41: return 'vec3f'+str([s.rd('<f',4) for _ in range(3)])
        if op==0x24: return str(s.rd('<B',1))
        if op==0x25: return '0'
        if op==0x26: return '1'
        if op==0x27: return 'true'
        if op==0x28: return 'false'
        if op==0x29:
            k=s.rd('<B',1)
            if k==0: return 'text:empty'
            if k==1: return f'text({E()},{E()},{E()})'
            if k==2: return f'text:invariant({E()})'
            if k==3: return f'text:literal({E()})'
            if k==4: return f'text:table({s.obj()},{E()},{E()})'
            return f'text?{k}'
        if op==0x2A: return 'None'
        if op==0x2B: return 'xform'+str([s.rd('<d',8) for _ in range(10)])
        if op==0x2C: return str(s.rd('<B',1))
        if op==0x2D: return 'NoInterface'
        if op==0x2E: c=s.obj(); return f'Cast<{c}>({E()})'
        if op==0x2F:
            c=s.obj(); s.rd('<i',4); a=[]
            while s.b[s.o]!=0x30: a.append(E())
            s.o+=1;s.mem+=1; return f'{c}{{{", ".join(a)}}}'
        if op in (0x31,):
            t=E(); a=[]
            while s.b[s.o]!=0x32: a.append(E())
            s.o+=1;s.mem+=1; return f'{t} = [{", ".join(a)}]'
        if op==0x33: return 'prop:'+s.path()
        if op==0x35: return str(s.rd('<q',8))
        if op==0x36: return str(s.rd('<Q',8))
        if op==0x37: return repr(s.rd('<d',8))
        if op==0x38: c=s.rd('<B',1); return f'conv{c}({E()})'
        if op in (0x39,0x3B):
            t=E(); s.rd('<i',4); a=[]; endb=0x3A if op==0x39 else 0x3C
            while s.b[s.o]!=endb: a.append(E())
            s.o+=1;s.mem+=1; return f'{t} = set/map[{", ".join(a)}]'
        if op in (0x3D,0x65):
            s.path(); s.rd('<i',4); a=[]; endb=0x3E if op==0x3D else 0x66
            while s.b[s.o]!=endb: a.append(E())
            s.o+=1;s.mem+=1; return f'[{", ".join(a)}]'
        if op==0x3F:
            s.path(); s.path(); s.rd('<i',4); a=[]
            while s.b[s.o]!=0x40: a.append(E())
            s.o+=1;s.mem+=1; return f'map[{", ".join(a)}]'
        if op==0x42: p=s.path(); return f'{E()}.{p}'
        if op==0x4B: return 'delegate:'+s.name()
        if op==0x4C: return f'push_flow {s.rd("<I",4)}'
        if op==0x4D: return 'pop_flow'
        if op==0x4E: return 'computed_goto '+E()
        if op==0x4F: return f'pop_flow_if_not ({E()})'
        if op==0x50: return 'bp'
        if op==0x51: return 'iface('+E()+')'
        if op in (0x52,0x54,0x55): c=s.obj(); return f'icast<{c}>({E()})'
        if op==0x53: return 'END'
        if op in (0x5A,0x5E): return 'trace'
        if op==0x5B: return f'skipconst {s.rd("<I",4)}'
        if op==0x5D: return 'clear '+E()
        if op==0x61: n=s.name(); a=E(); return f'bind {n} {a} {E()}'
        if op==0x64: p=s.path(); return f'{p} = {E()}'
        if op==0x67: return 'soft('+E()+')'
        if op==0x69:
            n=s.rd('<H',2); s.rd('<I',4); idx=E(); cs=[]
            for _ in range(n): k=E(); s.rd('<I',4); cs.append(f'{k}:{E()}')
            return f'switch({idx}){{{"; ".join(cs)}; default:{E()}}}'
        if op==0x6A:
            k=s.rd('<B',1)
            if k==4: s.rd('<i',4)
            return 'instr'
        if op==0x6B: a=E(); return f'{a}[{E()}]'
        if op==0x6D: return 'fieldpath('+E()+')'
        raise ValueError(f'op {op:#x} at {s.o-1}')
def find_script(b,s,e):
    for p in range(s,e-8):
        a=i32(b,p); n=i32(b,p+4)
        if 0<a and 0<n and 0<=e-(p+8+n)<=20 and b[p+8+n-1]==0x53: return p+8,p+8+n
    return None
only=sys.argv[2:] 
for ex in z.exports:
    if only and ex['name'] not in only: continue
    s=z.hdr+ex['off']; e=s+ex['size']; r=find_script(z.b,s,e)
    if not r: continue
    print(f'=== {ex["name"]}')
    d=D(z.b,r[0],r[1])
    while d.o<r[1]:
        m=d.mem
        try: t=d.expr()
        except Exception as x: print('  ERR',x); break
        print(f'  {m:5d}: {t}')
        if t=='END': break
