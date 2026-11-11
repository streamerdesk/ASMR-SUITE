#!/usr/bin/env python3
"""Streamerdesk ASMR Suite - instalador combinado de Windows x64 + zip manual.

Toma los archivos TAL CUAL de los .zip de Windows ya aprobados de cada efecto (no recompila nada),
genera el script NSIS, compila el instalador, crea el zip combinado y verifica que cada archivo
embebido es identico byte a byte al de su paquete de origen.

  python3 suite-bundle/build_bundle.py            (DEPS=$ROOT/.deps por defecto)
"""
import hashlib, json, os, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEPS = os.environ.get('DEPS', os.path.join(ROOT, '.deps'))
OUT = os.environ.get('OUT', os.path.join(ROOT, 'dist'))
BUILD = os.path.join(ROOT, 'build', 'suite-bundle')
CFG = json.load(open(os.path.join(HERE, 'effects.json'), encoding='utf-8'))
VER = CFG['suite_version']
FX = CFG['effects']
BASE = f'StreamerdeskASMR-Suite-{VER}'
SUITE_ID = 'streamerdesk-asmr-suite'
fails = []


def sha(b):
    return hashlib.sha256(b).hexdigest()


def check(cond, msg):
    print(('  ok: ' if cond else '  FALLO: ') + msg)
    if not cond:
        fails.append(msg)


def payload(z, fx):
    """archivos del efecto dentro de su zip: solo obs-plugins/ y data/ (el resto son documentos)"""
    out = {}
    for n in z.namelist():
        if n.endswith('/'):
            continue
        if n.startswith('obs-plugins/') or n.startswith('data/'):
            out[n] = z.read(n)
    dll = f'obs-plugins/64bit/{fx["id"]}.dll'
    if dll not in out:
        sys.exit(f'{fx["zip"]}: falta {dll}')
    for n in out:
        if not (n == dll or n.startswith(f'data/obs-plugins/{fx["id"]}/')):
            sys.exit(f'{fx["zip"]}: archivo inesperado {n}')
    return out


# ------------------------------------------------------------------ 1. origen
print('== Paquetes de origen (comprobados contra sus SHA256SUMS)')
src_files = {}
for fx in FX:
    zp = fx['zip']
    if not os.path.isabs(zp):
        zp = os.path.join(ROOT, zp)
    fx['zip'] = zp
    d = os.path.dirname(zp)
    sums = [f for f in os.listdir(d) if f.endswith('SHA256SUMS.txt') and os.path.basename(zp).rsplit('-windows', 1)[0] in f]
    ok = False
    for s in sums:
        for line in open(os.path.join(d, s)):
            h, name = line.split()
            if name == os.path.basename(zp):
                ok = h == sha(open(zp, 'rb').read())
    check(ok, f'{os.path.basename(zp)} coincide con su SHA256SUMS')
    with zipfile.ZipFile(zp) as z:
        files = payload(z, fx)
    src_files[fx['id']] = files
    fx['dll_sha'] = sha(files[f'obs-plugins/64bit/{fx["id"]}.dll'])
    fx['kb'] = sum(len(b) for b in files.values()) // 1024 + 1

# ------------------------------------------------------------------ 2. stage
shutil.rmtree(BUILD, ignore_errors=True)
STAGE = os.path.join(BUILD, 'stage')
for fx in FX:
    for n, b in src_files[fx['id']].items():
        p = os.path.join(STAGE, n)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(b)
suite_dir = os.path.join(BUILD, 'suite-data')
os.makedirs(suite_dir)
shutil.copy(os.path.join(ROOT, 'branding', 'icon.ico'), os.path.join(suite_dir, 'icon.ico'))

