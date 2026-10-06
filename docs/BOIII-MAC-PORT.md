# Conectar el BO3 de Mac a servidores BOIII/Ezz: análisis (fase 1)

Fuente analizada: `Ezz-lol/boiii-free` (solo lectura). La parte de saltarse la propiedad de Steam queda **fuera** del port.

## Protocolo (lado cliente de BOIII)
1. `\xFF\xFF\xFF\xFFgetInfo\n<challenge>` → `infoResponse` (infostring). El cliente exige `protocol=8`, `sub_protocol` 1 (o 0), `gamename=T7`, `mapname`, `gametype`, la contraseña de red si la hay y las dependencias del Workshop.
2. Unión: `CL_ConnectFromLobby(controller, XSESSION_INFO*, netadr*, publicSlots, 0, map, gametype, usermap)` con el modo de sesión ONLINE y `MATCHMAKING_PLAYLIST`.
3. `CL_CheckForResend`, en estado CHALLENGING: `getchallenge`. El servidor responde `getChallengeResponse <32 bytes>`; el cliente guarda los bytes y pasa a CONNECTING.
4. En CONNECTING: el userinfo nativo (`name`, `protocol 18532`, `netfieldchk`, `sessionmode`, `challenge`, `invited`, `xuid`, `qport`, `migrating`, más `password` y `clanAbbrev`) se envuelve así:
   `write_string(pubkey ECC libtomcrypt PK_PUBLIC)`, `write_string(firma ECC del challenge)`, `profile_info`, `write_string("connect \"<userinfo>\"")`.
   Se manda en fragmentos de 0x400 bytes: `connect ` + `u64 id` + `u32 total` + `u32 índice` + datos.
5. El servidor exige que el `xuid` sea igual a `key.get_hash()` y un `name` válido. Después llama a `SV_DirectConnect` y a partir de ahí todo es el protocolo nativo de BO3.
6. Paquetes extra del servidor: `playerXuid`, `error`, `getChallengeResponse`. Hay que permitirlos en el filtro connectionless de T7.

## Servidores reales (consulta al maestro `master.ezz.lol:20810`, `getservers\nT7 8 full empty`)
144 servidores, 43 de Zombies. Ejemplo: OX `5.161.179.25:6002`, `zm_theater`, `zclassic`, sin Workshop, `protocol 8`, `sub_protocol 1`.

## Funciones de Mac ya identificadas
| Función | RVA |
| --- | --- |
| `CL_CheckForResend` | `0xe17e98` (manda `getchallenge` y construye `connect "…"`) |
| `NET_OutOfBandPrint` | `0x14021e9` |
| `NET_OutOfBandData` | `0x14022b6` (envío del `connect` en `0xe185b9`) |
| `CL_ConnectionlessCMD` | `0xe19402` (ya enganchada) |
| Comando `connect` nativo | `0xda1538` → `LobbyJoinSource_IP` `0xda1495`. **No sirve**: busca un lobby de Steam |
| Estado de conexión por cliente | `0x115324a8 + lc*0x1078` (5 = CHALLENGING, 6 = CONNECTING) |
| `CL_Frame` | `0xe1a54a` |
| Candidato a `CL_ConnectFromLobby` | `0xe17b4d` (único que pone el estado 5; llamado desde la unión al lobby `0xd6f75d`). **Sin confirmar** |
| Socket IP | se abre en `127.0.0.1:27017`, dvar `net_ip` (registrada en `0xd6de59`). Hay que abrirlo en `0.0.0.0` |

## Ya resuelto por BO3MacFix
- `protocol` 18530 → 18532 (igual que Windows).
- `netfieldchk`: debería coincidir, porque el crossplay con hosts de Windows funciona.

## Pendiente
- Confirmar o encontrar `CL_ConnectFromLobby` y la estructura `XSESSION_INFO` de Mac.
- Identidad: el servidor usa el XUID derivado de la clave ECC. Hay que sustituir el XUID local (`LiveUser_GetXuid`, `LiveUser_UserGetXuid`) **solo** mientras se está en un servidor BOIII.
- Implementar ECC compatible con libtomcrypt (clave, firma, `get_hash`) y el formato de `profile_info`.
- Saltar las comprobaciones de Steam ID del cliente que BOIII parchea: `XUID_Valid`, `LiveSteamAuth_Pump`, lobby…
- Probar si el contenido de la versión de Mac (2018) carga los mapas y scripts del servidor (versión final de Windows). **Esto solo se puede saber probando.**

## Fase 2 (beta12): cliente experimental
`BO3MACFIX_BOIII=1` + `boiii_connect ip:puerto` (o `BO3MACFIX_BOIII_CONNECT`). Implementado en `source/boiii_client.c`: socket en 0.0.0.0 (interposición de `bind`), `CL_LaunchClient(map, gametype)`, redirección en `CL_CheckForResend`, challenge en el filtro connectionless, `connect` firmado (libtomcrypt develop, P-521, X9.62/SHA-512) y fragmentado en `NET_OutOfBandData`. Clave persistente en `BO3MacFix/boiii_key.dat`. Autenticación verificada contra un servidor real desde fuera del juego; el flujo dentro del juego está pendiente de probar.
