#!/usr/bin/env python3
"""Streamerdesk ASMR Suite - compilacion macOS universal (arm64 + x86_64) de los 6 efectos desde Linux.

Uso:  python3 macos/build_macos_suite.py
Compila cada plugin desde SU proyecto (sus fuentes y su common/ sin cambios) con cabeceras de OBS 28.0.0,
enlaza contra un stub .tbd generado del libobs REAL de OBS 28.0.0 para macOS (cualquier simbolo posterior
a 28.0 falla al enlazar), une slices, monta el .plugin, firma ad-hoc, verifica contra libobs 28.0.0 y 33.0.1
reales de macOS, ejecuta las pruebas DSP offline y empaqueta en dist-macos/."""
import hashlib, json, os, re, shlex, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SUITE = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import macpkg  # noqa: E402

DEPS = os.environ.get('DEPS', os.path.join(SUITE, '.deps'))
MACLIBS = os.path.join(DEPS, 'macobs', 'libs')
RCODESIGN = os.path.join(DEPS, 'mactools', 'rcodesign')
MKBOM = os.path.join(DEPS, 'mactools', 'mkbom')
BUILD = os.path.join(SUITE, 'build-macos')
OUT = os.path.join(SUITE, 'dist-macos')
ZIG = [sys.executable, '-m', 'ziglang']
CFG = json.load(open(os.path.join(HERE, 'plugins.json')))
SUITE_VER = CFG['suite_version']
SUITE_ENV = dict(re.findall(r'^(\w+)="?([^"\n]*)"?', open(os.path.join(SUITE, 'suite.env')).read(), re.M))
VENDOR = SUITE_ENV.get('SD_VENDOR', 'Streamerdesk')
URL = SUITE_ENV.get('SD_URL', 'https://streamerdesk.com')
PKG_REPORT = []
ARCHS = {'arm64': 'aarch64-macos.11.0', 'x86_64': 'x86_64-macos.11.0'}


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


# ------------------------------------------------------------------ Mach-O helpers
def macho_info(path):
    from macholib.MachO import MachO
    from macholib import mach_o
    from macholib.SymbolTable import SymbolTable
    m = MachO(path)
    res = {}
    for h in m.headers:
        arch = {0x0100000C: 'arm64', 0x01000007: 'x86_64'}.get(h.header.cputype, hex(h.header.cputype))
        libs, rpaths, idd, sig, minos, filetype = [], [], None, False, None, h.header.filetype
        for lc, c, d in h.commands:
            n = mach_o.LC_NAMES.get(lc.cmd, lc.cmd)
            if n in ('LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB'):
                libs.append(d.rstrip(b'\0').decode())
            elif n == 'LC_RPATH':
                rpaths.append(d.rstrip(b'\0').decode())
            elif n == 'LC_ID_DYLIB':
                idd = d.rstrip(b'\0').decode()
            elif n == 'LC_CODE_SIGNATURE':
                sig = True
            elif n == 'LC_BUILD_VERSION':
                v = c.minos
                minos = '%d.%d' % (v >> 16, (v >> 8) & 0xff)
        st = SymbolTable(m, h)
        ex = sorted(n.decode() if isinstance(n, bytes) else n for _, n in st.extdefsyms)
        und = {}
        for nl, n in st.undefsyms:
            n = n.decode() if isinstance(n, bytes) else n
            ordn = (nl.n_desc >> 8) & 0xff
            und[n] = libs[ordn - 1] if 0 < ordn <= len(libs) else 'ordinal-%d' % ordn
        res[arch] = dict(libs=libs, rpaths=rpaths, id=idd, sig=sig, minos=minos, exports=ex, undef=und,
                         filetype=filetype)
    return res


def obs_exports(ver):
    out = {}
    for a in ARCHS:
        out[a] = set(macho_info(os.path.join(MACLIBS, 'libobs-%s-%s' % (ver, a)))[a]['exports'])
    return out


def gen_tbd28(path):
    ex = obs_exports('28.0.0')
    common = sorted(ex['arm64'] & ex['x86_64'])
    syms = ', '.join("'%s'" % s for s in common)
    with open(path, 'w') as f:
        f.write("--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos, arm64-macos ]\n"
                "install-name: '@rpath/libobs.framework/Versions/A/libobs'\n"
                "current-version: 28.0\ncompatibility-version: 1.0\n"
                "exports:\n  - targets: [ x86_64-macos, arm64-macos ]\n    symbols: [ %s ]\n...\n" % syms)
    return len(common)


