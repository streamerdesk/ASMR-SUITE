#!/usr/bin/env python3
"""Streamerdesk ASMR Suite - compilacion Linux x86_64 de los 6 efectos (mismo enfoque que macos/build_macos_suite.py).

Uso:  python3 linux/build_linux_suite.py
Compila cada plugin desde SU proyecto (sus fuentes y su common/ sin cambios) con cabeceras de OBS 28.0.0 y zig
apuntando a glibc 2.28. Sin DT_NEEDED de libobs: los simbolos se resuelven contra la libobs que OBS ya tiene cargada,
asi el mismo .so vale para libobs.so.0 (OBS 28-29), libobs.so.30+ (OBS 30+) y Flatpak. Verifica (ELF, versiones de
glibc, exports, imports contra libobs 28.0.1 Linux / obs.dll 28.0.0 / libobs 30.2.3 / libobs 33.0.1, enlace real con
--no-allow-shlib-undefined), ejecuta las pruebas DSP, la prueba de integracion de cada plugin con libobs REAL 28.0.1 y
30.2.3, empaqueta en dist-linux/ y prueba install.sh / uninstall.sh de verdad (HOME temporal, OBS normal + Flatpak)
cargando los seis efectos a la vez con libobs real."""
import hashlib, json, os, re, shlex, shutil, subprocess, sys, tarfile, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SUITE = os.path.dirname(HERE)
DEPS = os.environ.get('DEPS', os.path.join(SUITE, '.deps'))
HDR = [os.path.join(DEPS, 'obs-headers', 'libobs'), os.path.join(DEPS, 'config')]
BUILD = os.path.join(SUITE, 'build-linux')
OUT = os.path.join(SUITE, 'dist-linux')
ZIG = [sys.executable, '-m', 'ziglang']
CFG = json.load(open(os.path.join(SUITE, 'macos', 'plugins.json')))  # misma lista/nombres que macOS (solo lectura)
SUITE_VER = CFG['suite_version']
GLIBC = '2.28'
TARGET = 'x86_64-linux-gnu.' + GLIBC
# libobs reales de Linux para enlazar/probar: (etiqueta, dir de libs, fichero libobs)
OBS28_LIB = os.path.join(DEPS, 'linux-obs', 'obs28root', 'usr', 'lib', 'x86_64-linux-gnu')
OBS30_LIB = os.path.join(DEPS, 'sysroot', 'usr', 'lib', 'x86_64-linux-gnu')
REAL_OBS = [('28.0.1 (Debian, libobs.so.0 -> libobs.so.28)', OBS28_LIB, 'libobs.so.0'),
            ('30.2.3 (Debian trixie, libobs.so.0 -> libobs.so.30)', OBS30_LIB, 'libobs.so.0')]
OBS33_SO = os.path.join(DEPS, 'libobs-latest.so')  # libobs 33.0.1 oficial (deb Ubuntu de OBS)
OBS28_DEF = os.path.join(DEPS, 'obs28.def')          # exports de obs.dll 28.0.0 oficial (Windows)
def integ_src(p):
    """Prueba de integracion: la que venga con el plugin, o la generica de la suite."""
    for cand in (
        os.path.join(p['pdir'], 'test', 'obs_load_test.c'),
        os.path.join(p['pdir'], 'test', 'obs-integration', 'obs_load_test.c'),
        os.path.join(SUITE, 'tests', 'obs-integration', 'obs_load_test.c'),
    ):
        if os.path.isfile(cand):
            return cand
    return None


def log(m):
    print('\n\033[1;35m==> %s\033[0m' % m, flush=True)


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit('fallo: %s' % ' '.join(map(str, cmd)))
    return r.stdout + r.stderr


def parse_env(path):
    return dict(re.findall(r'^(\w+)="([^"]*)"', open(path).read(), re.M))



def common_dir(p):
    """common/ vendorizado del plugin, o el compartido de la suite."""
    local = os.path.join(p['pdir'], 'common')
    if os.path.isdir(os.path.join(local, 'obs')):
        return local
    return os.path.join(p.get('root') or SUITE, 'common') if os.path.isdir(os.path.join(p.get('root') or SUITE, 'common', 'obs')) else os.path.join(SUITE, 'common')


