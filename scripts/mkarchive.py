#!/usr/bin/env python3
"""Crea zip / tar.gz reproducibles conservando permisos de ejecucion.
  mkarchive.py zip <out.zip> <dir> <prefijo>
  mkarchive.py tgz <out.tar.gz> <dir> <prefijo>
  mkarchive.py src <out.zip> <repo> <prefijo>   (excluye build/, dist/, .deps/, .git/)
"""
import os, sys, tarfile, zipfile

mode, out, src, prefix = sys.argv[1:5]
EXCL = {'build', 'dist', '.deps', '.git', '__pycache__'}


def walk(root, exclude=False):
    for d, dirs, files in os.walk(root):
        dirs[:] = sorted(x for x in dirs if not (exclude and x in EXCL))
        for f in sorted(files):
            p = os.path.join(d, f)
            if os.path.islink(p):
                continue
            yield p, os.path.relpath(p, root)


if mode in ('zip', 'src'):
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, rel in walk(src, mode == 'src'):
            arc = os.path.join(prefix, rel) if prefix else rel
            zi = zipfile.ZipInfo(arc, date_time=(2026, 11, 11, 0, 0, 0))
            st = os.stat(p).st_mode
            zi.external_attr = ((0o100755 if st & 0o111 else 0o100644) << 16)
            zi.create_system = 3
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, open(p, 'rb').read())
elif mode == 'tgz':
    with tarfile.open(out, 'w:gz') as t:
        for p, rel in walk(src):
            ti = t.gettarinfo(p, os.path.join(prefix, rel))
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = 'root'
            ti.mtime = 1794355200
            with open(p, 'rb') as f:
                t.addfile(ti, f)
print('creado', out, os.path.getsize(out), 'bytes')