# ------------------------------------------------------------------ 3. NSIS
L = {'ENGLISH': 'en', 'SPANISH': 'es', 'SPANISHINTERNATIONAL': 'es'}
T = {
    'WelcomeTitle': {'en': 'Streamerdesk ASMR Suite $VERSION', 'es': 'Streamerdesk ASMR Suite $VERSION'},
    'WelcomeText': {
        'en': 'Thank you for choosing the Streamerdesk ASMR Suite! This will install the effects you choose into OBS Studio 28 or newer: echoes, reverbs and voices that move around the listener\'s head, all made for your whispers.$\\r$\\n$\\r$\\nPlease close OBS Studio before continuing.$\\r$\\n$\\r$\\nFree software, made with love by Streamerdesk - https://streamerdesk.com',
        'es': '¡Gracias por elegir la Streamerdesk ASMR Suite! Vamos a instalar en OBS Studio 28 o posterior los efectos que elijas: ecos, reverbs y voces que se mueven alrededor de la cabeza, todo pensado para tus susurros.$\\r$\\n$\\r$\\nCierra OBS Studio antes de continuar.$\\r$\\n$\\r$\\nSoftware libre, hecho con cariño por Streamerdesk - https://streamerdesk.com'},
    'CompTop': {
        'en': 'Choose which effects to install. You can run this installer again later to add effects or to remove the ones you unchecked (your presets are always kept).',
        'es': 'Elige qué efectos quieres instalar. Puedes volver a ejecutar este instalador más adelante para añadir efectos o quitar los que desmarques (tus presets siempre se conservan).'},
    'NoneSelected': {
        'en': 'Please select at least one effect.$\\r$\\nTo remove the whole suite, use Settings > Apps > Streamerdesk ASMR Suite > Uninstall.',
        'es': 'Selecciona al menos un efecto.$\\r$\\nPara quitar toda la suite, usa Configuración > Aplicaciones > Streamerdesk ASMR Suite > Desinstalar.'},
    'DirText': {'en': 'Select the OBS Studio installation folder (detected automatically).',
                'es': 'Selecciona la carpeta de instalación de OBS Studio (detectada automáticamente).'},
    'NoObs': {'en': 'OBS Studio was not found in this folder (bin\\64bit\\obs64.exe is missing).$\\r$\\nInstall here anyway?',
              'es': 'No se encontró OBS Studio en esta carpeta (falta bin\\64bit\\obs64.exe).$\\r$\\n¿Instalar aquí de todos modos?'},
    'ObsRunning': {'en': 'OBS Studio seems to be running. Please close it and press Retry.',
                   'es': 'Parece que OBS Studio está abierto. Ciérralo y pulsa Reintentar.'},
    'Installed': {'en': 'installed', 'es': 'instalado'},
    'InstalledV': {'en': 'installed v', 'es': 'instalado v'},
    'FinishText': {
        'en': 'All done! Open OBS Studio, right-click your microphone in the Audio Mixer > Filters > + and pick any “Streamerdesk ASMR” effect. Put your headphones on and enjoy.',
        'es': '¡Todo listo! Abre OBS Studio, haz clic derecho en tu micrófono en el Mezclador de audio > Filtros > + y elige cualquier efecto «Streamerdesk ASMR». Ponte los auriculares y disfruta.'},
}
for fx in FX:
    T[f'Sec{fx["key"]}'] = {'en': f'{fx["en"]} {fx["version"]}', 'es': f'{fx["es"]} {fx["version"]}'}
    T[f'Desc{fx["key"]}'] = {'en': fx['en_desc'], 'es': fx['es_desc']}

def q(s):
    return s.replace('"', '$\\"')

