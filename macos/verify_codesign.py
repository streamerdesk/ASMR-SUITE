#!/usr/bin/env python3
"""Verificacion independiente (sin macOS) de la firma ad-hoc de un .plugin:
por cada slice comprueba los hashes de pagina del CodeDirectory, los slots especiales Info.plist y
CodeResources, y que CodeResources sella cada archivo de Resources con su sha256 correcto."""
import base64, hashlib, os, plistlib, struct, sys

LC_CODE_SIGNATURE = 0x1D


def slices(data):
    magic = struct.unpack('>I', data[:4])[0]
    if magic == 0xCAFEBABE:
        n = struct.unpack('>I', data[4:8])[0]
        for i in range(n):
            ct, cs, off, size, al = struct.unpack('>iiIII', data[8 + 20 * i:28 + 20 * i])
            yield ct, data[off:off + size]
    else:
        yield struct.unpack('<i', data[4:8])[0], data


def check_slice(m, info_hash, res_hash):
    ncmds = struct.unpack('<I', m[16:20])[0]
    p = 32
    sig = None
    for _ in range(ncmds):
        cmd, size = struct.unpack('<II', m[p:p + 8])
        if cmd == LC_CODE_SIGNATURE:
            sig = struct.unpack('<II', m[p + 8:p + 16])
        p += size
    if not sig:
        return ['sin LC_CODE_SIGNATURE']
    off, size = sig
    sb = m[off:off + size]
    magic, length, count = struct.unpack('>III', sb[:12])
    errs = []
    if magic != 0xFADE0CC0:
        return ['superblob invalido']
    cd = None
    for i in range(count):
        typ, boff = struct.unpack('>II', sb[12 + 8 * i:20 + 8 * i])
        if typ == 0:
            cd = sb[boff:]
    (cmagic, clen, ver, flags, hashoff, identoff, nspecial, ncode, codelimit, hsize, htype, plat, pgshift) = \
        struct.unpack('>IIIIIIIIIBBBB', cd[:40])
    if cmagic != 0xFADE0C02 or htype != 2 or hsize != 32:
        return ['CodeDirectory inesperado']
    if not flags & 0x2:
        errs.append('no es ad-hoc')
    if codelimit != off:
        errs.append('codeLimit %d != offset firma %d' % (codelimit, off))
    pg = 1 << pgshift
    for i in range(ncode):
        want = cd[hashoff + 32 * i:hashoff + 32 * (i + 1)]
        got = hashlib.sha256(m[i * pg:min((i + 1) * pg, codelimit)]).digest()
        if want != got:
            errs.append('hash de pagina %d no coincide' % i)
            break
    special = {k: cd[hashoff - 32 * k:hashoff - 32 * (k - 1)] for k in range(1, nspecial + 1)}
    if special.get(1) != info_hash:
        errs.append('slot Info.plist no coincide')
    if special.get(3) != res_hash:
        errs.append('slot CodeResources no coincide')
    ident = cd[identoff:cd.index(b'\0', identoff)].decode()
    return errs, ident, ncode


def main(bundle):
    c = os.path.join(bundle, 'Contents')
    exe = os.path.join(c, 'MacOS', os.listdir(os.path.join(c, 'MacOS'))[0])
    info = open(os.path.join(c, 'Info.plist'), 'rb').read()
    cr_raw = open(os.path.join(c, '_CodeSignature', 'CodeResources'), 'rb').read()
    ok = True
    for ct, m in slices(open(exe, 'rb').read()):
        arch = {0x0100000C: 'arm64', 0x01000007: 'x86_64'}.get(ct, hex(ct))
        r = check_slice(m, hashlib.sha256(info).digest(), hashlib.sha256(cr_raw).digest())
        if isinstance(r, list):
            print('  %s: FALLO %s' % (arch, r)); ok = False; continue
        errs, ident, n = r
        print('  %s: %s (id %s, %d paginas verificadas, ad-hoc)' % (arch, 'OK' if not errs else 'FALLO ' + '; '.join(errs), ident, n))
        ok &= not errs
    cr = plistlib.loads(cr_raw)
    files2 = cr.get('files2', {})
    sealed = 0
    for root, _, fs in os.walk(c):
        for f in fs:
            full = os.path.join(root, f)
            rel = os.path.relpath(full, c)
            if rel.startswith(('MacOS/', '_CodeSignature/')) or rel in ('Info.plist', 'PkgInfo'):
                continue
            e = files2.get(rel)
            h = e.get('hash2') if isinstance(e, dict) else None
            if h is None or h != hashlib.sha256(open(full, 'rb').read()).digest():
                print('  recurso sin sellar o alterado: %s' % rel); ok = False
            else:
                sealed += 1
    print('  CodeResources: %d recursos sellados con sha256 correcto' % sealed)
    return ok


if __name__ == '__main__':
    sys.exit(0 if all(main(b) for b in sys.argv[1:]) else 1)
