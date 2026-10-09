# Notas técnicas: BO3 en Mac + servidores de la comunidad

Todo lo que se investigó y construyó, en orden. Sirve para retomar el trabajo
en el futuro sin tener que redescubrir nada.

## 1. El juego de Mac

- `CoDBlkOps3.app/Contents/MacOS/CoDBlkOps3_Exe`: el juego de Windows (Aspyr,
  build v98 de 2019) envuelto en una capa "Emu" (rutas `C:/Emu`). Es x86_64 y
  corre en Apple Silicon con **Rosetta 2**.
- Binario sin símbolos (59.804 funciones vía LC_FUNCTION_STARTS).
  SHA-1 `5e42371b6a16f2b11e637f48e38554625570aa9f`. Si Steam actualiza el
  juego, todas las direcciones de abajo hay que volver a buscarlas.
- `AppBundleExe` es el lanzador. BO3MacFix lo reemplaza por el suyo, que
  ejecuta `CoDBlkOps3_Exe` con `DYLD_INSERT_LIBRARIES=BO3MacFix.dylib` y
  `SteamAppId=311210`. La app BO3 Mac Client hace lo mismo con su motor.
- Las opciones de lanzamiento de Steam con `env … %command%` rompen el .app
  (OS Error 260). Por eso las opciones van en `bo3macfix.conf`.
- `config.ini` del jugador: `~/Library/Application Support/Call of Duty Black Ops III/players/`.

## 2. BO3MacFix + T7Patch 3.09

- Base: BO3MacFix de Emma / InvoxiPlayGames (LGPL-2.1), versión modificada
  por Codex, actualizada a T7Patch 3.09 (Scroptss).
- Hooks con Dobby. Cada dirección se verifica con el SHA-1 del ejecutable y
  los primeros 16 bytes de la función (`source/t7_mac_symbols.h`).
- Ranked spoofing opcional (`BO3MACFIX_SPOOF_RANKED=1`): 17 llamadas a
  Mods_IsModsLoaded (0xe4962f) neutralizadas y 4 llamadas de almacenamiento
  redirigidas, para que la XP cuente con mods. Los desbloqueos de pago
  NO se incluyeron.
- Audio (`audio_fix.c`): AudioQueue sigue al dispositivo de salida por
  defecto (AirPods) y usa bloques SDL de mínimo 1024 muestras.
- Stutter: limitador de líneas de log repetidas y stub de LiveSteam_ServerFrame.

## 3. Protocolo de los servidores de la comunidad (BOIII)

