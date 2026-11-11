#!/usr/bin/env python3
"""Construye un instalador .pkg plano de macOS (con selector de componentes) SIN macOS:
cpio 'odc' + gzip (Payload/Scripts), Bom con mkbom (bomutils) y archivo xar escrito en Python.
Formato equivalente al de pkgbuild + productbuild."""
import gzip, hashlib, io, os, stat, struct, subprocess, time, zlib
from xml.sax.saxutils import escape


# ---------------------------------------------------------------- cpio odc
def cpio_odc(root, uid=0, gid=80):
    out = io.BytesIO()
    ino = 1

    def entry(path, rel):
        nonlocal ino
        st = os.lstat(path)
        mode = st.st_mode
        data = b''
        if stat.S_ISREG(mode):
            data = open(path, 'rb').read()
        elif stat.S_ISLNK(mode):
            data = os.readlink(path).encode()
        name = rel.encode() + b'\0'
        hdr = b'070707' + b'%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' % (
            0, ino, mode, uid, gid, 1, 0, int(st.st_mtime), len(name), len(data))
        out.write(hdr + name + data)
        ino += 1

    entry(root, '.')
    for d, dirs, files in os.walk(root):
        dirs.sort()
        for n in sorted(dirs) + sorted(files):
            p = os.path.join(d, n)
            entry(p, './' + os.path.relpath(p, root))
    name = b'TRAILER!!!\0'
    out.write(b'070707' + b'%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o' % (0, 0, 0, 0, 0, 1, 0, 0, len(name), 0) + name)
    return out.getvalue()


def gz(data):
    b = io.BytesIO()
    with gzip.GzipFile(fileobj=b, mode='wb', mtime=0, compresslevel=9) as g:
        g.write(data)
    return b.getvalue()


# ---------------------------------------------------------------- xar
class Xar:
    def __init__(self):
        self.root = []  # (name, bytes) o (name, [hijos])

    def add(self, path, data):
        parts = path.split('/')
        cur = self.root
        for p in parts[:-1]:
            for n, c in cur:
                if n == p and isinstance(c, list):
                    cur = c
                    break
            else:
                nl = []
                cur.append((p, nl))
                cur = nl
        cur.append((parts[-1], data))

    def write(self, out_path):
        heap = io.BytesIO()
        heap.write(b'\0' * 20)  # checksum del TOC (sha1) en el offset 0
        ids = [0]
        now = time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime())

        def node(name, c):
            ids[0] += 1
            fid = ids[0]
            if isinstance(c, list):
                inner = ''.join(node(n, cc) for n, cc in c)
                return ('<file id="%d"><name>%s</name><type>directory</type><mode>0755</mode><uid>0</uid><gid>80</gid>'
                        '%s</file>' % (fid, escape(name), inner))
            off = heap.tell()
            heap.write(c)
            h = hashlib.sha1(c).hexdigest()
            return ('<file id="%d"><name>%s</name><type>file</type><mode>0644</mode><uid>0</uid><gid>80</gid>'
                    '<data><length>%d</length><offset>%d</offset><size>%d</size>'
                    '<encoding style="application/octet-stream"/>'
                    '<extracted-checksum style="sha1">%s</extracted-checksum>'
                    '<archived-checksum style="sha1">%s</archived-checksum></data></file>'
                    % (fid, escape(name), len(c), off, len(c), h, h))

        body = ''.join(node(n, c) for n, c in self.root)
        toc = ('<?xml version="1.0" encoding="UTF-8"?>\n<xar><toc><checksum style="sha1"><offset>0</offset>'
               '<size>20</size></checksum><creation-time>%s</creation-time>%s</toc></xar>' % (now, body)).encode()
        ztoc = zlib.compress(toc, 9)
        h = bytearray(heap.getvalue())
        h[0:20] = hashlib.sha1(ztoc).digest()
        with open(out_path, 'wb') as f:
            f.write(struct.pack('>IHHQQI', 0x78617221, 28, 1, len(ztoc), len(toc), 1))
            f.write(ztoc)
            f.write(bytes(h))


# ---------------------------------------------------------------- componentes
def component(payload_root, scripts_root, identifier, version, install_location, mkbom):
    bom = payload_root.rstrip('/') + '.bom'
    subprocess.run([mkbom, '-u', '0', '-g', '80', payload_root, bom], check=True, capture_output=True)
    nfiles, size = 0, 0
    for d, dirs, files in os.walk(payload_root):
        nfiles += len(dirs) + len(files)
        size += sum(os.path.getsize(os.path.join(d, f)) for f in files)
    info = ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<pkg-info overwrite-permissions="true" relocatable="false" identifier="%s" postinstall-action="none" '
            'version="%s" format-version="2" generator-version="streamerdesk-macpkg" install-location="%s" auth="root">'
            '<payload numberOfFiles="%d" installKBytes="%d"/>'
            '<scripts><postinstall file="./postinstall"/></scripts>'
            '<bundle-version/><upgrade-bundle/><update-bundle/><atomic-update-bundle/><strict-identifier/><relocate/>'
            '</pkg-info>\n' % (escape(identifier), escape(version), escape(install_location), nfiles + 1, (size + 1023) // 1024))
    files = {
        'PackageInfo': info.encode(),
        'Bom': open(bom, 'rb').read(),
        'Payload': gz(cpio_odc(payload_root)),
        'Scripts': gz(cpio_odc(scripts_root)),
    }
    os.remove(bom)
    return files, (size + 1023) // 1024
