#!/usr/bin/env python3
# Card 136: split the host-event / client-row gap of a pair run by cause (static, from the logs alone).
# Run 4 host lines carry "pawn <instance>": unmatched events with it are counted per pawn instance (the client's
# pawn on the host is the filtered one; any other pawn's count is a gap).
# Usage: tools/fx_gap.py HOST_LOG CLIENT_LOG [--p1 MS] [--d0 MS] [--p2 MS] [--q MS]
#   p1: host first Pong; d0: client disconnect; p2: host first Pong of the rejoin; q: host quit (host t= clock).
#   Windows left unset are not split out (hb64: --p1 225836674 --d0 225943202 --p2 225957373 --q 226056406).
import re,collections,argparse
ap=argparse.ArgumentParser(); ap.add_argument('host_log'); ap.add_argument('client_log')
for k in ('p1','d0','p2','q'): ap.add_argument('--'+k,type=int,default=None)
A=ap.parse_args()
H=open(A.host_log,errors='replace').read().splitlines()
C=open(A.client_log,errors='replace').read().splitlines()
ev={}; pe=re.compile(r'card136: host fx (\d+) t=(\d+) (\S+) (\S+) (.*)$')
for l in H:
    m=pe.search(l)
    if m: ev[int(m.group(1))]=(int(m.group(2)),m.group(3),m.group(4),m.group(5))
rows=collections.Counter(); first={}; pc=re.compile(r'\[ *(\d+) ms\].*card136: client fx (\d+) ')
for l in C:
    m=pc.search(l)
    if m:
        i=int(m.group(2)); rows[i]+=1; first.setdefault(i,int(m.group(1)))
print('host events',len(ev),'client rows',sum(rows.values()),'distinct ids',len(rows),'line gap',len(ev)-sum(rows.values()))
dups=[i for i,v in rows.items() if v>1]; print('ids with 2 rows',len(dups),'extra rows',sum(rows[i]-1 for i in dups))
BIG=1<<62
P1=A.p1 if A.p1 is not None else -1
D0,P2=(A.d0,A.p2) if A.d0 is not None and A.p2 is not None else (BIG,BIG)
Q=A.q if A.q is not None else BIG
cat=collections.OrderedDict((k,0) for k in ['before first Pong (host alone)','disconnect (client Browse to host first Pong of rejoin)','after host quit closed the connection','gore decal, card 132 D rows','filtered: owner is the client pawn (cue on its ASC)','filtered: NonReplicated cue, instigator the client pawn','local_only','run 6 local trail (projectile or gore part op)','run 6 no asset (decal with no material)','first person drop','send failure','client parse fail: "Box Component" (space in comp name)','unexplained'])
unex=[]
for i in sorted(set(ev)-set(rows)):
    t,pt,a,r=ev[i]
    if t<P1: k='before first Pong (host alone)'
    elif D0<=t<P2: k='disconnect (client Browse to host first Pong of rejoin)'
    elif t>=Q: k='after host quit closed the connection'
    elif 'Decal' in pt and 'Gore' in a: k='gore decal, card 132 D rows'
    elif pt.startswith('Niagara') and re.search(r' owner (ADProjectile|STGorePart\S*) ', r): k='run 6 local trail (projectile or gore part op)'
    elif 'Decal' in pt and a in ('None', '-'): k='run 6 no asset (decal with no material)'
    elif 'attach Box Component/' in r: k='client parse fail: "Box Component" (space in comp name)'
    elif ' pawn ' in r and not re.search(r' instigator (?!BP_PlayerCharacterMI_C |null )\S+ \(', r):
        k='filtered (pawn field): pawn '+r.rsplit(' pawn ',1)[1].split()[0]; cat.setdefault(k,0)
    elif pt=='ExecuteGameplayCue' and 'owner BP_PlayerCharacterMI_C' in r: k='filtered: owner is the client pawn (cue on its ASC)'
    elif pt=='ExecuteGameplayCue_NonReplicated' and 'instigator BP_PlayerCharacterMI_C' in r: k='filtered: NonReplicated cue, instigator the client pawn'
    else: k='unexplained'; unex.append(i)
    cat[k]+=1
for k,v in cat.items(): print(f'{v:6d}  {k}')
print('sum',sum(cat.values()))
for i in unex[:10]: print(i,ev[i])
# dup rows: replay? time of 2nd row vs P2
pp=collections.Counter()
for i in dups: pp['made before drop' if ev[i][0]<D0 else 'made after']+=1
print('dup ids',pp)
# gore split pre/post drop
print('gore pre-drop',sum(1 for i,(t,pt,a,r) in ev.items() if 'Decal' in pt and 'Gore' in a and t<D0),'post-rejoin',sum(1 for i,(t,pt,a,r) in ev.items() if 'Decal' in pt and 'Gore' in a and t>=P2))
print('flak box pre/post',sum(1 for i,(t,pt,a,r) in ev.items() if 'Box Component/' in r and t<D0),sum(1 for i,(t,pt,a,r) in ev.items() if 'Box Component/' in r and t>=P2))