- Servidor maestro: `master.ezz.lol:20810` (también `m.ezz.lol`), protocolo
  estilo Quake 3: `getservers\nT7 8 full empty` → `getserversResponse` +
  (`\` + IP:4 + puerto:2)* + `\EOT`. Los servidores mandan `heartbeat T7`.
- `getInfo` → `infoResponse`: hostname, mapname, gametype, clients, bots,
  sv_maxclients, playmode (0 = ZM, 1 = MP, 2 = CP), protocol 8,
  sub_protocol 1, gamename T7, isPrivate, workshop_id, usermapId, modId…
- `getchallenge` → `getChallengeResponse ` + 32 bytes. **OX cambia el
  challenge en cada petición**: usar el último y una sola vez.
- `connect` firmado: write_string(clave pública ECC exportada),
  write_string(firma ECDSA X9.62 sobre SHA-512 del challenge),
  int32 versión + string (profile_info), write_string(`connect "<userinfo>"`).
  Se manda en fragmentos: `connect ` + u64 id + u32 total + u32 índice +
  ≤0x400 bytes.
- Identidad: clave P-521 de libtomcrypt (`ecc_make_key` 64). El xuid es
  igual a los primeros 8 bytes del SHA-1 de `ecc_ansi_x963_export`. El
  servidor lo exige. El VIP, el rango y las estadísticas van ligados a ese ID.
  - Formato de archivo: `ecc_export(PK_PRIVATE)`, el mismo que el
    `ezz-private.key` del cliente de Windows, así que se pueden intercambiar.
- Paquetes: [byte de socket (sock & 0xF) | (netid << 4)] + carga, sin
  checksum. Netchan: [0xe0][u64 destino][u64 origen][flags 0x40 = fiable]
  [canal (varía)][00][u32 secuencia netchan][fragmento]…
  - El primer fragmento del gamestate trae en el offset 29 la secuencia de
    mensaje del juego, que es la que confirma el cliente. No es la del
    netchan (offset 21).

## 4. Problemas del cliente de Mac y cómo se resolvieron

| Problema | Causa | Solución |
| --- | --- | --- |
| dwSendTo -7 | Demonware rechaza IPs desconocidas | sendto directo en el socket del juego + inyección en Sys_GetPacket |
| Socket en 127.0.0.1 | net_ip localhost | interponer `bind`: 0.0.0.0 y puertos 27017–27027 |
| Crash PMem_Free LensFlareManager | servidor local del menú vivo | Com_KillLocalServerForLaunch antes de CL_LaunchClient |
| "Bad signature" | OX rota el challenge | challenge de un solo uso + volver a pedirlo |
| Bucle de la intro | el servidor de Windows deja vacío el config string 3 (serverId); el cliente manda 0 | ver abajo |
| EXE_ERR_RELIABLE_CYCLED_OUT | carga del Mac > 60 s, más de 128 comandos acumulados | entrar al mundo solo con el nivel cargado + saltar comandos ciclados (CL_CGameNeedsServerCommand 0xe2de31) |
| Intro sonando de fondo / ESC | sin lobby, nunca llega el "start play" | llamar 0x12eee13(1): la pantalla de carga recibe `loading_startplay` |
| Chat "Unknown Soldier" | el servidor manda número de cliente + 1 en vez de Steam ID | hook LiveSteam_GetPlayerName (0x1435176) → nombre del cgame |
| VIP perdido | la app creó otra identidad | copiar la clave original; importar/respaldar en la app |
| Doble entrada (perks ×2) | serverId mal atribuido | candidatos de 800 ms + ignorar paquetes sueltos |
| EXE_ERR_BG_CACHE "Index Mismatch" | mods de servidor registran assets | `je` → `jmp` en 0x12a2453 (como el cliente de Windows) |
| Servidores con mod bloqueados | criterio demasiado estricto | solo bloquear mod real (modId ≠ "", "usermaps") o mapa no instalado |

### Búsqueda del serverId (lo más difícil)

- Servidor (Mac SV_ExecuteClientMessage, 0x105bd66): solo compara el
  **nibble alto** `(id ^ sv.serverId) & 0xf0`. `sv.serverId += 0x10` por
  cada mapa (nunca con nibble alto 0). El nibble bajo solo cambia con
  map_restart.
- Si el id no coincide y `messageAck > gamestateMessageNum`, el servidor
  reenvía el gamestate.
- El cliente prueba los 15 nibbles altos, cada uno 800 ms, con el
  messageAck (offset 30 del paquete) fijado a la secuencia del gamestate
  para que no haya reenvío.
- La entrada se detecta por la ráfaga de snapshots (8 paquetes no fiables
  en 400 ms) y se atribuye por tiempo.
- El valor se guarda por servidor en `boiii_serverids.txt`.
- Formato del paquete de datos del cliente: offset 29 = serverId,
  30 = messageAck, 34 = reliableAck.

## 5. Direcciones del ejecutable de Mac (RVA)

Estados de conexión: `0x115324a8 + lc*0x1078`
(4 challenge, 5 connect, 8 connected, 10 primed, 11 active).

- **Bloques de estado:**
  - `cl = *(0x11534598)`; `cl+0xb684` = serverId.
  - `clc = *(0x115345a0)`; `clc+0x4540` = secuencia de mensaje,
    `+0x4544` = secuencia de comandos del servidor, `+0x130/+0x134` =
    comandos fiables enviados/confirmados, `+0x2454d` = nivel cargado,
    `+0x24b68` = xuid del peer, `+0x2455c` = dirección del peer.
  - `cls`: `0x115345b0`.
- **cgame:** `*(0x145b9fd0) + lc*0x342a80`. Cliente i en `+i*0xed0`:
  válido en +0x2e7ba0, nombre en +0x2e7bac, clan en +0x2e7c20.
- **Funciones del cliente:**

  | Función | RVA |
  | --- | --- |
  | CL_CheckForResend | 0xe17e98 |
  | NET_OutOfBandData | 0x14022b6 |
  | NET_OutOfBandPrint | 0x14021e9 |
  | CL_LaunchClient | 0xd6f75d |
  | Sys_GetPacket | 0xd6dacf |
  | Sys_SendPacket | 0xd6dc30 |
  | CL_ParseServerId | 0xeca498 |
  | CL_ParseGamestate | 0xecaa64 |
  | CL_WritePacket | 0x149f471 |
  | CL_AddReliableCommand | 0x1109e58 |
  | CL_SetCGameTime (puerta PRIMED → ACTIVE) | 0xe2fbe8 |
  | Paso a ACTIVE | 0xe2f0be |
  | Com_ReturnToFrontend | 0xe7535e |
  | LiveUser_GetControllerFromXuid | 0x103a79e |
  | Com_IsLocalServerRunning | 0xe71737 |
  | Com_KillLocalServerForLaunch | 0xe71641 |
  | Envío del chat (`chat "%i %s"`) | 0x13bef1c |

- **Cinemáticas:** flags en 0x15999da0 (bit 0x100 = vídeo de carga que
  bloquea). Parar película: 0x12ee826.
- **Mods y Workshop** (funciones Lua del menú Mods):

  | Función | RVA |
  | --- | --- |
  | Mods_SubscribeUGC | 0xe4971e |
  | Mods_InstalledUGC | 0xe49763 |
  | Mods_InstallProgressUGC | 0xe4977c |
  | Mods_Lists_UpdateUsermaps | 0xe49263 |
  | Mods_SetMod (fs_game; "usermaps") | 0xe49c5a |
  | Mods_Load_Updating | 0xe4a0b9 |

  Mod actual en 0x30878dc. SteamUGC(): `*(0x1b90300)`, SubscribeItem en
  vtable+0x1a8. Los mapas de serie están en `Contents/Assets/zone/<mapa>.ff`.
- **Servidor (referencia):** serverId global 0x148ad360;
  SV_ExecuteClientMessage 0x105bd66; `client->serverId` +0xbb464.

## 6. BO3 Mac Client (la app)

- SwiftUI (`app/`) + motor C (`engine/`, fork de BO3MacFix).
- La app y el motor se comunican con archivos en
  `~/Library/Application Support/BO3 Mac Client/`:
  - `join.request` (server=, mode=, name=), lo escribe la app;
  - `status` (state, message, pid, time), heartbeat del motor;
  - `engine.log`, `boiii_key.dat`, `boiii_serverids.txt`.
- Compilar: `./build.sh` → `dist/BO3 Mac Client.app` (universal, firma ad-hoc).

## 7. Pendiente / ideas

- Mods que también necesita el cliente (modId real): Mods_SetMod(id) con
  recarga del sistema de archivos.
- Mapas personalizados: implementado (suscripción + "usermaps"), por
  probar más.
- VIP en OX: lo asigna el servidor (IW4MAdmin / script), ligado al ID del
  jugador.
- Input que se congela al cambiar de ventana.
- iPhone/iPad: no ejecuta el binario de Mac. La vía real es Madeira
  (FEX + Wine + DXMT, sideload con JIT) con la versión de Windows del
  juego, o streaming (Steam Link).

## 8. Herramientas de análisis

- `3 - Extras/herramientas/re/`: index.py (indexa funciones, llamadas y referencias
  RIP del binario con LIEF y Capstone) y q.py/around.py (consultas: s, xr,
  xfuncs, dis, callers, callees, region, find_imm, strs_of).
- Regenerar el índice:
  `python3 -m venv venv && venv/bin/pip install lief capstone && venv/bin/python index.py idx.pkl`
  (en index.py, ajusta la ruta del ejecutable si hace falta).
- `probe.py`: consultas UDP a servidores y al maestro.