def load_plugins():
    ps = []
    for p in CFG['plugins']:
        root = p['root']
        if not os.path.isabs(root):
            root = os.path.normpath(os.path.join(SUITE, root))
        p = dict(p, root=root)
        pdir = os.path.normpath(os.path.join(root, p['dir']))
        env = parse_env(os.path.join(pdir, 'plugin.env'))
        q = dict(p, pdir=pdir, id=env['PLUGIN_ID'], display=env['PLUGIN_DISPLAY'], product=env['PLUGIN_PRODUCT'],
                 version=env['PLUGIN_VERSION'])
        q['sources'] = (env.get('PLUGIN_SOURCES') or p['sources']).split()
        q['tests'] = (env.get('PLUGIN_TESTS') or p['tests']).split()
        q['suite_layout'] = p['dir'] != '.'
        ps.append(q)
    return ps


def cflags(p):  # identico a macOS / Windows de cada proyecto
    root = p['root']
    f = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Wno-macro-redefined', '-fvisibility=hidden', '-ffp-contract=off',
         '-DPLUGIN_NAME="%s"' % p['id'], '-DPLUGIN_VERSION="%s"' % p['version']]
    if not p['suite_layout']:
        f += ['-Wno-unused-parameter', '-Wno-missing-field-initializers', '-fno-strict-aliasing']
    com = common_dir(p)
    f += ['-I' + h for h in HDR] + ['-I' + com, '-I' + os.path.join(com, 'dsp'),
                                    '-I' + os.path.join(com, 'obs')]
    if not p['suite_layout']:
        f += ['-I' + os.path.join(p['pdir'], 'src')]
    return f


def build_plugin(p):
    o = os.path.join(BUILD, p['id'])
    shutil.rmtree(o, ignore_errors=True)
    os.makedirs(o)
    srcs = [os.path.join(p['pdir'], s) for s in p['sources']] + [os.path.join(common_dir(p), 'obs', 'sd_presets.c')]
    so = os.path.join(o, p['id'] + '.so')
    w = run(ZIG + ['cc', '-target', TARGET, '-fPIC', '-shared', '-s'] + cflags(p) + ['-o', so] + srcs +
            ['-lm', '-Wl,-soname,%s.so' % p['id'], '-Wl,-z,relro', '-Wl,-z,noexecstack', '-Wl,--gc-sections'])
    if 'warning' in w:
        print(w)
    return so


# ------------------------------------------------------------------ verificacion ELF
def dyn_syms(path, defined):
    out = run(['nm', '-D', '--defined-only' if defined else '--undefined-only', path])
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if not parts:
            continue
        name = parts[-1]
        typ = parts[-2] if len(parts) >= 2 else ''
        n, _, ver = name.partition('@')
        syms[n] = (typ, ver.lstrip('@'))
    return syms


def lib_exports(path):
    return {n for n, (t, v) in dyn_syms(path, True).items() if t in ('T', 'D', 'B', 'R', 'W', 'V', 'i')}


def def_exports(path):
    ex, on = set(), False
    for line in open(path):
        if line.strip() == 'EXPORTS':
            on = True
            continue
        if on and line.strip():
            ex.add(line.split()[0])
    return ex