# ------------------------------------------------------------------ plugins

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
        q = dict(p)
        q['pdir'] = pdir
        q['id'] = env['PLUGIN_ID']
        q['display'] = env['PLUGIN_DISPLAY']
        q['product'] = env['PLUGIN_PRODUCT']
        q['version'] = env['PLUGIN_VERSION']
        q['bundle_id'] = env.get('PLUGIN_BUNDLE_ID') or p['bundle_id']
        q['sources'] = (env.get('PLUGIN_SOURCES') or p['sources']).split()
        q['tests'] = (env.get('PLUGIN_TESTS') or p['tests']).split()
        q['suite_layout'] = p['dir'] != '.'
        ps.append(q)
    return ps


def cflags(p):
    root = p['root']
    f = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Wno-macro-redefined', '-fvisibility=hidden', '-ffp-contract=off',
         '-DPLUGIN_NAME="%s"' % p['id'], '-DPLUGIN_VERSION="%s"' % p['version']]
    if not p['suite_layout']:  # mismos flags que build-windows.sh de esos proyectos
        f += ['-Wno-unused-parameter', '-Wno-missing-field-initializers', '-fno-strict-aliasing']
    com = common_dir(p)
    f += ['-I' + os.path.join(DEPS, 'obs-headers', 'libobs'), '-I' + os.path.join(DEPS, 'config'),
          '-I' + com, '-I' + os.path.join(com, 'dsp'),
          '-I' + os.path.join(com, 'obs')]
    if not p['suite_layout']:
        f += ['-I' + os.path.join(p['pdir'], 'src')]
    return f


def build_plugin(p, tbd):
    o = os.path.join(BUILD, p['id'])
    shutil.rmtree(o, ignore_errors=True)
    os.makedirs(o)
    srcs = [os.path.join(p['pdir'], s) for s in p['sources']] + [os.path.join(common_dir(p), 'obs', 'sd_presets.c')]
    thin = []
    for a, tgt in ARCHS.items():
        out = os.path.join(o, '%s-%s' % (p['id'], a))
        w = run(ZIG + ['cc', '-target', tgt, '-shared'] + cflags(p) + ['-o', out] + srcs + [tbd,
                '-Wl,-install_name,@rpath/%s.plugin/Contents/MacOS/%s' % (p['id'], p['id']),
                '-Wl,-rpath,@executable_path/../Frameworks', '-Wl,-rpath,@loader_path/../../../../Frameworks',
                '-Wl,-dead_strip'])
        if 'warning' in w:
            print(w)
        thin.append(out)
    fat = os.path.join(o, p['id'])
    run([sys.executable, os.path.join(SUITE, 'scripts', 'macho_lipo.py'), fat] + thin)
    b = os.path.join(o, p['id'] + '.plugin', 'Contents')
    os.makedirs(os.path.join(b, 'MacOS'))
    shutil.copy(fat, os.path.join(b, 'MacOS', p['id']))
    os.chmod(os.path.join(b, 'MacOS', p['id']), 0o755)
    shutil.copytree(os.path.join(p['pdir'], 'data'), os.path.join(b, 'Resources'))
    shutil.copy(os.path.join(SUITE, 'branding', 'icon.icns'), os.path.join(b, 'Resources', 'icon.icns'))
    plist = open(os.path.join(SUITE, 'common', 'macos', 'Info.plist.in')).read()
    for k, v in (('@ID@', p['id']), ('@DISPLAY@', p['display']), ('@BUNDLE_ID@', p['bundle_id']),
                 ('@VERSION@', p['version']), ('@VENDOR@', VENDOR), ('@URL@', URL)):
        plist = plist.replace(k, v)
    open(os.path.join(b, 'Info.plist'), 'w').write(plist)
    open(os.path.join(b, 'PkgInfo'), 'w').write('BNDL????')
    bundle = os.path.join(o, p['id'] + '.plugin')
    # firma ad-hoc de todo el bundle (ambos slices + _CodeSignature/CodeResources sellando Resources)
    run([RCODESIGN, 'sign', bundle])
    return bundle, thin