nsi = [f'''; Streamerdesk ASMR Suite - instalador combinado (generado por suite-bundle/build_bundle.py)
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "Sections.nsh"

!define SUITE_ID "{SUITE_ID}"
!define DISPLAY "Streamerdesk ASMR Suite"
!define VERSION "{VER}"
!define VENDOR "Streamerdesk"
!define URL "https://streamerdesk.com"
!define UNINST_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\${{SUITE_ID}}"
!define COMP_KEY "Software\\Streamerdesk\\ASMR Suite\\Components"
!define INDIV_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
!define STAGE "{STAGE}"

Name "${{DISPLAY}} ${{VERSION}}"
!ifdef TEST_OUTFILE
OutFile "${{TEST_OUTFILE}}"
!else
OutFile "{os.path.join(OUT, BASE + '-windows-installer.exe')}"
!endif
InstallDir "$PROGRAMFILES64\\obs-studio"
RequestExecutionLevel admin
BrandingText "${{VENDOR}} - ${{URL}}"

VIProductVersion "${{VERSION}}.0"
VIAddVersionKey /LANG=0 "ProductName" "${{DISPLAY}}"
VIAddVersionKey /LANG=0 "CompanyName" "${{VENDOR}}"
VIAddVersionKey /LANG=0 "FileDescription" "${{DISPLAY}} - OBS Studio plugins installer"
VIAddVersionKey /LANG=0 "FileVersion" "${{VERSION}}"
VIAddVersionKey /LANG=0 "ProductVersion" "${{VERSION}}"
VIAddVersionKey /LANG=0 "LegalCopyright" "(C) 2026 ${{VENDOR}} - GPL-2.0-or-later"

!define MUI_ICON "{ROOT}/branding/icon.ico"
!define MUI_UNICON "{ROOT}/branding/icon.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "{ROOT}/branding/nsis-welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "{ROOT}/branding/nsis-welcome.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "{ROOT}/branding/nsis-header.bmp"
!define MUI_HEADERIMAGE_RIGHT
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC

Var OldDir     ; carpeta de una instalacion previa de la suite ("" si no hay)

!define MUI_WELCOMEPAGE_TITLE "$(WelcomeTitle)"
!define MUI_WELCOMEPAGE_TEXT "$(WelcomeText)"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "{ROOT}/LICENSE"
!define MUI_COMPONENTSPAGE_TEXT_TOP "$(CompTop)"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE ComponentsLeave
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_DIRECTORYPAGE_TEXT_TOP "$(DirText)"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "$(FinishText)"
!define MUI_FINISHPAGE_LINK "${{URL}}"
!define MUI_FINISHPAGE_LINK_LOCATION "${{URL}}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Idiomas: el primero es el de reserva; NSIS elige automaticamente el del sistema.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "SpanishInternational"
''']
for k, v in T.items():
    for lang, code in L.items():
        nsi.append(f'LangString {k} ${{LANG_{lang}}} "{q(v[code]).replace("$VERSION", "${VERSION}")}"')
