# Política de firma de código / Code signing policy

**Free code signing provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).**

Este documento es la **política de firma de código** (*Code signing policy*) del proyecto **Streamerdesk ASMR Suite**. Cumple las condiciones publicadas por SignPath Foundation en <https://signpath.org/terms.html>.

This document is the **Code signing policy** for the **Streamerdesk ASMR Suite**. It follows the SignPath Foundation conditions at <https://signpath.org/terms.html>.

---

## Español

### Estado

Estamos solicitando el programa gratuito de firma de código de SignPath Foundation. **Hasta que la solicitud se apruebe y se publique la primera versión firmada, los instaladores oficiales de Windows no llevan firma digital.** Cuando eso ocurra, actualizaremos este documento y la página de descargas, y el publicador que verá Windows será **SignPath Foundation**, porque el certificado es de la fundación, no de Streamerdesk.

### Qué se firma

Solo se firman artefactos de Windows construidos de forma automática a partir del código de este repositorio:

- el instalador de la suite (`.exe`);
- los instaladores individuales de cada efecto (`.exe`);
- las bibliotecas de los plugins (`.dll`) incluidas en esos instaladores, cuando la configuración de artefactos de SignPath lo permita (firma en profundidad).

No se firman con el certificado de este proyecto:

- binarios de terceros ni de proyectos «upstream» (por ejemplo, OBS Studio / `obs.dll`, que el usuario ya tiene instalado);
- paquetes de macOS o Linux (esta política cubre la firma Authenticode de Windows);
- compilaciones hechas a mano en un ordenador local.

Las descargas oficiales siguen estando en [streamerdesk.com/rincondeasmr.php](https://streamerdesk.com/rincondeasmr.php). El código fuente canónico es [github.com/streamerdesk/ASMR-SUITE](https://github.com/streamerdesk/ASMR-SUITE).

### Cómo se construye y se firma

- Los instaladores de Windows se generan con GitHub Actions, en el flujo [`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml), usando ejecutores alojados por GitHub.
- Solo esos artefactos de CI se envían a SignPath.io para firmarse. La clave privada se genera y se guarda en el HSM de SignPath; este proyecto no la almacena.
- Cada solicitud de firma requiere una **aprobación manual** de un aprobador listado más abajo. No hay firma automática sin esa aprobación.
- El equipo responsable de la firma es el mismo que mantiene el código y es dueño del repositorio.

### Roles del equipo (un solo maintainer)

Este es un proyecto con un único responsable. La misma persona ocupa los tres roles que pide SignPath Foundation. Eso es compatible con las condiciones de la fundación (el equipo debe tener los roles claros; no exige que sean personas distintas), y cada solicitud de firma se aprueba a mano.

- **Autores / committers** (pueden modificar el código en el repositorio sin una revisión adicional de otra persona):
  - Jose Angel Jerez Santos ([@streamerdesk](https://github.com/streamerdesk))
- **Revisores** (revisan los cambios propuestos por quienes no son committers, por ejemplo un pull request externo):
  - Jose Angel Jerez Santos ([@streamerdesk](https://github.com/streamerdesk))
  - Política: todo pull request externo se revisa y se acepta o se rechaza por el maintainer antes de integrarlo.
- **Aprobadores** (aprueban cada solicitud de firma):
  - Jose Angel Jerez Santos ([@streamerdesk](https://github.com/streamerdesk))
  - Política: cada solicitud de firma requiere la aprobación explícita del maintainer en SignPath.io.

Todos los miembros del equipo usan autenticación multifactor (MFA) tanto en GitHub como en SignPath.

### Política de privacidad

Este programa no transferirá ninguna información a otros sistemas en red salvo que lo pida de forma específica la persona usuaria o quien lo instala o lo utiliza.

*This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.*

Los filtros solo procesan el audio en el ordenador, dentro de OBS Studio. Los presets se guardan en local. No hay telemetría, cuentas ni llamadas de red. Los plugins no usan los inicios de sesión de Streamerdesk (Twitch, Google u otros), que sí describe la [política de privacidad del sitio web](https://streamerdesk.com/privacidad.php) para las herramientas online del resto de la plataforma.

### Licencia

Software libre bajo **GPL-2.0-or-later**, sin una licencia comercial paralela del mismo código. Ver [LICENSE](LICENSE).

---

## English

### Status

We are applying to the SignPath Foundation free open-source code signing program. **Until the application is approved and the first signed release is published, official Windows installers are not digitally signed.** After that, this document and the download page will be updated, and Windows will show **SignPath Foundation** as the publisher, because the certificate belongs to the foundation, not to Streamerdesk.

### What is signed

Only Windows artifacts built automatically from the source in this repository are signed:

- the suite installer (`.exe`);
- the standalone installer for each effect (`.exe`);
- the plugin libraries (`.dll`) bundled in those installers, when SignPath deep signing is configured.

The project certificate is not used to sign:

- third-party or upstream binaries (for example OBS Studio / `obs.dll`, which the user already has installed);
- macOS or Linux packages (this policy covers Windows Authenticode signing);
- builds made by hand on a local machine.

Official downloads remain at [streamerdesk.com/rincondeasmr.php](https://streamerdesk.com/rincondeasmr.php). The canonical source repository is [github.com/streamerdesk/ASMR-SUITE](https://github.com/streamerdesk/ASMR-SUITE).

### Build and signing process

- Windows installers are built by GitHub Actions in [`.github/workflows/build-windows.yml`](.github/workflows/build-windows.yml), on GitHub-hosted runners.
- Only those CI artifacts are submitted to SignPath.io for signing. The private key is generated and stored in SignPath’s HSM; this project does not hold it.
- Every signing request requires **manual approval** by an approver listed below. There is no unattended signing without that approval.
- The team responsible for code signing is the same team that develops and maintains the project and owns the source repository.

### Team roles (single maintainer)

This is a single-maintainer project. The same person holds the three roles required by SignPath Foundation. The Foundation’s conditions ask for clearly assigned roles; they do not require those roles to be filled by different people. Each signing request is still approved by hand.

- **Authors / committers** (trusted to modify source in the project’s version control system without additional reviews):
  - Jose Angel Jerez Santos ([@streamerdesk](https://github.com/streamerdesk))
- **Reviewers** (review changes proposed by people who are not committers, for example pull requests):
  - Jose Angel Jerez Santos ([@streamerdesk](https://github.com/streamerdesk))
  - Policy: every external pull request is reviewed by the maintainer before it is merged.
- **Approvers** (approve each signing request):
  - Jose Angel Jerez Santos ([@streamerdesk](https://github.com/streamerdesk))
  - Policy: each signing request requires the maintainer’s explicit approval in SignPath.io.

All team members use multi-factor authentication (MFA) for both GitHub and SignPath.

### Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.

The filters only process audio on the user’s computer, inside OBS Studio. Presets are stored locally. There is no telemetry, no accounts and no network traffic. The plugins do not use Streamerdesk website logins (Twitch, Google or others), which are described in the [website privacy policy](https://streamerdesk.com/privacidad.php) for the platform’s online tools.

### License

Free software under **GPL-2.0-or-later**, with no commercial dual license of the same code. See [LICENSE](LICENSE).