def verify_plugin(p, bundle, thin, obs28, obs33, report):
    binp = os.path.join(bundle, 'Contents', 'MacOS', p['id'])
    info = macho_info(binp)
    ok = True
    lines = ['## %s %s (%s)' % (p['display'], p['version'], p['id'])]

    def chk(cond, msg):
        nonlocal ok
        ok &= bool(cond)
        lines.append('  [%s] %s' % ('OK' if cond else 'FALLO', msg))

    raw = open(binp, 'rb').read(4)
    chk(raw == b'\xca\xfe\xba\xbe' and sorted(info) == ['arm64', 'x86_64'],
        'binario universal (fat) con slices: %s' % ', '.join(sorted(info)))
    for a, i in sorted(info.items()):
        chk(i['filetype'] == 6, '%s: MH_DYLIB' % a)
        chk(i['minos'] == '11.0', '%s: minos macOS %s' % (a, i['minos']))
        chk(i['sig'], '%s: LC_CODE_SIGNATURE presente (firma ad-hoc)' % a)
        chk(i['id'] == '@rpath/%s.plugin/Contents/MacOS/%s' % (p['id'], p['id']), '%s: install name %s' % (a, i['id']))
        chk(sorted(i['libs']) == ['/usr/lib/libSystem.B.dylib', '@rpath/libobs.framework/Versions/A/libobs'],
            '%s: dependencias = %s' % (a, ', '.join(i['libs'])))
        chk('@executable_path/../Frameworks' in i['rpaths'], '%s: rpaths = %s' % (a, ', '.join(i['rpaths'])))
        # __dso_handle y __mh_dylib_header los sintetiza el enlazador en todo dylib (como ld64)
        mods = [s for s in i['exports'] if s.startswith('_obs_module_')]
        bad_ex = [s for s in i['exports'] if not s.startswith('_obs_module_') and s not in ('___dso_handle', '__mh_dylib_header')]
        chk(not bad_ex and '_obs_module_load' in mods,
            '%s: exporta solo obs_module_* (%d) + simbolos del enlazador: %s' % (a, len(mods), ' '.join(s[1:] for s in mods)) +
            ('' if not bad_ex else ' EXTRA: %s' % bad_ex))
        libobs = sorted(s for s, l in i['undef'].items() if 'libobs' in l)
        other = sorted(s for s, l in i['undef'].items() if 'libobs' not in l)
        m28 = [s for s in libobs if s not in obs28[a]]
        m33 = [s for s in libobs if s not in obs33[a]]
        chk(not m28, '%s: %d simbolos de libobs importados, todos en libobs 28.0.0 macOS %s' % (a, len(libobs), a) +
            ('' if not m28 else ' FALTAN: %s' % m28))
        chk(not m33, '%s: todos en libobs 33.0.1 macOS %s' % (a, a) + ('' if not m33 else ' FALTAN: %s' % m33))
        odd = [s for s, l in i['undef'].items() if l not in ('/usr/lib/libSystem.B.dylib', '@rpath/libobs.framework/Versions/A/libobs')]
        chk(not odd, '%s: %d simbolos de libSystem (%s)' % (a, len(other), ' '.join(s[1:] for s in other)))
        if a == 'arm64':
            lines.append('     libobs: ' + ' '.join(s[1:] for s in libobs))
    r = subprocess.run([sys.executable, os.path.join(HERE, 'verify_codesign.py'), bundle], capture_output=True, text=True)
    chk(r.returncode == 0, 'firma ad-hoc verificada (hashes de pagina, Info.plist, recursos sellados):')
    lines.extend('    ' + l for l in r.stdout.rstrip().splitlines())
    cs = os.path.isfile(os.path.join(bundle, 'Contents', '_CodeSignature', 'CodeResources'))
    chk(cs, '_CodeSignature/CodeResources presente (recursos sellados)')
    pl = open(os.path.join(bundle, 'Contents', 'Info.plist')).read()
    chk(p['bundle_id'] in pl and p['version'] in pl, 'Info.plist: %s %s' % (p['bundle_id'], p['version']))
    locs = sorted(os.listdir(os.path.join(bundle, 'Contents', 'Resources', 'locale')))
    chk('es-ES.ini' in locs and 'en-US.ini' in locs, 'locale: %s' % ', '.join(locs))
    report.extend(lines)
    return ok