nsi.append(r'''
; ---- instalar / quitar un efecto (los presets de usuario en %APPDATA%\obs-studio\plugin_config\<id> nunca se tocan)
; Instaladores individuales (common/windows/installer.nsi.in, identico en los 6 efectos):
;   HKLM (vista 64 bits) Software\Microsoft\Windows\CurrentVersion\Uninstall\<id>
;     DisplayName "Streamerdesk ASMR - <efecto> (OBS Studio)", DisplayVersion, Publisher "Streamerdesk",
;     InstallLocation <carpeta OBS>, UninstallString <obs>\data\obs-plugins\<id>\uninstall.exe
;   archivos: <obs>\obs-plugins\64bit\<id>.dll y <obs>\data\obs-plugins\<id>\ (incluye uninstall.exe e icon.ico)
; Por robustez tambien se miran la vista de 32 bits (Wow6432Node) y HKCU.

; copia individual en otra carpeta de OBS distinta del destino: se quita (la suite la sustituye)
!macro REMOVE_OLD_COPY LOC ID
  ${If} ${LOC} != ""
  ${AndIf} ${LOC} != $INSTDIR
  ${AndIf} ${FileExists} "${LOC}\obs-plugins\64bit\${ID}.dll"
    Delete "${LOC}\obs-plugins\64bit\${ID}.dll"
    Delete "${LOC}\obs-plugins\64bit\${ID}.pdb"
    RMDir /r "${LOC}\data\obs-plugins\${ID}"
  ${EndIf}
!macroend

; la suite pasa a gestionar este efecto: quita la entrada individual de Aplicaciones (solo si es de Streamerdesk)
!macro TAKEOVER_ROOT ROOT ID
  ReadRegStr $5 ${ROOT} "${INDIV_KEY}\${ID}" "Publisher"
  ${If} $5 == "${VENDOR}"
    ReadRegStr $7 ${ROOT} "${INDIV_KEY}\${ID}" "InstallLocation"
    !insertmacro REMOVE_OLD_COPY "$7" "${ID}"
    DeleteRegKey ${ROOT} "${INDIV_KEY}\${ID}"
  ${EndIf}
!macroend

!macro INSTALL_FX ID VER
  ${If} ${RunningX64}
    SetRegView 32
    !insertmacro TAKEOVER_ROOT HKLM "${ID}"
    SetRegView 64
  ${EndIf}
  !insertmacro TAKEOVER_ROOT HKLM "${ID}"
  !insertmacro TAKEOVER_ROOT HKCU "${ID}"
  ; carpeta de datos limpia (quita restos y el uninstall.exe de la instalacion individual)
  RMDir /r "$INSTDIR\data\obs-plugins\${ID}"
  SetOutPath "$INSTDIR\obs-plugins\64bit"
  File "${STAGE}\obs-plugins\64bit\${ID}.dll"
  SetOutPath "$INSTDIR\data\obs-plugins\${ID}"
  File /r "${STAGE}\data\obs-plugins\${ID}\*.*"
  WriteRegStr HKLM "${COMP_KEY}" "${ID}" "${VER}"
!macroend

; instalacion individual: $6=1 si existe, $5=version, $7=carpeta
!macro STANDALONE_ROOT ROOT ID
  ${If} $6 == 0
    ReadRegStr $1 ${ROOT} "${INDIV_KEY}\${ID}" "DisplayName"
    ReadRegStr $2 ${ROOT} "${INDIV_KEY}\${ID}" "UninstallString"
    ${If} $1 != ""
    ${OrIf} $2 != ""
      StrCpy $6 1
      ReadRegStr $5 ${ROOT} "${INDIV_KEY}\${ID}" "DisplayVersion"
      ReadRegStr $7 ${ROOT} "${INDIV_KEY}\${ID}" "InstallLocation"
    ${EndIf}
  ${EndIf}
!macroend

!macro STANDALONE_INFO ID
  StrCpy $5 ""
  StrCpy $6 0
  StrCpy $7 ""
  !insertmacro STANDALONE_ROOT HKLM "${ID}"
  ${If} ${RunningX64}
    SetRegView 32
    !insertmacro STANDALONE_ROOT HKLM "${ID}"
    SetRegView 64
  ${EndIf}
  !insertmacro STANDALONE_ROOT HKCU "${ID}"
!macroend

; marca en la lista los efectos ya instalados (por la suite, por su instalador individual o copiados a mano)
!macro DETECT_FX ID KEY
  StrCpy $3 ""
  StrCpy $4 0
  StrCpy $8 ""
  ${If} $OldDir != ""
    ReadRegStr $8 HKLM "${COMP_KEY}" "${ID}"
  ${EndIf}
  ${If} $8 != ""
    StrCpy $3 $8
    StrCpy $4 1
  ${EndIf}
  !insertmacro STANDALONE_INFO "${ID}"
  ${If} $6 == 1
    StrCpy $4 1
    ${If} $3 == ""
      StrCpy $3 $5
    ${EndIf}
  ${EndIf}
  ${If} ${FileExists} "$INSTDIR\obs-plugins\64bit\${ID}.dll"
    StrCpy $4 1
  ${EndIf}
  ${If} $4 == 1
    ${If} $3 != ""
      SectionSetText ${Sec${KEY}} "$(Sec${KEY})  ($(InstalledV)$3)"
    ${Else}
      SectionSetText ${Sec${KEY}} "$(Sec${KEY})  ($(Installed))"
    ${EndIf}
  ${EndIf}
  ; suite ya instalada: se marcan solo los efectos que gestiona la suite (primera vez: todos marcados)
  ${If} $OldDir != ""
  ${AndIf} $8 == ""
    !insertmacro UnselectSection ${Sec${KEY}}
  ${EndIf}
!ifdef TEST_DUMP
  SectionGetText ${Sec${KEY}} $1
  SectionGetFlags ${Sec${KEY}} $2
  IntOp $2 $2 & ${SF_SELECTED}
  FileWrite $9 "${ID}|sel=$2|$1$\r$\n"
!endif
!macroend

!macro REMOVE_FX DIR ID
  ReadRegStr $0 HKLM "${COMP_KEY}" "${ID}"
  ${If} $0 != ""
    Delete "${DIR}\obs-plugins\64bit\${ID}.dll"
    Delete "${DIR}\obs-plugins\64bit\${ID}.pdb"
    RMDir /r "${DIR}\data\obs-plugins\${ID}"
    DeleteRegValue HKLM "${COMP_KEY}" "${ID}"
  ${EndIf}
!macroend

; OBS abierto => obs64.exe y las DLL cargadas estan bloqueadas para escritura (no se modifica nada)
!macro OBS_RUNNING_CHECK DIR RESULT
  StrCpy ${RESULT} 0
  ${If} ${FileExists} "${DIR}\bin\64bit\obs64.exe"
    ClearErrors
    FileOpen $9 "${DIR}\bin\64bit\obs64.exe" a
    ${If} ${Errors}
      StrCpy ${RESULT} 1
    ${Else}
      FileClose $9
    ${EndIf}
  ${EndIf}
''')
for fx in FX:
    nsi.append(f'''  ${{If}} ${{FileExists}} "${{DIR}}\\obs-plugins\\64bit\\{fx["id"]}.dll"
    ClearErrors
    FileOpen $9 "${{DIR}}\\obs-plugins\\64bit\\{fx["id"]}.dll" a
    ${{If}} ${{Errors}}
      StrCpy ${{RESULT}} 1
    ${{Else}}
      FileClose $9
    ${{EndIf}}
  ${{EndIf}}''')
