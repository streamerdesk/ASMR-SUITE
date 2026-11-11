#!/usr/bin/env python3
"""Resuelve dependencias de paquetes Debian a partir de un indice Packages (sin apt)."""
import re, subprocess, sys
pk={}; cur={}
for line in open('Packages', encoding='utf-8', errors='replace'):
    if line.strip()=='' :
        if 'Package' in cur: pk.setdefault(cur['Package'],cur)
        cur={}; continue
    if line[0] in ' \t': continue
    k,_,v=line.partition(':'); cur[k]=v.strip()
prov={}
for n,p in pk.items():
    for pr in p.get('Provides','').split(','):
        pr=pr.strip().split(' ')[0]
        if pr: prov.setdefault(pr,n)
installed=set(subprocess.run(['dpkg-query','-W','-f','${Package}\n'],capture_output=True,text=True).stdout.split())
todo=sys.argv[1:]; seen=set(); out=[]
while todo:
    n=todo.pop()
    if n in seen: continue
    seen.add(n)
    if n in installed: continue
    if n not in pk:
        if n in prov: todo.append(prov[n]); continue
        print('#missing',n,file=sys.stderr); continue
    out.append(pk[n]['Filename'])
    for alt in pk[n].get('Depends','').split(',')+pk[n].get('Pre-Depends','').split(','):
        alt=alt.strip()
        if not alt: continue
        name=alt.split('|')[0].strip().split(' ')[0].split(':')[0]
        todo.append(name)
print('\n'.join(out))