def run_tests(p, report):
    o = os.path.join(BUILD, p['id'])
    exe = os.path.join(o, 'dsp-test')
    inc = ['-I' + common_dir(p), '-I' + os.path.join(common_dir(p), 'dsp'),
           '-I' + os.path.join(p['pdir'], 'src')]
    run(ZIG + ['cc', '-O2', '-std=gnu11', '-ffp-contract=off'] + inc + ['-o', exe] +
        [os.path.join(p['pdir'], t) for t in p['tests']] + ['-lm'])
    r = subprocess.run([exe], capture_output=True, text=True, cwd=p['pdir'])
    out = (r.stdout + r.stderr).strip()
    open(os.path.join(o, 'dsp-test.log'), 'w').write(out + '\n')
    tail = out.splitlines()[-1] if out else ''
    report.append('  [%s] pruebas DSP offline (exit %d): %s' % ('OK' if r.returncode == 0 else 'FALLO', r.returncode, tail))
    return r.returncode == 0


# ------------------------------------------------------------------ empaquetado
def subst(text, p):
    for k, v in (('@ID@', p['id']), ('@DISPLAY@', p['display']), ('@VERSION@', p['version']),
                 ('@NAME_ES@', p['name_es']), ('@NAME_EN@', p['name_en']), ('@PRODUCT@', p['product'])):
        text = text.replace(k, v)
    return text