nsi.append('!macroend\n')

nsi.append(r'''Section "-Pre"
  SetRegView 64
  ; si la suite estaba instalada en otra carpeta de OBS, se mueve: quitar lo de la carpeta antigua
  ${If} $OldDir != ""
  ${AndIf} $OldDir != $INSTDIR''')
for fx in FX:
    nsi.append(f'    !insertmacro REMOVE_FX "$OldDir" "{fx["id"]}"')
nsi.append(r'''    RMDir /r "$OldDir\data\obs-plugins\${SUITE_ID}"
  ${EndIf}
SectionEnd
''')
for fx in FX:
    nsi.append(f'''Section "$(Sec{fx["key"]})" Sec{fx["key"]}
  !insertmacro INSTALL_FX "{fx["id"]}" "{fx["version"]}"
SectionEnd
''')
nsi.append(r'''Section "-Post"
  ; efectos desmarcados que la suite habia instalado antes: se quitan (los presets se conservan)
  StrCpy $8 0''')
for fx in FX:
    nsi.append(f'''  ${{If}} ${{SectionIsSelected}} ${{Sec{fx["key"]}}}
    IntOp $8 $8 + {fx["kb"]}
  ${{Else}}
    !insertmacro REMOVE_FX "$INSTDIR" "{fx["id"]}"
  ${{EndIf}}''')
nsi.append(r'''  SetOutPath "$INSTDIR\data\obs-plugins\${SUITE_ID}"
  File "''' + os.path.join(suite_dir, 'icon.ico') + r'''"
  WriteUninstaller "$INSTDIR\data\obs-plugins\${SUITE_ID}\uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${DISPLAY} (OBS Studio)"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${VENDOR}"
  WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "${URL}"
  WriteRegStr HKLM "${UNINST_KEY}" "URLUpdateInfo" "${URL}"
  WriteRegStr HKLM "${UNINST_KEY}" "HelpLink" "${URL}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\data\obs-plugins\${SUITE_ID}\icon.ico"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\data\obs-plugins\${SUITE_ID}\uninstall.exe$\""
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\data\obs-plugins\${SUITE_ID}\uninstall.exe$\" /S"
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $8
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN''')
for fx in FX:
    nsi.append(f'  !insertmacro MUI_DESCRIPTION_TEXT ${{Sec{fx["key"]}}} $(Desc{fx["key"]})')
nsi.append(r'''!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
  ; carpeta de OBS desde el registro (la escribe el instalador oficial de OBS); si no, C:\Program Files\obs-studio
  ReadRegStr $0 HKLM "SOFTWARE\OBS Studio" ""
  ${If} $0 == ""
    ReadRegStr $0 HKCU "SOFTWARE\OBS Studio" ""
  ${EndIf}
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
  ; suite ya instalada: misma carpeta
  ReadRegStr $OldDir HKLM "${UNINST_KEY}" "InstallLocation"
  ${If} $OldDir != ""
    StrCpy $INSTDIR $OldDir
  ${ElseIf} $0 == ""
    ; OBS sin registro (p. ej. portable): la carpeta donde se instalo algun efecto individual''')
for fx in FX:
    nsi.append(f'''    ${{If}} $0 == ""
      !insertmacro STANDALONE_INFO "{fx["id"]}"
      ${{If}} $7 != ""
        StrCpy $0 $7
        StrCpy $INSTDIR $7
      ${{EndIf}}
    ${{EndIf}}''')
