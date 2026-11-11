#!/usr/bin/env python3
"""lipo minimo: une binarios Mach-O thin (x86_64 + arm64) en un binario universal (fat)."""
import struct, sys

CPU = {0x01000007: 'x86_64', 0x0100000C: 'arm64'}


def main():
    out, inputs = sys.argv[1], sys.argv[2:]
    slices = []
    for p in inputs:
        data = open(p, 'rb').read()
        magic, cputype, cpusub = struct.unpack('<IiI', data[:12])
        if magic != 0xFEEDFACF:
            sys.exit('%s no es Mach-O 64 bits thin' % p)
        slices.append((cputype, cpusub & 0x00FFFFFF, data))
    align = 14  # 2^14 = 16 KiB
    off = 4096
    header = struct.pack('>II', 0xCAFEBABE, len(slices))
    body = b''
    entries = []
    cur = off
    for cputype, cpusub, data in slices:
        cur = (cur + (1 << align) - 1) & ~((1 << align) - 1)
        entries.append((cputype, cpusub, cur, len(data), align, data))
        cur += len(data)
    blob = bytearray(cur)
    blob[:8] = header
    pos = 8
    for cputype, cpusub, o, size, a, data in entries:
        blob[pos:pos + 20] = struct.pack('>iiIII', cputype, cpusub, o, size, a)
        pos += 20
        blob[o:o + size] = data
    open(out, 'wb').write(bytes(blob))
    print('universal:', out, [CPU.get(e[0], hex(e[0])) for e in entries])


if __name__ == '__main__':
    main()
