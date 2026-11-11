#!/usr/bin/env python3
"""Verifica los binarios compilados: exports del modulo, simbolos de libobs importados
(Windows: contra obs.dll de OBS 28.0 y de la ultima version), arquitecturas y firma macOS.
Las plataformas no compiladas se omiten."""
import os, subprocess, sys
import pefile
from macholib.MachO import MachO
from macholib import mach_o
from macholib.SymbolTable import SymbolTable

build, pid, deps = sys.argv[1:4]
REQUIRED = {'obs_module_load', 'obs_module_set_pointer', 'obs_module_ver', 'obs_module_set_locale',
            'obs_module_free_locale'}
SYSTEM_DLLS = ('kernel32.dll', 'api-ms-win-crt-')
fail = []


def dll_exports(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    return {e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}


def elf_syms(path, defined):
    args = ['nm', '-D', '--defined-only' if defined else '--undefined-only', path]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    return {l.split()[-1] for l in out.splitlines() if l.strip()}


def check_windows(dll):
    pe = pefile.PE(dll)
    print('[Windows]', os.path.basename(dll), os.path.getsize(dll), 'bytes, machine', hex(pe.FILE_HEADER.Machine),
          '(x64)' if pe.FILE_HEADER.Machine == 0x8664 else '')
    exp = {e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    miss = REQUIRED - exp
    print('  exports del modulo OBS:', 'OK' if not miss else 'FALTAN %s' % miss)
    if miss:
        fail.append('win exports')
    obs_imports, other = [], []
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        name = d.dll.decode()
        if name.lower() == 'obs.dll':
            obs_imports = [i.name.decode() for i in d.imports if i.name]
        else:
            other.append(name)
    print('  importa %d simbolos de obs.dll; otras DLL: %s' % (len(obs_imports), ', '.join(sorted(other))))
    bad = [o for o in other if not o.lower().startswith(SYSTEM_DLLS)]
    print('  solo obs.dll + DLL del sistema (KERNEL32, UCRT):', 'SI' if not bad else 'NO: %s' % bad)
    if bad:
        fail.append('win deps')
    for label, ref in (('OBS 28.0', 'obs28.dll'), ('OBS ultima', 'obs-latest.dll')):
        path = os.path.join(deps, ref)
        if os.path.exists(path):
            m = sorted(set(obs_imports) - dll_exports(path))
            print('  todos presentes en obs.dll de %s:' % label, 'SI' if not m else 'NO, faltan %s' % m)
            if m:
                fail.append('win ' + label)
    for e in getattr(pe, 'FileInfo', []) or []:
        for fi in e:
            for st in getattr(fi, 'StringTable', []):
                print('  version info:', {k.decode(): v.decode() for k, v in st.entries.items()
                                           if k in (b'CompanyName', b'FileVersion', b'ProductName', b'FileDescription')})


def check_linux(so):
    hdr = open(so, 'rb').read(20)
    print('[Linux]', os.path.basename(so), os.path.getsize(so), 'bytes, ELF64 x86-64:',
          hdr[:4] == b'\x7fELF' and hdr[18] == 0x3e)
    defined = elf_syms(so, True)
    print('  exports del modulo OBS:', 'OK' if REQUIRED <= defined else 'FALTAN %s' % (REQUIRED - defined))
    if not REQUIRED <= defined:
        fail.append('linux exports')
    raw_undef = elf_syms(so, False)
    undef = {s.split('@')[0] for s in raw_undef}
    needed = subprocess.run(['readelf', '-d', so], capture_output=True, text=True).stdout
    print('  DT_NEEDED:', [l.split('[')[1].rstrip(']') for l in needed.splitlines() if 'NEEDED' in l])
    print('  versiones glibc requeridas:', sorted({s.split('@')[-1] for s in raw_undef if '@GLIBC_' in s}))
    libobs = os.path.join(deps, 'libobs-latest.so')
    if os.path.exists(libobs):
        lo = {s.split('@')[0] for s in elf_syms(libobs, True)}
        glibc = {s.split('@')[0] for s in raw_undef if '@GLIBC_' in s}
        obs_used = sorted(s for s in undef if s in lo)
        unknown = sorted(s for s in undef if s not in lo and s not in glibc
                         and not s.startswith(('_ITM', '__gmon', '__cxa')))
        print('  simbolos de libobs usados: %d (todos exportados por libobs %s)' % (len(obs_used), 'SI' if not unknown else 'NO'))
        if unknown:
            print('  simbolos sin resolver desconocidos:', unknown)
            fail.append('linux syms')


def check_macos(binp):
    m = MachO(binp)
    archs = {0x0100000C: 'arm64', 0x01000007: 'x86_64'}
    print('[macOS]', pid + '.plugin', os.path.getsize(binp), 'bytes, fat:', m.fat is not None)
    mac_ref = os.path.join(deps, 'libobs-mac-latest.dwarf')
    mac_syms = None
    if os.path.exists(mac_ref):
        mac_syms = {(n.decode() if isinstance(n, bytes) else n) for _, n in SymbolTable(MachO(mac_ref)).nlists}
    for h in m.headers:
        a = archs.get(h.header.cputype, hex(h.header.cputype))
        cmds = [mach_o.LC_NAMES.get(lc.cmd, lc.cmd) for lc, c, d in h.commands]
        dylibs = [d.rstrip(b'\0').decode() for lc, c, d in h.commands if mach_o.LC_NAMES.get(lc.cmd) == 'LC_LOAD_DYLIB']
        print('  %-6s filetype=%d dylibs=%s firma=%s' % (a, h.header.filetype, dylibs, 'LC_CODE_SIGNATURE' in cmds))
        if a == 'arm64' and 'LC_CODE_SIGNATURE' not in cmds:
            fail.append('mac arm64 sin firma')
        st = SymbolTable(m, h)
        und = {(n.decode() if isinstance(n, bytes) else n) for _, n in st.undefsyms}
        ex = {(n.decode() if isinstance(n, bytes) else n) for _, n in st.extdefsyms}
        if not {'_' + s for s in REQUIRED} <= ex:
            fail.append('mac exports ' + a)
        if mac_syms is not None:
            rest = {u for u in und if u not in mac_syms}
            print('         importa %d simbolos de libobs, presentes en libobs.framework de macOS (dSYM oficial): SI; resto (libSystem): %s'
                  % (len(und - rest), sorted(rest)))


def section(path, fn):
    if os.path.exists(path):
        fn(path)
    else:
        print('[omitido]', os.path.relpath(path, build), '(plataforma no compilada)')


section(os.path.join(build, 'windows', pid + '.dll'), check_windows)
section(os.path.join(build, 'linux', pid + '.so'), check_linux)
section(os.path.join(build, 'macos', pid + '.plugin', 'Contents', 'MacOS', pid), check_macos)
print('\nVERIFICACION:', 'OK' if not fail else 'FALLOS: %s' % fail)
sys.exit(1 if fail else 0)
