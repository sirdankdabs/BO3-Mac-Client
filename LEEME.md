# BO3MacFix 1.2-beta4-t7patch3.09

Basado en BO3MacFix `985f0c2` (último commit de InvoxiPlayGames/BO3MacFix) con la capa T7 de la versión `t7compat.2`, actualizada a **T7Patch 3.09** (`Scroptss/T7Patch-src` `16b9b50`, 1 oct 2026).

Todos los binarios son x86_64 y se ejecutan con Rosetta, igual que el juego.

## Qué cambia respecto a t7compat.2

### Funciones del juego localizadas en el ejecutable de Mac
El ejecutable de Mac no tiene símbolos, así que en t7compat.2 **no se activaba ningún hook interno (0 de 27)**. Esta versión trae un mapa integrado (`source/t7_mac_symbols.h`) de funciones localizadas por análisis estático. Solo se usa si el SHA-1 del ejecutable es `5e42371b6a16f2b11e637f48e38554625570aa9f` (Mac v1.0.0.0, in-game v98) **y** los primeros 16 bytes de cada función coinciden con los registrados. Si algo no cuadra, ese hook no se instala.

| Función Mac | Dirección | Protección |
| --- | --- | --- |
| `LobbyMsgRW_PackageInt` / `PackageUInt` | `0xf41d33` / `0xf41db0` | Límites de tipos de lobby, miembros, votos y settings; **rechazo de `datamask` = 0 (nuevo en 3.09)** |
| `LobbyMsgRW_PackageUChar` | `0xf41fc0` | Normaliza `nattype` a ≤ 4 |
| `LobbyMsgRW_PrintMessage` | `0xf417ae` | Desactivado, como en T7Patch |
| `LobbyMsgRW_ReadDebugSection` | `0xf41836` | Límite de anidamiento (16) contra mensajes que desbordan la pila |
| `CL_ConnectionlessCMD` | `0xe19402` | Solo deja pasar los comandos de la lista de T7Patch (más voz/relay de Mac) |
| `CMD_MenuResponse_f` | `0xf451d1` | Lógica de 3.09: expulsa a un cliente no anfitrión que mande `killserverpc` o `restart_level_zm`, o `endgame`/`endround` fuera de campaña. **Permite las transiciones de campaña (arreglo de 3.09)** |
| `CL_GetConfigString` | `0xe2e90a` | Bloquea `mspreload` en las config strings 3514/3627 fuera de campaña |

Funciones auxiliares verificadas igual: `SV_Cmd_ArgvBuffer`, `MSG_BeginReading`, `MSG_ReadLong`, `MSG_ReadStringLine`, `Cbuf_AddText`.

### Correcciones a la capa de Codex
- **Filtro de paquetes sin conexión:** la versión anterior leía `Cmd_Argv` al entrar en la función. En Mac la propia función es la que tokeniza el paquete, así que habría filtrado con datos viejos. Ahora el comando se lee de una copia privada del mensaje con los lectores `MSG_*` del juego.
- **`PrintDebugMessage`:** en Mac no existe esa función por separado. La que lee las secciones de depuración forma parte de la lectura normal de mensajes, así que anularla habría roto el lobby. Se sustituyó por un límite de recursión.
- **`BG_Cache_*`:** ya no se intentan enganchar. En T7Patch estos hooks están comentados (desactivados).
- **`CMD_MenuResponseCached_f`:** no existe en esta versión de Mac. Se eliminó.

### Otros cambios de T7Patch 3.09 portados
- `Mods_SubscribeUGC`: la instalación automática de mods del Workshop pasa a ser opcional (`BO3MACFIX_AUTOINSTALLMODS=1`). El hook solo se activa si la función está en el mapa, y por ahora no lo está.

### No portado a propósito
- Ranked spoofing, desbloqueos de inventario y spoof de propiedad (`SPOOF_RANKED`, `SPOOF_UNLOCK`, `SPOOF_OWNERSHIP`): en T7Patch vienen desactivados por defecto y pueden suponer un riesgo de baneo.
- Hooks sin dirección verificada en Mac: `SEH_ReplaceDirectiveInStringWithBinding`, `UI_DoModelStringReplacement`, `UI_Model_*`, `qmemcpy`, `ExecLuaCMD`, `UI_BrowserOpen`, `LiveInvites_*`, `Scr_GscObjLink`, `CL_SwitchState_Idle_Update` y `LiveSteam_InitServer`. En el log aparecen como "not mapped for this executable".

## Comprobar que funciona
Después de abrir el juego, mira `BO3MacFix/t7-status.log`. Deberías ver líneas `active` para las funciones de la tabla y un resumen `T7 optional hooks: 9 installed, …`.

## Volver a la versión anterior
En esta carpeta están `BO3MacFix.dylib.t7compat2-backup`, `LEEME.t7compat2-backup.md` y `SHA256SUMS.t7compat2-backup`. Para volver atrás, copia el backup sobre `BO3MacFix.dylib`.

## Variables útiles (opciones de lanzamiento de Steam)
| Variable | Efecto |
| --- | --- |
| `BO3MACFIX_DISABLE_OPTIONAL_HOOKS=1` | Desactiva todos los hooks T7 internos |
| `BO3MACFIX_DISABLE_STEAM_FILTERS=1` | Desactiva los filtros de Steam (P2P, chat, lobby solo para amigos) |
| `BO3MACFIX_AUTOINSTALLMODS=1` | Permite que el juego suscriba mods del Workshop automáticamente |
| `BO3MACFIX_SYMBOL_MAP=/ruta` | Mapa adicional de funciones (mismo formato y verificación) |

## Pruebas realizadas
`make test`: las 8 suites anteriores más `dobby_prologue_test`, que comprueba que Dobby reubica bien los inicios de función con `jcc` corto y `lea` relativo a RIP, que son los que tienen las funciones del mapa. **Todavía no se ha probado dentro de una partida.**