def verify_plugin(p, so, refs, report):
    ok = True
    lines = ['## %s %s (%s)' % (p['display'], p['version'], p['id'])]

    def chk(cond, msg):
        nonlocal ok
        ok &= bool(cond)
        lines.append('  [%s] %s' % ('OK' if cond else 'FALLO', msg))

    hdr = run(['readelf', '-h', so])
    chk('ELF64' in hdr and 'X86-64' in hdr and 'DYN' in hdr, 'ELF64 x86-64 objeto compartido (DYN), %d bytes, stripped' % os.path.getsize(so))
    dyn = run(['readelf', '-d', so])
    needed = re.findall(r'\(NEEDED\).*\[(.*)\]', dyn)
    chk(set(needed) <= {'libc.so.6', 'libm.so.6'}, 'DT_NEEDED = %s (sin libobs: se enlaza con la libobs que OBS ya cargó)' % ', '.join(needed))
    chk('RPATH' not in dyn and 'RUNPATH' not in dyn and 'TEXTREL' not in dyn, 'sin RPATH/RUNPATH/TEXTREL')
    ph = run(['readelf', '-lW', so])
    st = [l for l in ph.splitlines() if 'GNU_STACK' in l]
    chk(st and 'RWE' not in st[0], 'pila no ejecutable (GNU_STACK RW), GNU_RELRO=%s' % ('GNU_RELRO' in ph))
    ex = sorted(n for n in dyn_syms(so, True))
    bad = [s for s in ex if not s.startswith('obs_module_')]
    chk(not bad and 'obs_module_load' in ex, 'exporta solo obs_module_* (%d): %s' % (len(ex), ' '.join(ex)) + ('' if not bad else ' EXTRA: %s' % bad))
    und = dyn_syms(so, False)
    glibc = {n: v for n, (t, v) in und.items() if v.startswith('GLIBC_')}
    obs = sorted(n for n, (t, v) in und.items() if not v and n not in ('__gmon_start__', '_ITM_deregisterTMCloneTable', '_ITM_registerTMCloneTable', '__cxa_finalize'))
    weak_misc = sorted(n for n, (t, v) in und.items() if not v and n not in obs)
    vers = sorted({tuple(map(int, v[6:].split('.'))) for v in glibc.values()})
    maxv = '.'.join(map(str, vers[-1])) if vers else '-'
    chk(not vers or vers[-1] <= tuple(map(int, GLIBC.split('.'))), 'glibc maxima requerida: GLIBC_%s (<= %s) — %d simbolos: %s' % (
        maxv, GLIBC, len(glibc), ' '.join('%s@%s' % (n, v) for n, v in sorted(glibc.items()))))
    for label, ex_set in refs:
        miss = [s for s in obs if s not in ex_set]
        chk(not miss, '%d simbolos de libobs importados, todos en %s' % (len(obs), label) + ('' if not miss else ' FALTAN: %s' % miss))
    lines.append('     libobs: ' + ' '.join(obs))
    if weak_misc:
        lines.append('     (debiles opcionales del runtime: %s)' % ' '.join(weak_misc))
    # enlace real: todo simbolo indefinido del plugin debe resolverse con libobs real + glibc
    tdir = os.path.join(BUILD, p['id'])
    main_c = os.path.join(tdir, 'linkcheck.c')
    open(main_c, 'w').write('int main(void){return 0;}\n')
    for label, libdir, lib in REAL_OBS:
        r = subprocess.run(['gcc', '-o', os.path.join(tdir, 'linkcheck'), main_c, '-Wl,--no-as-needed', so,
                            os.path.join(libdir, lib), '-lm', '-Wl,--no-allow-shlib-undefined', '-Wl,-rpath-link,' + libdir],
                           capture_output=True, text=True)
        err = [l for l in (r.stdout + r.stderr).splitlines() if p['id'] in l]
        chk(r.returncode == 0 or not err, 'enlace real con ld --no-allow-shlib-undefined contra libobs %s: sin simbolos sin resolver' % label +
            ('' if not err else ' ERRORES: %s' % err[:5]))
    locs = sorted(os.listdir(os.path.join(p['pdir'], 'data', 'locale')))
    chk('es-ES.ini' in locs and 'en-US.ini' in locs, 'locale: %s' % ', '.join(locs))
    report.extend(lines)
    return ok, obs


def run_dsp_tests(p, report):
    o = os.path.join(BUILD, p['id'])
    exe = os.path.join(o, 'dsp-test')
    inc = ['-I' + common_dir(p), '-I' + os.path.join(common_dir(p), 'dsp'),
           '-I' + os.path.join(p['pdir'], 'src')]
    run(ZIG + ['cc', '-target', TARGET, '-O2', '-std=gnu11', '-ffp-contract=off'] + inc + ['-o', exe] +
        [os.path.join(p['pdir'], t) for t in p['tests']] + ['-lm'])
    r = subprocess.run([exe], capture_output=True, text=True, cwd=p['pdir'])
    out = (r.stdout + r.stderr).strip()
    open(os.path.join(o, 'dsp-test.log'), 'w').write(out + '\n')
    tail = out.splitlines()[-1] if out else ''
    report.append('  [%s] pruebas DSP offline, mismo binario x86_64 glibc %s (exit %d): %s' % (
        'OK' if r.returncode == 0 else 'FALLO', GLIBC, r.returncode, tail))
    return r.returncode == 0


