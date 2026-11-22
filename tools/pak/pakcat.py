# usage: python3 -I pakcat.py PAK ENTRY_OFFSET OUT REPO  -- decode one Oodle .pak entry (offset from pakls.py) to OUT via ../iostore/oodec.exe.
import struct,sys,os,subprocess
pak,off,out=sys.argv[1],int(sys.argv[2]),sys.argv[3]
f=open(pak,'rb'); f.seek(off); h=f.read(4096)
o0,sz,usz,meth=struct.unpack_from('<qqqI',h,0); q=28+20
blocks=[]
if meth:
    n,=struct.unpack_from('<I',h,q); q+=4
    for i in range(n):
        s,e=struct.unpack_from('<qq',h,q); q+=16; blocks.append((s,e))
flags=h[q]; bs,=struct.unpack_from('<I',h,q+1)
print('size',sz,'usz',usz,'meth',meth,'blocks',len(blocks),'flags',flags,'bs',bs,file=sys.stderr)
jobs=out+'.jobs'
with open(jobs,'wb') as j:
    rem=usz
    for s,e in blocks:
        f.seek(off+s); d=f.read(e-s); u=min(bs,rem); rem-=u
        j.write(struct.pack('<II',len(d),u)); j.write(d)
R=os.path.dirname(os.path.abspath(pak)); 
repo=sys.argv[4]
exe='Z:'+repo+'/game/StarshipTroopers_UBW/Binaries/Win64/StarshipTroopers_UBW-Win64-Shipping.exe'
env=dict(os.environ,WINEPREFIX=os.path.expanduser('~/.cache/ubw-iox-wpfx'),WINEDEBUG='-all')
r=subprocess.run(['wine',repo+'/tools/iostore/oodec.exe',exe,jobs,out],env=env,capture_output=True,text=True)
print(r.returncode,r.stderr[-300:],file=sys.stderr)
