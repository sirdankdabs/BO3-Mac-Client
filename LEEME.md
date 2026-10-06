# BO3MacFix 1.2-beta6-t7patch3.09

Basado en BO3MacFix `985f0c2` (último commit de InvoxiPlayGames/BO3MacFix) con la capa T7 de la versión `t7compat.2`, actualizada a **T7Patch 3.09** (`Scroptss/T7Patch-src` `16b9b50`, 1 oct 2026).

Todos los binarios son x86_64 y se ejecutan con Rosetta, igual que el juego.

## Qué cambia respecto a t7compat.2

### Funciones del juego localizadas en el ejecutable de Mac
El ejecutable de Mac no tiene símbolos, así que en t7compat.2 **no se activaba ningún hook interno (0 de 27)**. Esta versión trae un mapa integrado (`source/t7_mac_symbols.h`) de funciones localizadas por análisis estático. Solo se usa si el SHA-1 del ejecutable es `5e42371b6a16f2b11e637f48e38554625570aa9f` (Mac v1.0.0.0, in-game v98) **y** los primeros 16 bytes de cada función coinciden con los registrados. Si algo no cuadra, ese hook no se instala.

| Función Mac | Dirección | Protección |
| --- | --- | --- |
| `LobbyMsgRW_PackageInt` / `PackageUInt` | `0xf41d33` / `0xf41db0` | Límites de tipos de lobby, miembros, votos y settings; **rechazo de `datamask` = 0 (3.09)** |
| `LobbyMsgRW_PackageUChar` | `0xf41fc0` | Normaliza `nattype` a ≤ 4 |
| `LobbyMsgRW_PrintMessage` | `0xf417ae` | Desactivado, como en T7Patch |
| `LobbyMsgRW_ReadDebugSection` | `0xf41836` | Límite de anidamiento (16) contra mensajes que desbordan la pila |
| `LobbyTypes_GetMsgTypeName` | `0x1311e7c` | Índice fuera de `-1..0x20` → `"invalid"` (la tabla de Mac tiene 33 nombres) |
| `CL_ConnectionlessCMD` | `0xe19402` | Solo deja pasar los comandos de la lista de T7Patch (más voz/relay de Mac) |
| `CMD_MenuResponse_f` | `0xf451d1` | Política de 3.09: expulsa a no anfitriones con `killserverpc`/`restart_level_zm`, o `endgame`/`endround` fuera de campaña |
| `CL_GetConfigString` | `0xe2e90a` | Bloquea `mspreload` (3514/3627) fuera de campaña |
| `CL_SwitchState_Idle_Update` | `0x131489c` | Precarga en estado ocioso solo en campaña (entrada "idle" de la tabla `0x1b78e98`) |
| `SEH_ParseNextDirective` + `…Short` | `0xf47819` / `0x12c8e15` | Saneado de `^B…^` y `[{…}]`. Son los analizadores que comparten `SEH_ReplaceDirectiveInStringWithBinding` (`0xf47799`) y los otros dos llamadores de Mac |
| `UI_DoModelStringReplacement` | `0xd50fdc` | `$(` sin cerrar → `..` |
| `UI_Model_ResolvePath` | `0x1036426` | Claves de ruta ≥ 64 caracteres rechazadas. Es el recorredor común de `GetModelFromPath`/`CreateModelFromPath`, que copiaba cada clave a un buffer de pila de 64 bytes sin límite |
| `UI_Model_AllocateNode` | `0x1036158` | Devuelve 0 en lugar de expulsar al menú con `EXE_LUI_OUT_OF_MEMORY` cuando no quedan nodos |
| `LiveInvites_AcceptInvite` / `JoinMessageAction` / `SendJoinInfo` | `0x134b1b9` / `0x134bbf3` / `0x134bdb2` | Con "solo amigos" activo, ignora invitaciones y peticiones de unión de quien no es tu amigo |
| `LivePresence_Serialize` | `0x15312a1` | Al desempaquetar, recorta a 18 el número de jugadores (bits 2-6 del byte de control) |
| `UI_BrowserOpen` (Lua `OpenURL`) | `0xe0e2da` | Desactivado, como en T7Patch: impide que un script abra URLs o cierre el juego |
| `Mods_SubscribeUGC` | `0xe4971e` | Auto-suscripción a mods del Workshop solo con `BO3MACFIX_AUTOINSTALLMODS=1` (3.09) |
| `LiveSteam_InitServer` | `0xf6956d` | No se registra un servidor de juego de Steam (no se publica tu IP), como en T7Patch |
| `Scr_GscObjLink` | `0x149293c` | Arreglo de daño de Demon Within (Realista). Se aplica solo si el bytecode coincide byte a byte (GSC 0x1C) |

Funciones auxiliares verificadas igual: `SV_Cmd_ArgvBuffer`, `MSG_BeginReading`, `MSG_ReadLong`, `MSG_ReadStringLine`, `Cbuf_AddText`.