def run_integration(p, so, data, report):
    """La prueba de integracion propia del plugin, con libobs REAL (como OBS: obs_open_module + audio real)."""
    o = os.path.join(BUILD, p['id'])
    allok = True
    for label, libdir, lib in REAL_OBS:
        tag = label.split()[0]
        exe = os.path.join(o, 'obs_load_test-%s' % tag)
        run(['gcc', '-O1', '-std=gnu11', '-o', exe, integ_src(p)] + ['-I' + h for h in HDR] +
            [os.path.join(libdir, lib), '-lm', '-lpthread', '-Wl,-rpath-link,' + libdir])
        cfg = tempfile.mkdtemp()
        env = dict(os.environ, LD_LIBRARY_PATH=libdir)
        r = subprocess.run([exe, so, data, cfg], capture_output=True, text=True, env=env, timeout=300)
        shutil.rmtree(cfg, ignore_errors=True)
        out = '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if not l.startswith('debug: '))
        open(os.path.join(o, 'obs-integration-%s.log' % tag), 'w').write(out + '\n')
        good = r.returncode == 0 and 'RESULTADO INTEGRACION: OK' in out
        allok &= good
        oks = len(re.findall(r'^\s+ok: ', out, re.M))
        fails = len(re.findall(r'FALLO', out))
        ver = re.search(r'libobs version (\S+)', out)
        report.append('  [%s] integracion con libobs REAL %s (version en ejecucion %s): %d comprobaciones ok, %d fallos, %s' % (
            'OK' if good else 'FALLO', label, ver.group(1) if ver else '?', oks, fails,
            'RESULTADO INTEGRACION: OK' if good else 'exit %d' % r.returncode))
        for l in out.splitlines():
            if re.search(r'cargado \(version|nombre en espanol|controles, .*ayuda', l):
                report.append('       ' + l.strip())
    return allok


# ------------------------------------------------------------------ empaquetado
def shell_array(vals):
    return '(' + ' '.join(shlex.quote(v) for v in vals) + ')'


