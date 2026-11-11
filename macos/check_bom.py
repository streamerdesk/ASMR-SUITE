#!/usr/bin/env python3
"""Lee un archivo Bom (formato BOMStore de Apple) y lista rutas con modo/uid/gid/tamano, para comprobar
que coincide con el Payload. Uso: check_bom.py Bom"""
import struct, sys


def read_bom(path):
    d = open(path, 'rb').read()
    magic, ver, nblocks, ioff, ilen, voff, vlen = struct.unpack('>8sIIIIII', d[:32])
    assert magic == b'BOMStore' and ver == 1
    n = struct.unpack('>I', d[ioff:ioff + 4])[0]
    blocks = [struct.unpack('>II', d[ioff + 4 + 8 * i:ioff + 12 + 8 * i]) for i in range(n)]
    blk = lambda i: d[blocks[i][0]:blocks[i][0] + blocks[i][1]]
    vars_ = {}
    cnt = struct.unpack('>I', d[voff:voff + 4])[0]
    p = voff + 4
    for _ in range(cnt):
        idx, ln = struct.unpack('>IB', d[p:p + 5])
        vars_[d[p + 5:p + 5 + ln].decode()] = idx
        p += 5 + ln
    tree = blk(vars_['Paths'])
    tmagic, tver, child, bsize, pcount, _u = struct.unpack('>4sIIIIB', tree[:21])
    assert tmagic == b'tree'
    # bajar a la primera hoja
    node = blk(child)
    while struct.unpack('>H', node[:2])[0] == 0:
        node = blk(struct.unpack('>I', node[12:16])[0])
    entries = {}
    while True:
        leaf, count, fwd, bwd = struct.unpack('>HHII', node[:12])
        for i in range(count):
            i0, i1 = struct.unpack('>II', node[12 + 8 * i:20 + 8 * i])
            f = blk(i1)
            parent = struct.unpack('>I', f[:4])[0]
            name = f[4:f.index(b'\0', 4)].decode()
            pid, i2 = struct.unpack('>II', blk(i0)[:8])
            typ, _x, arch, mode, uid, gid, mtime, size = struct.unpack('>BBHHIIII', blk(i2)[:22])
            entries[pid] = (parent, name, typ, mode, uid, gid, size)
        if not fwd:
            break
        node = blk(fwd)
    def full(pid):
        parent, name = entries[pid][:2]
        return name if parent == 0 else full(parent) + '/' + name
    return sorted((full(k),) + v[2:] for k, v in entries.items()), pcount


if __name__ == '__main__':
    ents, pcount = read_bom(sys.argv[1])
    for e in ents:
        print('%s  type=%d mode=%o uid=%d gid=%d size=%d' % e)
    print('pathCount=%d entradas=%d' % (pcount, len(ents)))