def write(path, text, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w').write(text)
    os.chmod(path, mode)


def mkzip(zpath, stage, prefix):
    if os.path.exists(zpath):
        os.remove(zpath)
    run([sys.executable, os.path.join(SUITE, 'scripts', 'mkarchive.py'), 'zip', zpath, stage, prefix])


def docs(dst, p=None, plugins=None):
    shutil.copy(os.path.join(SUITE, 'LICENSE'), os.path.join(dst, 'LICENSE.txt'))
    shutil.copy(os.path.join(SUITE, 'branding', 'banner.jpg'), os.path.join(dst, 'banner.jpg'))
    gk = open(os.path.join(HERE, 'gatekeeper.md')).read().strip()
    if p:
        readme = subst(open(os.path.join(HERE, 'README-plugin.md.in')).read(), p).replace('@GATEKEEPER@', gk)
        write(os.path.join(dst, 'LEEME-README.md'), readme)
        note = ('# Changelog — %s\n\n## macOS — %s\n- **Nuevo:** versión para macOS (bundle `.plugin` universal arm64 + x86_64, '
                'macOS 11+, OBS Studio 28+), con el mismo sonido, presets e interfaz que en Windows. Firmado ad-hoc, no notarizado.\n'
                '- **New:** macOS build (universal arm64 + x86_64 `.plugin`, macOS 11+, OBS Studio 28+), same sound, presets and UI '
                'as on Windows. Ad-hoc signed, not notarized.\n\n' % (p['display'], p['version']))
        orig = open(os.path.join(p['pdir'], 'CHANGELOG.md')).read()
        orig = re.sub(r'^# .*\n+', '', orig, count=1)
        write(os.path.join(dst, 'CHANGELOG.md'), note + orig)
    else:
        table = '\n'.join('| %s / %s | %s | `%s.plugin` |' % (q['name_es'], q['name_en'], q['version'], q['id']) for q in plugins)
        readme = open(os.path.join(HERE, 'README-suite.md.in')).read().replace('@TABLE@', table) \
            .replace('@GATEKEEPER@', gk).replace('@SUITE_VERSION@', SUITE_VER)
        write(os.path.join(dst, 'LEEME-README.md'), readme)
        shutil.copy(os.path.join(HERE, 'CHANGELOG-macos.md'), os.path.join(dst, 'CHANGELOG.md'))


def package_plugin(p, bundle):
    stage = os.path.join(BUILD, 'stage', p['product'])
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    shutil.copytree(bundle, os.path.join(stage, p['id'] + '.plugin'), symlinks=True)
    cmd = subst(open(os.path.join(SUITE, 'common', 'macos', 'Instalar.command.in')).read(), p)
    write(os.path.join(stage, 'Instalar - Install.command'), cmd, 0o755)
    docs(stage, p)
    z = os.path.join(OUT, '%s-%s-macos-universal.zip' % (p['product'], p['version']))
    mkzip(z, stage, '%s-%s-macos' % (p['product'], p['version']))
    return z


def shell_array(vals):
    return '(' + ' '.join(shlex.quote(v) for v in vals) + ')'


def build_pkg(plugins, bundles, pkg_path):
    """Producto .pkg con una opcion por efecto (todas marcadas). Cada componente deja el bundle en
    /Library/Application Support/Streamerdesk/ASMR/ y su postinstall lo copia a
    ~/Library/Application Support/obs-studio/plugins del usuario de la sesion (OBS 28+ solo busca ahi)."""
    work = os.path.join(BUILD, 'pkg')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    x = macpkg.Xar()
    loc = '/Library/Application Support/Streamerdesk/ASMR'
    post_t = open(os.path.join(HERE, 'pkg-postinstall.in')).read()
    choices, refs, outline = [], [], []
    for i, p in enumerate(plugins):
        pr = os.path.join(work, p['id'], 'root')
        sc = os.path.join(work, p['id'], 'scripts')
        os.makedirs(sc)
        shutil.copytree(bundles[p['id']], os.path.join(pr, p['id'] + '.plugin'), symlinks=True)
        write(os.path.join(sc, 'postinstall'), subst(post_t, p), 0o755)
        ident = p['bundle_id'] + '.pkg'
        files, kb = macpkg.component(pr, sc, ident, p['version'], loc, MKBOM)
        comp = '%s.pkg' % p['id']
        for n, d in files.items():
            x.add('%s/%s' % (comp, n), d)
        cid = 'c%d' % (i + 1)
        outline.append('<line choice="%s"/>' % cid)
        choices.append('<choice id="%s" title="%s / %s" description="%s&#10;%s" start_selected="true" visible="true">'
                       '<pkg-ref id="%s"/></choice>' % (cid, macpkg.escape(p['name_es']), macpkg.escape(p['name_en']),
                                                        macpkg.escape(p['desc_es']), macpkg.escape(p['desc_en']), ident))
        refs.append('<pkg-ref id="%s" version="%s" installKBytes="%d" auth="root" onConclusion="none">#%s</pkg-ref>'
                    % (ident, p['version'], kb, comp))
    dist = ('<?xml version="1.0" encoding="utf-8"?>\n<installer-gui-script minSpecVersion="2">\n'
            '<title>Streamerdesk ASMR Suite %s</title>\n'
            '<organization>com.streamerdesk</organization>\n'
            '<options customize="always" require-scripts="false" rootVolumeOnly="true" hostArchitectures="x86_64,arm64"/>\n'
            '<domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>\n'
            '<os-version min="11.0"/>\n'
            '<welcome file="welcome.html" mime-type="text/html"/>\n'
            '<license file="LICENSE.txt" mime-type="text/plain"/>\n'
            '<conclusion file="conclusion.html" mime-type="text/html"/>\n'
            '<background file="banner.jpg" alignment="bottomleft" scaling="proportional" mime-type="image/jpeg"/>\n'
            '<choices-outline>%s</choices-outline>\n%s\n%s\n</installer-gui-script>\n'
            % (SUITE_VER, ''.join(outline), '\n'.join(choices), '\n'.join(refs)))
    x.add('Distribution', dist.encode())
    lic = open(os.path.join(SUITE, 'LICENSE'), 'rb').read()
    for lang in ('en', 'es'):
        for n in ('welcome.html', 'conclusion.html'):
            x.add('Resources/%s.lproj/%s' % (lang, n), open(os.path.join(HERE, 'pkg-res', lang, n), 'rb').read())
        x.add('Resources/%s.lproj/LICENSE.txt' % lang, lic)
    x.add('Resources/banner.jpg', open(os.path.join(SUITE, 'branding', 'banner.jpg'), 'rb').read())
    x.write(pkg_path)
    return dist


def package_suite(plugins, bundles):
    name = 'StreamerdeskASMR-Suite-%s-macos' % SUITE_VER
    stage = os.path.join(BUILD, 'stage', name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, 'Plugins'))
    for p in plugins:
        shutil.copytree(bundles[p['id']], os.path.join(stage, 'Plugins', p['id'] + '.plugin'), symlinks=True)
    arr = {
        '@IDS@': shell_array([p['id'] for p in plugins]),
        '@NAMES_ES@': shell_array([p['name_es'] for p in plugins]),
        '@NAMES_EN@': shell_array([p['name_en'] for p in plugins]),
        '@SUITE_VERSION@': SUITE_VER,
    }
    for tpl, out in (('Instalar-Suite.command.in', 'Instalar - Install.command'),
                     ('Desinstalar-Suite.command.in', 'Desinstalar - Uninstall.command')):
        t = open(os.path.join(HERE, tpl)).read()
        for k, v in arr.items():
            t = t.replace(k, v)
        write(os.path.join(stage, out), t, 0o755)
    docs(stage, plugins=plugins)
    pkg = os.path.join(stage, 'StreamerdeskASMR-Suite-%s-macos.pkg' % SUITE_VER)
    build_pkg(plugins, bundles, pkg)
    r = subprocess.run([sys.executable, os.path.join(HERE, 'check_pkg.py'), pkg], capture_output=True, text=True)
    PKG_REPORT.append('## Instalador .pkg (%s)' % os.path.basename(pkg))
    PKG_REPORT.append('  [%s] estructura del .pkg verificada' % ('OK' if r.returncode == 0 else 'FALLO'))
    PKG_REPORT.extend(r.stdout.rstrip().splitlines() + r.stderr.rstrip().splitlines())
    PKG_REPORT.append('')
    if r.returncode:
        raise SystemExit('pkg invalido')
    shutil.copy(pkg, os.path.join(OUT, os.path.basename(pkg)))
    z = os.path.join(OUT, '%s-universal.zip' % name)
    mkzip(z, stage, name)
    return z