nsi.append(r'''  ${EndIf}
  ; la carpeta de OBS ya esta resuelta: ahora se detecta que efectos estan instalados
!ifdef TEST_DUMP
  FileOpen $9 "$TEMP\sd-suite-detect.txt" w
  FileWrite $9 "lang=$LANGUAGE|instdir=$INSTDIR|olddir=$OldDir$\r$\n"
!endif''')
for fx in FX:
    nsi.append(f'  !insertmacro DETECT_FX "{fx["id"]}" {fx["key"]}')
nsi.append(r'''!ifdef TEST_DUMP
  FileClose $9
!endif
FunctionEnd

Function ComponentsLeave
  StrCpy $0 0''')
for fx in FX:
    nsi.append(f'''  ${{If}} ${{SectionIsSelected}} ${{Sec{fx["key"]}}}
    IntOp $0 $0 + 1
  ${{EndIf}}''')
nsi.append(r'''  ${If} $0 == 0
    MessageBox MB_OK|MB_ICONINFORMATION "$(NoneSelected)"
    Abort
  ${EndIf}
FunctionEnd

Function DirLeave
  ${IfNot} ${FileExists} "$INSTDIR\bin\64bit\obs64.exe"
    MessageBox MB_YESNO|MB_ICONEXCLAMATION "$(NoObs)" IDYES +2
    Abort
  ${EndIf}
  retry:
  !insertmacro OBS_RUNNING_CHECK "$INSTDIR" $1
  ${If} $1 == 1
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(ObsRunning)" IDRETRY retry
    Abort
  ${EndIf}
FunctionEnd

Function un.onInit
  ${If} ${RunningX64}
    SetRegView 64
  ${EndIf}
  ; el desinstalador vive en <obs>\data\obs-plugins\streamerdesk-asmr-suite\ -> subir 3 niveles
  ReadRegStr $0 HKLM "${UNINST_KEY}" "InstallLocation"
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${Else}
    GetFullPathName $INSTDIR "$INSTDIR\..\..\.."
  ${EndIf}
  retry:
  !insertmacro OBS_RUNNING_CHECK "$INSTDIR" $1
  ${If} $1 == 1
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(ObsRunning)" IDRETRY retry
    Quit
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetRegView 64
  ; solo los efectos que instalo la suite; los presets de usuario se conservan''')
for fx in FX:
    nsi.append(f'  !insertmacro REMOVE_FX "$INSTDIR" "{fx["id"]}"')
nsi.append(r'''  RMDir /r "$INSTDIR\data\obs-plugins\${SUITE_ID}"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey /ifempty HKLM "${COMP_KEY}"
  DeleteRegKey /ifempty HKLM "Software\Streamerdesk\ASMR Suite"
  DeleteRegKey /ifempty HKLM "Software\Streamerdesk"
SectionEnd
''')
os.makedirs(OUT, exist_ok=True)
nsi_path = os.path.join(BUILD, 'installer-suite.nsi')
txt = '\n'.join(nsi)
# NSIS en Linux acepta '/' en rutas File
txt = '\n'.join(l.replace('\\', '/') if l.strip().startswith('File ') else l for l in txt.split('\n'))
open(nsi_path, 'w', encoding='utf-8-sig').write(txt)

for f in os.listdir(OUT):
    if f.startswith(BASE + '-'):
        os.remove(os.path.join(OUT, f))
makensis = shutil.which('makensis') or os.path.join(DEPS, 'nsis/root/usr/bin/makensis')
env = dict(os.environ)
if not shutil.which('makensis'):
    env['NSISDIR'] = os.path.join(DEPS, 'nsis/root/usr/share/nsis')
print('== Compilacion NSIS (avisos tratados como errores: -WX)')
r = subprocess.run([makensis, '-V2', '-WX', nsi_path], env=env, capture_output=True, text=True)
print(r.stdout[-2000:], r.stderr[-2000:])
check(r.returncode == 0, 'el script NSIS compila sin errores ni avisos')
if r.returncode:
    sys.exit(1)
