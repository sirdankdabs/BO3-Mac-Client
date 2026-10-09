# BO3 Mac Client + BO3MacFix

Juega **Call of Duty: Black Ops III para Mac** (versión de Steam) en los
servidores dedicados de la comunidad (lista de `master.ezz.lol`), los mismos
a los que entran los jugadores de Windows.

Este repositorio trae todo junto:

- **BO3 Mac Client**: app con lista de servidores (mapa, modo, ronda de
  Zombies, jugadores, ping) y entrada con un clic.
- **BO3MacFix** (versión 1.2-beta41): el parche del juego con T7Patch 3.09,
  arreglos de audio y stutter, XP con mods y cliente de servidores de la
  comunidad al abrir el juego desde Steam.
- Un **instalador** que lo deja todo listo.

## Instalar

1. Instala Steam, inicia sesión e instala Call of Duty: Black Ops III.
   Abre el juego una vez y ciérralo.
2. Descarga este repositorio (botón **Code → Download ZIP**, o `git clone`).
3. Doble clic en **`Install BO3 Mac Client.command`**.
   Si macOS no deja abrirlo: clic derecho → Abrir → Abrir.

El instalador:

- copia la app a Aplicaciones;
- instala BO3MacFix dentro del juego (abrir BO3 desde Steam pasa a usarlo);
- si existe la carpeta `3 - Personal (keep private)/` con tu
  `identity-key-*.dat` y/o `game-settings/`, restaura tu identidad y tu
  configuración de gráficos y controles. Si no existe, la app crea una
  identidad nueva la primera vez que entras a un servidor.

Después abre **BO3 Mac Client**, elige un servidor y pulsa **Join**.

> **Tu identidad es privada.** La clave (`boiii_key.dat` /
> `identity-key-*.dat`) es tu cuenta en los servidores: quien la tenga entra
> como tú, con tu rango y VIP. La carpeta `3 - Personal (keep private)/` está
> en `.gitignore`; no la subas ni la compartas.

## Contenido

| Carpeta | Qué hay |
| --- | --- |
| `1 - Install/BO3 Mac Client.dmg` | La app. También se instala a mano arrastrándola a Aplicaciones. |
| `1 - Install/BO3MacFix/` | El parche compilado, con `bo3macfix.conf`. A mano: copia la carpeta junto a `CoDBlkOps3.app` y abre `Install_BO3MacFix`. |
| `2 - Source Code/BO3MacClient/` | Código de la app (SwiftUI) y su motor (C, fork de BO3MacFix). |
| `2 - Source Code/BO3MacFix/` | Código de BO3MacFix con T7Patch 3.09 y el cliente de servidores. |
| `3 - Extras/herramientas/` | Análisis del ejecutable (`re/`) y `probe.py` para consultar servidores. |
| `NOTAS-TECNICAS.md` | Protocolo, direcciones del ejecutable, problemas y soluciones. |

## Opciones de BO3MacFix

Se configuran en `bo3macfix.conf` (las opciones de lanzamiento de Steam con
`env … %command%` no funcionan en Mac). El archivo incluido activa el
*ranked spoofing* (`BO3MACFIX_SPOOF_RANKED=1`, para que la XP cuente con
mods): úsalo bajo tu responsabilidad.

## Compilar

Requiere Xcode (no solo las Command Line Tools) y una ruta **sin espacios**
(el Makefile del motor no las admite):

```bash
cd "2 - Source Code/BO3MacClient"
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer ./build.sh
```

La app queda en `dist/BO3 Mac Client.app`. Para BO3MacFix, ver
`2 - Source Code/BO3MacFix/README.md`.

## Créditos y licencia

- **BO3MacFix** de Emma / InvoxiPlayGames y Anthony / serious (LGPL-2.1).
  Tanto BO3MacFix como el motor de la app son versiones modificadas
  (ver `LICENSE.txt` en cada carpeta).
- Autores de **T7Patch** (Scroptss/T7Patch-src).
- [Dobby](https://github.com/jmpews/Dobby), [LibTomCrypt / LibTomMath](https://github.com/libtom).
- El proyecto de servidores de la comunidad detrás de `master.ezz.lol`.

Sin relación con Activision, Treyarch, Aspyr ni Valve. Necesitas tu propia
copia del juego en Steam. Call of Duty y Black Ops son marcas de Activision
Publishing, Inc.