def write(path, text, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w').write(text)
    os.chmod(path, mode)


def scripts(stage, plugins, title, version):
    common = open(os.path.join(HERE, 'common.sh.in')).read()
    rep = {'@TITLE@': shlex.quote(title), '@IDS@': shell_array([p['id'] for p in plugins]),
           '@NAMES_ES@': shell_array([p['name_es'] for p in plugins]), '@NAMES_EN@': shell_array([p['name_en'] for p in plugins])}
    for k, v in rep.items():
        common = common.replace(k, v)
    for tpl, out in (('install.sh.in', 'install.sh'), ('uninstall.sh.in', 'uninstall.sh')):
        t = open(os.path.join(HERE, tpl)).read().replace('@COMMON@', common.rstrip('\n'))
        t = t.replace('@TITLE@', title).replace('@VERSION@', version)
        write(os.path.join(stage, out), t, 0o755)


def stage_plugin(stage, p, so):
    d = os.path.join(stage, 'plugins', p['id'])
    os.makedirs(os.path.join(d, 'bin', '64bit'))
    shutil.copy(so, os.path.join(d, 'bin', '64bit', p['id'] + '.so'))
    os.chmod(os.path.join(d, 'bin', '64bit', p['id'] + '.so'), 0o755)
    shutil.copytree(os.path.join(p['pdir'], 'data'), os.path.join(d, 'data'))


def docs(dst, p=None, plugins=None):
    shutil.copy(os.path.join(SUITE, 'LICENSE'), os.path.join(dst, 'LICENSE.txt'))
    shutil.copy(os.path.join(SUITE, 'branding', 'banner.jpg'), os.path.join(dst, 'banner.jpg'))
    notes = open(os.path.join(HERE, 'notes.md')).read().strip()
    if p:
        t = open(os.path.join(HERE, 'README-plugin.md.in')).read()
        for k, v in (('@ID@', p['id']), ('@DISPLAY@', p['display']), ('@VERSION@', p['version']),
                     ('@NAME_ES@', p['name_es']), ('@NAME_EN@', p['name_en']), ('@NOTES@', notes)):
            t = t.replace(k, v)
        write(os.path.join(dst, 'LEEME-README.md'), t)
        note = ('# Changelog — %s\n\n## Linux — %s\n- **Nuevo:** versión para Linux x86_64 (glibc 2.28+, OBS Studio 28+, '
                'también OBS Flatpak), con el mismo sonido, presets e interfaz que en Windows y macOS. Incluye `install.sh` '
                'y `uninstall.sh` (sin sudo; tus presets se conservan).\n'
                '- **New:** Linux x86_64 build (glibc 2.28+, OBS Studio 28+, Flatpak OBS included), same sound, presets and UI '
                'as on Windows and macOS. Includes `install.sh` and `uninstall.sh` (no sudo; your presets are kept).\n\n' % (p['display'], p['version']))
        orig = re.sub(r'^# .*\n+', '', open(os.path.join(p['pdir'], 'CHANGELOG.md')).read(), count=1)
        write(os.path.join(dst, 'CHANGELOG.md'), note + orig)
    else:
        table = '\n'.join('| %s / %s | %s | `plugins/%s` |' % (q['name_es'], q['name_en'], q['version'], q['id']) for q in plugins)
        t = open(os.path.join(HERE, 'README-suite.md.in')).read().replace('@TABLE@', table) \
            .replace('@SUITE_VERSION@', SUITE_VER).replace('@NOTES@', notes)
        write(os.path.join(dst, 'LEEME-README.md'), t)
        shutil.copy(os.path.join(HERE, 'CHANGELOG-linux.md'), os.path.join(dst, 'CHANGELOG.md'))


def mktgz(path, stage, prefix):
    if os.path.exists(path):
        os.remove(path)
    run([sys.executable, os.path.join(SUITE, 'scripts', 'mkarchive.py'), 'tgz', path, stage, prefix])


def package_plugin(p, so):
    stage = os.path.join(BUILD, 'stage', p['product'])
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    stage_plugin(stage, p, so)
    scripts(stage, [p], p['display'], p['version'])
    docs(stage, p)
    z = os.path.join(OUT, '%s-%s-linux-x86_64.tar.gz' % (p['product'], p['version']))
    mktgz(z, stage, '%s-%s-linux' % (p['product'], p['version']))
    return z


def package_suite(plugins, sos):
    name = 'StreamerdeskASMR-Suite-%s-linux' % SUITE_VER
    stage = os.path.join(BUILD, 'stage', name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    for p in plugins:
        stage_plugin(stage, p, sos[p['id']])
    scripts(stage, plugins, 'Streamerdesk ASMR Suite', SUITE_VER)
    docs(stage, plugins=plugins)
    z = os.path.join(OUT, '%s-x86_64.tar.gz' % name)
    mktgz(z, stage, name)
    return z


# ------------------------------------------------------------------ prueba real de install.sh / uninstall.sh
def test_installers(suite_tgz, plugin_tgzs, plugins, report):
    lines = ['## Prueba real de install.sh / uninstall.sh (HOME temporal, tarballs finales extraidos)']
    ok = True

    def chk(cond, msg):
        nonlocal ok
        ok &= bool(cond)
        lines.append('  [%s] %s' % ('OK' if cond else 'FALLO', msg))

    ids = [p['id'] for p in plugins]
    w = tempfile.mkdtemp(prefix='sdlinux-')
    tarfile.open(suite_tgz).extractall(w, filter='tar') if hasattr(tarfile, 'data_filter') else tarfile.open(suite_tgz).extractall(w)
    top = os.path.join(w, os.listdir(w)[0])
    for s in ('install.sh', 'uninstall.sh'):
        r = subprocess.run(['bash', '-n', os.path.join(top, s)], capture_output=True, text=True)
        chk(r.returncode == 0, 'bash -n %s' % s)
        if shutil.which('shellcheck'):
            r = subprocess.run(['shellcheck', '-S', 'warning', os.path.join(top, s)], capture_output=True, text=True)
            chk(r.returncode == 0, 'shellcheck -S warning %s%s' % (s, '' if r.returncode == 0 else '\n' + r.stdout[:2000]))
    chk(all(os.access(os.path.join(top, s), os.X_OK) for s in ('install.sh', 'uninstall.sh')), 'install.sh / uninstall.sh ejecutables dentro del tar.gz')
    home = os.path.join(w, 'home')
    nat = os.path.join(home, '.config', 'obs-studio')
    flat = os.path.join(home, '.var', 'app', 'com.obsproject.Studio', 'config', 'obs-studio')
    os.makedirs(nat)
    os.makedirs(os.path.join(home, '.var', 'app', 'com.obsproject.Studio'))
    # presets previos del usuario (deben sobrevivir a instalar, reinstalar y desinstalar)
    presets = {}
    for base in (nat, flat):
        for i in ids:
            pth = os.path.join(base, 'plugin_config', i, 'presets.json')
            os.makedirs(os.path.dirname(pth), exist_ok=True)
            open(pth, 'w').write('{"presets":[{"name":"Mi preset %s"}]}' % i)
            presets[pth] = hashlib.sha256(open(pth, 'rb').read()).hexdigest()
    env = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XDG_CONFIG_HOME', 'LC_ALL', 'LC_MESSAGES', 'LANGUAGE')}
    env.update(HOME=home, LANG='es_ES.UTF-8', PATH='/usr/bin:/bin')
    r = subprocess.run([os.path.join(top, 'install.sh'), '--yes'], capture_output=True, text=True, env=env, stdin=subprocess.DEVNULL)
    lines.append('     $ ./install.sh --yes  (OBS normal + Flatpak presentes)')
    lines.extend('       ' + l for l in r.stdout.strip().splitlines())
    chk(r.returncode == 0, 'install.sh --yes termina con exit 0')
    for base, lab in ((nat, '~/.config/obs-studio'), (flat, 'Flatpak ~/.var/app/com.obsproject.Studio/config/obs-studio')):
        good = all(os.path.isfile(os.path.join(base, 'plugins', i, 'bin', '64bit', i + '.so')) and
                   os.path.isfile(os.path.join(base, 'plugins', i, 'data', 'locale', 'es-ES.ini')) for i in ids)
        chk(good, 'los 6 efectos instalados en %s/plugins/<id>/{bin/64bit,data}' % lab)
    # carga real como OBS (busqueda de modulos por ruta) con libobs 28 y 30
    lt = os.path.join(BUILD, 'obs_layout_test')
    for label, libdir, lib in REAL_OBS:
        tag = label.split()[0]
        run(['gcc', '-O1', '-std=gnu11', '-o', lt + tag, os.path.join(HERE, 'obs_layout_test.c')] + ['-I' + h for h in HDR] +
            [os.path.join(libdir, lib), '-Wl,-rpath-link,' + libdir])
        for base, lab in ((nat, 'OBS normal'), (flat, 'Flatpak')):
            rr = subprocess.run([lt + tag, base] + ids, capture_output=True, text=True, env=dict(os.environ, LD_LIBRARY_PATH=libdir))
            out = [l for l in (rr.stdout + rr.stderr).splitlines() if not l.startswith(('debug:', 'info:'))]
            open(os.path.join(BUILD, 'install-load-%s-%s.log' % (tag, lab.replace(' ', '-'))), 'w').write('\n'.join(out) + '\n')
            chk(rr.returncode == 0, 'libobs REAL %s carga los 6 efectos a la vez desde la carpeta instalada (%s), como OBS:' % (label, lab))
            lines.extend('       ' + l for l in out if l.strip().startswith(('ok:', 'FALLO', 'libobs', 'RESULTADO')))
    same = all(hashlib.sha256(open(pth, 'rb').read()).hexdigest() == h for pth, h in presets.items())
    chk(same, 'presets.json del usuario intactos tras instalar (%d ficheros)' % len(presets))
    # solo Flatpak / solo nativo / reinstalar
    r = subprocess.run([os.path.join(top, 'install.sh'), '--yes', '--flatpak'], capture_output=True, text=True, env=env, stdin=subprocess.DEVNULL)
    chk(r.returncode == 0 and 'obs-studio/plugins' in r.stdout and '.config' not in r.stdout, 'install.sh --yes --flatpak instala solo en Flatpak (reinstalación limpia)')
    # sin OBS detectado -> ruta normal
    home2 = os.path.join(w, 'home2')
    os.makedirs(home2)
    env2 = dict(env, HOME=home2, LANG='en_US.UTF-8')
    r = subprocess.run([os.path.join(top, 'install.sh'), '--text'], capture_output=True, text=True, env=env2, stdin=subprocess.DEVNULL)
    chk(r.returncode == 0 and all(os.path.isfile(os.path.join(home2, '.config', 'obs-studio', 'plugins', i, 'bin', '64bit', i + '.so')) for i in ids),
        'sin OBS detectado y sin terminal: instala los 6 en ~/.config/obs-studio (mensajes en inglés con LANG=en_US)')
    # respuestas por terminal (pty): elegir solo algunos
    try:
        import pexpect
        home3 = os.path.join(w, 'home3')
        os.makedirs(os.path.join(home3, '.config', 'obs-studio'))
        ch = pexpect.spawn(os.path.join(top, 'install.sh'), ['--text'], env=dict(env, HOME=home3), encoding='utf-8', timeout=20)
        ch.expect(r'\[S/n\]'); ch.sendline('n')
        answers = ['', 'n', '', 'n', '', '']  # quita Reverb y Susurro
        for a in answers:
            ch.expect(r'\[S/n\]'); ch.sendline(a)
        ch.expect(pexpect.EOF)
        got = sorted(os.listdir(os.path.join(home3, '.config', 'obs-studio', 'plugins')))
        want = sorted(i for k, i in enumerate(ids) if answers[k] != 'n')
        chk(got == want, 'modo texto en terminal: desmarcar 2 efectos instala solo los otros 4 (%s)' % ', '.join(got))
        # whiptail en terminal real: todos marcados por defecto -> Intro instala los 6
        if shutil.which('whiptail'):
            home4 = os.path.join(w, 'home4')
            os.makedirs(os.path.join(home4, '.config', 'obs-studio'))
            ch = pexpect.spawn(os.path.join(top, 'install.sh'), ['--lang=es'], env=dict(env, HOME=home4, TERM='xterm', LANG='C.UTF-8'), encoding='utf-8', codec_errors='replace',
                               timeout=20, dimensions=(30, 90))
            ch.expect('Espacio marca'); ch.send('\r')
            ch.expect('Listo'); ch.send('\r')
            ch.expect(pexpect.EOF)
            got = sorted(os.listdir(os.path.join(home4, '.config', 'obs-studio', 'plugins')))
            chk(got == sorted(ids), 'whiptail (terminal real): lista con los 6 marcados por defecto, Intro instala los 6')
            # whiptail: OBS normal + Flatpak -> pregunta donde
            home5 = os.path.join(w, 'home5')
            os.makedirs(os.path.join(home5, '.config', 'obs-studio'))
            os.makedirs(os.path.join(home5, '.var', 'app', 'com.obsproject.Studio'))
            ch = pexpect.spawn(os.path.join(top, 'install.sh'), ['--lang=es'], env=dict(env, HOME=home5, TERM='xterm', LANG='C.UTF-8'), encoding='utf-8', codec_errors='replace',
                               timeout=20, dimensions=(30, 90))
            ch.expect('Espacio marca'); ch.send('\r')
            ch.expect('dos sitios'); ch.send(' ')  # desmarca el primero (OBS normal) y acepta
            ch.send('\r')
            ch.expect('Listo'); ch.send('\r')
            ch.expect(pexpect.EOF)
            fl = os.path.join(home5, '.var', 'app', 'com.obsproject.Studio', 'config', 'obs-studio', 'plugins')
            chk(os.path.isdir(fl) and len(os.listdir(fl)) == 6 and not os.path.exists(os.path.join(home5, '.config', 'obs-studio', 'plugins')),
                'whiptail: con OBS normal y Flatpak pregunta dónde; elegir solo Flatpak instala solo ahí')
    except ImportError:
        lines.append('  [--] pexpect no disponible: pruebas interactivas omitidas')
    # desinstalar
    r = subprocess.run([os.path.join(top, 'uninstall.sh'), '--yes'], capture_output=True, text=True, env=env, stdin=subprocess.DEVNULL)
    lines.append('     $ ./uninstall.sh --yes')
    lines.extend('       ' + l for l in r.stdout.strip().splitlines())
    gone = all(not os.path.exists(os.path.join(b, 'plugins', i)) for b in (nat, flat) for i in ids)
    same = all(os.path.isfile(pth) and hashlib.sha256(open(pth, 'rb').read()).hexdigest() == h for pth, h in presets.items())
    chk(r.returncode == 0 and gone, 'uninstall.sh --yes quita los 6 efectos de OBS normal y Flatpak')
    chk(same, 'presets.json del usuario intactos tras desinstalar (%d ficheros)' % len(presets))
    # instaladores individuales
    for z in plugin_tgzs:
        d = tempfile.mkdtemp(dir=w)
        tarfile.open(z).extractall(d)
        t = os.path.join(d, os.listdir(d)[0])
        pid = os.listdir(os.path.join(t, 'plugins'))[0]
        h = os.path.join(d, 'h')
        os.makedirs(os.path.join(h, '.config', 'obs-studio'))
        r = subprocess.run([os.path.join(t, 'install.sh')], capture_output=True, text=True, env=dict(env, HOME=h), stdin=subprocess.DEVNULL)
        so = os.path.join(h, '.config', 'obs-studio', 'plugins', pid, 'bin', '64bit', pid + '.so')
        inst = os.path.isfile(so)
        r2 = subprocess.run([os.path.join(t, 'uninstall.sh'), '--yes'], capture_output=True, text=True, env=dict(env, HOME=h), stdin=subprocess.DEVNULL)
        chk(r.returncode == 0 and inst and r2.returncode == 0 and not os.path.exists(os.path.dirname(os.path.dirname(os.path.dirname(so)))),
            '%s: install.sh / uninstall.sh individuales' % os.path.basename(z) +
            ('' if r.returncode == 0 and r2.returncode == 0 else ' rc=%d/%d %s %s' % (r.returncode, r2.returncode, r.stdout[-300:], r2.stdout[-300:])))
    shutil.rmtree(w, ignore_errors=True)
    report.extend(lines)
    return ok


def main():
    for lib in [os.path.join(d, l) for _, d, l in REAL_OBS] + [OBS33_SO, OBS28_DEF]:
        if not os.path.exists(lib):
            raise SystemExit('falta %s' % lib)
    os.makedirs(BUILD, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    refs = [('libobs 28.0.1 Linux (Debian libobs0 28.0.1+dfsg1-1)', lib_exports(os.path.join(OBS28_LIB, 'libobs.so.28'))),
            ('obs.dll 28.0.0 oficial (Windows, misma API)', def_exports(OBS28_DEF)),
            ('libobs 30.2.3 Linux (Debian trixie)', lib_exports(os.path.join(OBS30_LIB, 'libobs.so.30'))),
            ('libobs 33.0.1 Linux oficial (deb de OBS Project)', lib_exports(OBS33_SO))]
    plugins = load_plugins()
    report = ['# Verificacion Linux - Streamerdesk ASMR Suite %s' % SUITE_VER, '',
              'Compilador: zig %s, objetivo %s, cabeceras OBS 28.0.0' % (run(ZIG + ['version']).strip(), TARGET),
              'libobs de referencia: ' + '; '.join('%s (%d exports)' % (l, len(s)) for l, s in refs), '']
    sos, allok = {}, True
    for p in plugins:
        log('%s %s' % (p['display'], p['version']))
        so = build_plugin(p)
        sos[p['id']] = so
        good, _ = verify_plugin(p, so, refs, report)
        allok &= good
        allok &= run_dsp_tests(p, report)
        allok &= run_integration(p, so, os.path.join(p['pdir'], 'data'), report)
        report.append('')
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    log('Empaquetado')
    tgzs = [package_plugin(p, sos[p['id']]) for p in plugins]
    suite = package_suite(plugins, sos)
    docs(OUT, plugins=plugins)
    log('Prueba de instaladores')
    allok &= test_installers(suite, tgzs, plugins, report)
    files = sorted(f for f in os.listdir(OUT) if f.endswith('.tar.gz'))
    with open(os.path.join(OUT, 'SHA256SUMS.txt'), 'w') as f:
        for n in files:
            f.write('%s  %s\n' % (hashlib.sha256(open(os.path.join(OUT, n), 'rb').read()).hexdigest(), n))
    report += ['', 'RESULTADO: %s' % ('TODO OK' if allok else 'HAY FALLOS')]
    open(os.path.join(BUILD, 'verify-linux.md'), 'w').write('\n'.join(report) + '\n')
    print('\n'.join(report))
    print(open(os.path.join(OUT, 'SHA256SUMS.txt')).read())
    sys.exit(0 if allok else 1)


if __name__ == '__main__':
    main()