def main():
    for t in (RCODESIGN, MKBOM):
        if not os.access(t, os.X_OK):
            raise SystemExit('falta %s' % t)
    os.makedirs(BUILD, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    tbd = os.path.join(BUILD, 'libobs-28.0.0-macos.tbd')
    log('Stub libobs OBS 28.0.0 macOS: %d simbolos (interseccion arm64/x86_64)' % gen_tbd28(tbd))
    obs28, obs33 = obs_exports('28.0.0'), obs_exports('33.0.1')
    plugins = load_plugins()
    report = ['# Verificacion macOS - Streamerdesk ASMR Suite %s' % SUITE_VER, '',
              'libobs de referencia: OBS 28.0.0 y 33.0.1 oficiales macOS (arm64 %d/%d, x86_64 %d/%d exports)' % (
                  len(obs28['arm64']), len(obs33['arm64']), len(obs28['x86_64']), len(obs33['x86_64'])), '']
    bundles, zips, allok = {}, [], True
    for p in plugins:
        log('%s %s' % (p['display'], p['version']))
        bundle, thin = build_plugin(p, tbd)
        bundles[p['id']] = bundle
        allok &= verify_plugin(p, bundle, thin, obs28, obs33, report)
        allok &= run_tests(p, report)
        report.append('')
    for f in os.listdir(OUT):
        if f.endswith(('.zip', '.pkg')) or f.startswith('SHA256SUMS'):
            os.remove(os.path.join(OUT, f))
    log('Empaquetado')
    for p in plugins:
        zips.append(package_plugin(p, bundles[p['id']]))
    zips.append(package_suite(plugins, bundles))
    docs(OUT, plugins=plugins)
    report.extend(PKG_REPORT)
    files = sorted(f for f in os.listdir(OUT) if f.endswith(('.zip', '.pkg')))
    with open(os.path.join(OUT, 'SHA256SUMS.txt'), 'w') as f:
        for n in files:
            f.write('%s  %s\n' % (hashlib.sha256(open(os.path.join(OUT, n), 'rb').read()).hexdigest(), n))
    report.append('RESULTADO: %s' % ('TODO OK' if allok else 'HAY FALLOS'))
    open(os.path.join(BUILD, 'verify-macos.md'), 'w').write('\n'.join(report) + '\n')
    print('\n'.join(report))
    print(open(os.path.join(OUT, 'SHA256SUMS.txt')).read())
    if not allok:
        sys.exit(1)


if __name__ == '__main__':
    main()
