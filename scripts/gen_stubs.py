#!/usr/bin/env python3
"""Genera stubs de enlace de libobs a partir de binarios oficiales de OBS.

  gen_stubs.py def  <obs.dll>      <out.def>   -> .def para crear la import lib de Windows
  gen_stubs.py tbd  <libobs.so|sym> <out.tbd>  -> .tbd (text stub) de libobs.framework para macOS
"""
import subprocess, sys


def dll_exports(path):
    import pefile
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    return sorted(e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name)


def elf_exports(path):
    out = subprocess.run(['nm', '-D', '--defined-only', path], capture_output=True, text=True, check=True).stdout
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'TtWwDdBbRrVv' and not parts[2].startswith('_'):
            syms.add(parts[2].split('@')[0])
    return sorted(syms)


def main():
    mode, src, out = sys.argv[1:4]
    if mode == 'def':
        names = dll_exports(src)
        with open(out, 'w') as f:
            f.write('LIBRARY obs.dll\nEXPORTS\n' + '\n'.join(names) + '\n')
    elif mode == 'tbd':
        names = elf_exports(src)
        syms = ', '.join("'_%s'" % n for n in names)
        with open(out, 'w') as f:
            f.write("--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos, arm64-macos ]\n"
                    "install-name: '@rpath/libobs.framework/Versions/A/libobs'\n"
                    "exports:\n  - targets: [ x86_64-macos, arm64-macos ]\n"
                    "    symbols: [ %s ]\n...\n" % syms)
    else:
        sys.exit('modo desconocido')
    print('%s: %d simbolos -> %s' % (mode, len(names), out))


if __name__ == '__main__':
    main()
