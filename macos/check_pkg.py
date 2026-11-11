#!/usr/bin/env python3
"""Verifica un .pkg plano: cabecera xar, checksum del TOC, checksums de cada archivo, XML de Distribution y
PackageInfo, y que el Bom de cada componente lista exactamente las rutas del Payload (cpio odc)."""
import gzip, hashlib, os, struct, sys, tempfile, zlib
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_bom import read_bom  # noqa: E402


def cpio_names(data):
    p, names = 0, []
    while True:
        assert data[p:p + 6] == b'070707', 'cpio odc invalido'
        mode = int(data[p + 18:p + 24], 8)
        nlen = int(data[p + 59:p + 65], 8)
        fsize = int(data[p + 65:p + 76], 8)
        name = data[p + 76:p + 76 + nlen - 1].decode()
        p += 76 + nlen + fsize
        if name == 'TRAILER!!!':
            return names
        names.append((name, mode))


def main(pkg):
    d = open(pkg, 'rb').read()
    magic, hsize, ver, zlen, ulen, alg = struct.unpack('>IHHQQI', d[:28])
    assert magic == 0x78617221 and hsize == 28 and alg == 1
    ztoc = d[28:28 + zlen]
    toc = zlib.decompress(ztoc)
    assert len(toc) == ulen
    heap = 28 + zlen
    assert d[heap:heap + 20] == hashlib.sha1(ztoc).digest(), 'checksum TOC'
    root = ET.fromstring(toc).find('toc')
    files = {}

    def walk(el, prefix):
        for f in el.findall('file'):
            name = prefix + f.find('name').text
            if f.find('type').text == 'directory':
                walk(f, name + '/')
            else:
                dt = f.find('data')
                off, ln = int(dt.find('offset').text), int(dt.find('length').text)
                blob = d[heap + off:heap + off + ln]
                assert hashlib.sha1(blob).hexdigest() == dt.find('archived-checksum').text, name
                files[name] = blob
    walk(root, '')
    print('  xar OK: %d archivos, checksums sha1 correctos' % len(files))
    dist = ET.fromstring(files['Distribution'])
    choices = dist.findall('choice')
    refs = {r.get('id'): r.text for r in dist.findall('pkg-ref') if r.text}
    print('  Distribution OK: %d opciones (start_selected=%s), customize=%s, hostArchitectures=%s' % (
        len(choices), ','.join(sorted({c.get('start_selected') for c in choices})),
        dist.find('options').get('customize'), dist.find('options').get('hostArchitectures')))
    for lang in ('en', 'es'):
        for r in ('welcome.html', 'conclusion.html', 'LICENSE.txt'):
            assert 'Resources/%s.lproj/%s' % (lang, r) in files
    ok = True
    for c in choices:
        ref = c.find('pkg-ref').get('id')
        comp = refs[ref].lstrip('#')
        info = ET.fromstring(files[comp + '/PackageInfo'])
        names = cpio_names(gzip.decompress(files[comp + '/Payload']))
        scripts = cpio_names(gzip.decompress(files[comp + '/Scripts']))
        with tempfile.NamedTemporaryFile(delete=False) as t:
            t.write(files[comp + '/Bom'])
        bom, _ = read_bom(t.name)
        os.unlink(t.name)
        same = sorted(n for n, _ in names) == sorted(e[0] for e in bom)
        post = [m for n, m in scripts if n == './postinstall']
        good = same and post and post[0] & 0o111 and info.get('identifier') == ref
        ok &= bool(good)
        print('  [%s] %-28s %s v%s -> %s | Bom=Payload (%d rutas) | postinstall %o' % (
            'OK' if good else 'FALLO', c.get('title'), ref, info.get('version'), info.get('install-location'),
            len(names), post[0] if post else 0))
    return ok


if __name__ == '__main__':
    sys.exit(0 if main(sys.argv[1]) else 1)