# variante de prueba (solo build/, nunca en dist/): vuelca en %TEMP%\sd-suite-detect.txt lo detectado en .onInit
test_exe = os.path.join(BUILD, BASE + '-TEST-detect.exe')
r = subprocess.run([makensis, '-V2', '-WX', '-DTEST_DUMP', '-DTEST_OUTFILE=' + test_exe, nsi_path], env=env, capture_output=True, text=True)
check(r.returncode == 0, 'variante de prueba (TEST_DUMP) compila: ' + test_exe)

# ------------------------------------------------------------------ 4. zip combinado
print('== Zip combinado')
zip_path = os.path.join(OUT, BASE + '-windows.zip')
docs = {
    'LEEME-README.md': open(os.path.join(HERE, 'README.md'), encoding='utf-8').read().replace('(../branding/banner.jpg)', '(banner.jpg)').encode(),
    'CHANGELOG.md': open(os.path.join(HERE, 'CHANGELOG.md'), 'rb').read(),
    'LICENSE.txt': open(os.path.join(ROOT, 'LICENSE'), 'rb').read(),
    'banner.jpg': open(os.path.join(ROOT, 'branding', 'banner.jpg'), 'rb').read(),
}
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    entries = dict(docs)
    for fx in FX:
        entries.update(src_files[fx['id']])
    for n in sorted(entries):
        zi = zipfile.ZipInfo(n, date_time=(2026, 11, 11, 0, 0, 0))
        zi.external_attr = 0o100644 << 16
        zi.create_system = 3
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, entries[n])

# ------------------------------------------------------------------ 5. verificacion
print('== Verificacion del zip combinado')
with zipfile.ZipFile(zip_path) as z:
    check(z.testzip() is None, 'zip integro (CRC correctos)')
    names = set(z.namelist())
    expect = set(docs)
    for fx in FX:
        expect |= set(src_files[fx['id']])
    check(names == expect, f'contenido exacto: {len(names)} archivos (6 DLL + datos de 6 efectos + README, LICENSE, CHANGELOG, banner)')
    dlls = sorted(n for n in names if n.startswith('obs-plugins/'))
    check(len(dlls) == 6 and all(n.startswith('obs-plugins/64bit/') and n.endswith('.dll') for n in dlls), 'obs-plugins/64bit/: ' + ', '.join(os.path.basename(d) for d in dlls))
    for fx in FX:
        same = all(z.read(n) == b for n, b in src_files[fx['id']].items())
        check(same, f'{fx["es"]} {fx["version"]}: {len(src_files[fx["id"]])} archivos identicos al paquete de origen; DLL sha256 {fx["dll_sha"][:16]}...')

print('== Verificacion del instalador (extraido con 7-Zip)')
exe = os.path.join(OUT, BASE + '-windows-installer.exe')
sz = os.path.join(DEPS, '7zip', '7zz')
if os.path.exists(sz):
    ex = os.path.join(BUILD, 'exe-extract')
    subprocess.run([sz, 'x', '-y', '-o' + ex, exe], capture_output=True)
    for fx in FX:
        ok = all(os.path.exists(os.path.join(ex, n)) and open(os.path.join(ex, n), 'rb').read() == b
                 for n, b in src_files[fx['id']].items())
        check(ok, f'{fx["es"]}: DLL y datos embebidos identicos al paquete de origen')
    extra = []
    for d, _, fs in os.walk(ex):
        for f in fs:
            rel = os.path.relpath(os.path.join(d, f), ex).replace(os.sep, '/')
            if not rel.startswith('$') and rel not in expect and not rel.endswith('icon.ico'):
                extra.append(rel)
    check(not extra, 'sin archivos inesperados en el instalador' + (f': {extra}' if extra else ''))
else:
    print('  (7-Zip no disponible: se omite la extraccion del instalador)')

with open(os.path.join(OUT, BASE + '-SHA256SUMS.txt'), 'w') as f:
    for n in sorted([BASE + '-windows-installer.exe', BASE + '-windows.zip']):
        f.write(f'{sha(open(os.path.join(OUT, n), "rb").read())}  {n}\n')
print(open(os.path.join(OUT, BASE + '-SHA256SUMS.txt')).read())
print('== DLL incluidas (sha256)')
for fx in FX:
    print(f'  {fx["id"]}.dll  {fx["version"]}  {fx["dll_sha"]}')
print('\nRESULTADO SUITE: ' + ('OK' if not fails else f'{len(fails)} FALLOS'))
sys.exit(1 if fails else 0)