### Correcciones a la capa de Codex
- **Filtro de paquetes sin conexión:** la versión anterior leía `Cmd_Argv` al entrar en la función. En Mac la propia función es la que tokeniza el paquete, así que habría filtrado con datos viejos. Ahora el comando se lee de una copia privada del mensaje con los lectores `MSG_*` del juego.
- **`PrintDebugMessage`:** en Mac no existe esa función por separado. La que lee las secciones de depuración forma parte de la lectura normal de mensajes, así que anularla habría roto el lobby. Se sustituyó por un límite de recursión.
- **`BG_Cache_*`:** ya no se intentan enganchar. En T7Patch estos hooks están comentados (desactivados).
- **`CMD_MenuResponseCached_f`:** no existe en esta versión de Mac. Se eliminó.

### Otros cambios de T7Patch 3.09 portados
- `Mods_SubscribeUGC`: la instalación automática de mods del Workshop pasa a ser opcional (`BO3MACFIX_AUTOINSTALLMODS=1`). El hook solo se activa si la función está en el mapa, y por ahora no lo está.

### No portado
- **Desbloqueos de inventario/GobbleGums/DLC y spoof de propiedad** (`SPOOF_UNLOCK`, `SPOOF_OWNERSHIP`): desbloquean contenido de pago que no se ha comprado.

### Ranked spoofing (opcional, desactivado por defecto)
Es el `SPOOF_RANKED` de T7Patch 3.09 (Scropts-QOL). Con `BO3MACFIX_SPOOF_RANKED=1`, 13 llamadas a `Mods_IsModsLoaded` (`0xe4962f`) responden "sin mods", así que la XP y las estadísticas siguen contando con un mod cargado. Cada `call` se comprueba antes de cambiarlo por `xor eax,eax`:
- Modo de sesión: `0x127afde`, `0x127aff8`, `0x127b043`, `0x127b08c`, `0x127b0c8`
- Script: `AddWeaponStat` `0x13e7228`, `AddSpecialistStat` `0x13e784b`, `DoubleXPTimerFired` `0x13ea3d1`, `ReportLootReward` `0x13ea577`, `ReportLootConsume` `0x13ea645` y `0x13ea868`, `giveachievement` `0x118c99c`
- `LiveStats` (primera ejecución): `0x1082535`

No se tocan las llamadas que cargan contenido de mods (fastfiles, sonido, rutas, scripts). No se replica `Mods_IsModsLoaded_1`: en Mac lo más parecido es el indicador de mapa del Workshop, y forzarlo rompería los mapas personalizados. **Riesgo:** Activision puede marcar estadísticas conseguidas con mods, y la cuenta podría ser baneada.
- **`qmemcpy`:** en Mac no existe como función, porque el compilador mete `memcpy` en línea.
- **`ExecLuaCMD`:** el BO3MacFix original ya neutraliza `Lua_CmdParseArgs`, que es el origen del cierre.
- **`flsomeWeirdCharacterIndex` y `Live_UserGetName`:** no hay una equivalencia segura. BO3MacFix ya gestiona el nombre del jugador por su cuenta.
- **Recuperación de nombres en errores Lua (`LuaStateManager_Error`, HKS):** solo afecta a los mensajes de error y necesita bases `.luacallstackdb` que no vienen incluidas.

## Comprobar que funciona
Después de abrir el juego, mira `BO3MacFix/t7-status.log`. Deberías ver líneas `active` para las funciones de la tabla y un resumen `T7 hooks: 23 installed, 0 unresolved.`

## Volver a la versión anterior
En esta carpeta están `BO3MacFix.dylib.beta5-backup` (versión anterior, sin ranked spoof), `BO3MacFix.dylib.beta4-backup`, `BO3MacFix.dylib.t7compat2-backup` (versión de Codex), `LEEME.t7compat2-backup.md` y `SHA256SUMS.t7compat2-backup`. Para volver atrás, copia el backup sobre `BO3MacFix.dylib`.

## Variables útiles (opciones de lanzamiento de Steam)
| Variable | Efecto |
| --- | --- |
| `BO3MACFIX_DISABLE_OPTIONAL_HOOKS=1` | Desactiva todos los hooks T7 internos |
| `BO3MACFIX_DISABLE_STEAM_FILTERS=1` | Desactiva los filtros de Steam (P2P, chat, lobby solo para amigos) |
| `BO3MACFIX_SPOOF_RANKED=1` | Ranked spoofing (ver arriba). Bajo tu responsabilidad |
| `BO3MACFIX_AUTOINSTALLMODS=1` | Permite que el juego suscriba mods del Workshop automáticamente |
| `BO3MACFIX_SYMBOL_MAP=/ruta` | Mapa adicional de funciones (mismo formato y verificación) |

## Pruebas realizadas
`make test`: las 8 suites anteriores más `dobby_prologue_test`, que comprueba que Dobby reubica bien los inicios de función con `jcc` corto, `lea` relativo a RIP y un `call` rel32 partido por el borde del parche, que son los que tienen las funciones del mapa. **Todavía no se ha probado dentro de una partida.**
