# BO3MacFix 1.2-beta3-t7compat.2 — build experimental ampliado

Adaptación para probar en el juego. Incluye código de T7Patch v3.07, pero no afirma equivalencia total ni funcionamiento comprobado dentro de Black Ops 3.

Fuentes locales: BO3MacFix `985f0c2`; T7Patch-src `46b268a6cda05ac5ee2f8ff88c6db9f6d549df71`.

## Instalar

El archivo `BO3MacFix-1.2-beta3-t7compat.2-binaries.tar.gz` contiene la dylib, el launcher y el instalador, todos x86_64. Esta versión se compila para **macOS 11 o posterior**, acorde con la libc++ del Xcode disponible. En Apple Silicon, el juego y estos binarios usan Rosetta.

1. Cierra el juego y guarda una copia de tu carpeta BO3MacFix anterior.
2. Combina los tres binarios con una distribución completa de BO3MacFix, conservando sus assets.
3. Coloca la carpeta `BO3MacFix` junto a `CoDBlkOps3.app` y ejecuta `Install_BO3MacFix` para instalar/actualizar el launcher.
4. Después de arrancar, consulta `BO3MacFix/t7-status.log`: `active` indica que se instaló el hook; `missing symbol` significa que no pudo activarse. El registro se añade al archivo en cada inicio.

El repositorio no incluye `assets/`, `cod2map` ni los fastfiles de reemplazo. El paquete es una actualización de binarios, **no una distribución completa independiente**. No se ha instalado nada en el juego durante este trabajo. El componente Windows `c2mpatch` no ha cambiado ni se ha recompilado.

## Integraciones activadas a través de los hooks Mac existentes

- Contraseña de red: aceptación del checksum anterior durante 1500 ms, reloj monotónico y estado protegido por mutex. Coincidencia con la escritura del prefijo de T7Patch, incluidas contraseñas cuyo primer byte de prefijo es cero.
- Rechazo de mensajes instantáneos nulos/truncados y conservación del filtro de comandos remotos `0x65`/`0x6d`.
- Steam P2P: filtro de tipos `0x65`, `0x66`, `0x6d`; solicitudes `0x68` sujetas a «solo amigos», como en T7Patch.
- Chat de lobby: filtrado de caracteres/directivas peligrosas, límites por bytes recibidos y rechazo de emisores fuera del lobby Steam. Esta pertenencia se comprueba mediante Steam, no mediante las estructuras privadas de sesión de BO3 que usa Windows.
- Creación de lobbies Steam de tipo friends-only, igual que T7Patch. No equivale a hacer privado todo el protocolo de red del juego.
- Caché de progreso de descargas de cinco minutos, conservando el retorno booleano de la API Mac; caché de amigos de 30 segundos con límite y consulta adicional para listas grandes.
- Disponibilidad de campaña/multijugador/zombies condicionada a los archivos `zone/cp_common.xpak`, `mp_common.xpak`, `zm_common.xpak`, tomando la raíz de instalación desde Steam. Conserva la comprobación de propiedad. La disposición real de esos archivos en tu instalación debe probarse.
- Menú nativo funcional para cambiar contraseña y activar/desactivar «solo amigos»; Cancelar no modifica el nombre.

También se mantienen las correcciones de `t7compat.1`: cadenas terminadas, hexadecimal sin solapamiento, cierre de archivos, permisos de Physics, comprobación de lecturas y argumentos/código de salida del launcher.

## Hooks que se activan al encontrar su función Mac

El resolvedor lee los símbolos Mach-O del ejecutable principal, incluidos nombres C++ desmanglados. Los nombres ambiguos no se utilizan. **Encontrar un nombre no demuestra que su ABI coincida**: estos adaptadores siguen siendo experimentales y requieren tu prueba dentro del juego. Un ejecutable sin símbolos necesitará un mapa de direcciones Mac; no se han inventado offsets ni trasladado RVAs de Windows.

| Área | Adaptación compilada |
| --- | --- |
| Serialización lobby | Tipos de lobby 0–2; límites de miembros/clientes (18), votos (216), settings (0xc000); normalización NAT a 4. |
| Cadenas/UI | Limpieza de directivas `^B`, `[{…}]`, `$(…)`; límites de segmentos de rutas de modelos a 63 caracteres; protección ante tamaños negativos en qmemcpy. |
| Índices de cachés UI | Menús, eventos, localización y datos de menús con los límites presentes en T7Patch. Los hooks de estos índices estaban comentados en la instalación de hooks Windows; aquí se intentan resolver. |
| Configstrings | Filtra `mspreload` en índices 3514/3627 fuera de campaña; conserva campaña. Devuelve cadena vacía en lugar de escribir en la tabla global del juego. |
| Conexión | Lista de comandos permitidos y excepción `requeststats` para campaña, usando `Cmd_Argv` en vez del TLS de Windows. |
| Invitaciones | Filtro de amigos en aceptar invitación, acción de unión y envío de información de unión. |
| Menús remotos | Bloquea `badspawn` y comandos de finalizar/reiniciar sin autoridad. Conserva el manejador original y sus verificaciones; no ejecuta el autobaneo de Windows. Presupone que el número de entidad ocupa el primer int32, comprobando antes que sea legible. |
| Fallos/rendimiento | Desactiva impresiones de lobby y ExecLuaCMD problemáticos, apertura del navegador y suscripción UGC remota; conserva la actualización idle de campaña; omite LiveSteam_InitServer como T7Patch. |
| Demon Within Realistic | Adaptación del parche de daño, tras `Scr_GscObjLink`; exige cabecera, versión 0x1b/0x1c, checksum, hashes de función/namespace, límites y bytes originales exactos. No modifica scripts que no coincidan. |
| Lua | Recuperación de nombres/líneas en textos de error con bases `.luacallstackdb` y `t7patch.stockluadb`, descartando claves ambiguas. |

## Adaptaciones profundas de formato: activación explícita

Estas funciones también están compiladas. Como utilizan estructuras internas tomadas de T7Patch, se habilitan aparte; **sus comprobaciones de memoria no demuestran compatibilidad del formato Mac**.

- `BO3MACFIX_EXPERIMENTAL_LOBBY_LAYOUT=1`: inspectores de join party, lobby state, lobby game state, heartbeat e info response; rechazo de tipos bloqueados por T7Patch. Resuelve primero todos los serializadores necesarios. Inspecciona una copia privada del mensaje/payload y buffers independientes, con comprobaciones de contadores, overflow y tamaño. Presupone el formato de cabecera de lobby de 0x44 bytes; heartbeat e info response también requieren sus funciones por símbolo.
- `BO3MACFIX_EXPERIMENTAL_HKS_LAYOUT=1`: recuperación de nombres de funciones desde prototipos HKS y reemplazo del objeto de error de la pila Lua. Requiere `hksi_lua_getinfo`, `HksObjectToString`, `lua_pushlstring` y el hook de errores correspondiente. Presupone los offsets de estructuras HKS usados por T7Patch; comprueba memoria legible/escribible y contempla que el push reubique la pila.

Se pueden pasar desde las opciones de lanzamiento de Steam, por ejemplo:

```sh
env BO3MACFIX_EXPERIMENTAL_LOBBY_LAYOUT=1 BO3MACFIX_EXPERIMENTAL_HKS_LAYOUT=1 %command%
```

Activar esas variables no resuelve símbolos ausentes. El registro indica las dependencias que falten. Para Lua, coloca `t7patch.stockluadb` en `BO3MacFix/` o en la raíz del juego. `BO3MACFIX_LUA_MOD=nombre` selecciona explícitamente `mods/nombre` para buscar bases de símbolos, evitando asumir el global Windows del mod activo. Las bases no están incluidas en estos repositorios.

## Opciones para aislar problemas durante las pruebas

| Variable | Efecto |
| --- | --- |
| `BO3MACFIX_DISABLE_OPTIONAL_HOOKS=1` | Omite todo el grupo de hooks internos nuevos por símbolo, incluidos inspectores profundos y Lua. |
| `BO3MACFIX_DISABLE_STEAM_FILTERS=1` | Omite los nuevos hooks de P2P, chat y creación de lobby. |
| `BO3MACFIX_DISABLE_CONTENT_CHECK=1` | Conserva solo la respuesta Steam para disponibilidad de modos. |
| `BO3MACFIX_NETWORKPASSWORD`, `BO3MACFIX_FRIENDSONLY`, `BO3MACFIX_PLAYERNAME` | Opciones originales; también existen `--password`, `--friendsonly`, `--playername` en el launcher. |
| `BO3MACFIX_SYMBOL_MAP=/ruta/mac-symbols.txt` | Mapa de funciones preparado mediante análisis del ejecutable Mac, descrito abajo. |

La contraseña conserva el checksum/protocolo de T7Patch; no es cifrado criptográfico. El cambio de contraseña/amigos desde el menú dura la sesión y no escribe una configuración persistente.

## Mapa de funciones para un ejecutable sin símbolos

La primera línea debe ser `sha1 HASH_DEL_EJECUTABLE` (40 caracteres hexadecimales minúsculos). Cada línea siguiente usa:

```text
NombreFuncion RVA_HEXADECIMAL BYTES_ORIGINALES_HEXADECIMALES
```

Las RVAs se miden desde `__TEXT`. Los bytes deben ser entre 8 y 32 bytes, sin espacios, en minúsculas. El mapa solo se usa si coincide el SHA-1 de todo el ejecutable y el prólogo coincide en memoria dentro de un segmento ejecutable. Un mapa erróneo se rechaza. Es un mecanismo para direcciones Mac investigadas; no se distribuye un mapa con direcciones ficticias. La lista de nombres solicitados figura en `SYMBOLS-NEEDED.txt`.

## Lo que no equivale a un port completo

- Arxan, VEH/SEH, IAT, APIs y prioridad de proceso de Windows no se trasladan a macOS.
- No se aplican los parches binarios Windows de extensión de signo, llamadas Com_Error, tokenización, fallos de personaje ni cambios de RVAs de febrero/septiembre de 2026: requieren instrucciones y ubicaciones Mac.
- Faltan las adaptaciones específicas del layout de presencia, el contador global de nodos UI, datamask y callbacks de Demonware. Los filtros incluidos no cubren todos los caminos de recepción de T7Patch.
- Los desbloqueos de inventario/licencias y opciones CWL estaban desactivados mediante `SPOOF_UNLOCK_ALL=false` en la fuente y no se han añadido. Se conservan las comprobaciones Steam de propiedad.
- Se mantienen los offsets base Mac v98 y los valores de protocolo de BO3MacFix. No se ha validado crossplay con Windows 2026 ni actualizado el contenido de los fastfiles ausentes.
- No hay base para afirmar que todas las funciones por símbolo vayan a encontrarse en tu ejecutable. El registro de tu prueba es necesario para saber qué queda activo.

## Validación y build

```sh
make test binaries
bash scripts/package.sh --binaries-only
```

Ocho suites con AddressSanitizer y UndefinedBehaviorSanitizer: red, Steam/cachés/contenido, utilidades, P2P/chat/lobbies, protecciones/campaña (ambas versiones y 10.000 cabeceras aleatorias), inspectores profundos, símbolos Lua/pila HKS simulada y resolvedor Mach-O/mapas. El resolvedor también se prueba con un SHA-1 incorrecto. Son pruebas de lógica, estructuras simuladas y resolución en un ejecutable de prueba, no de ABI ni ejecución de hooks dentro de BO3.

Además: compilación x86_64, carga de dylib con RTLD_NOW fuera del juego, dependencias del binario y SHA-256/permisos del paquete. No se han probado partida, mapas, campaña ni crossplay.

Referencias API consultadas: [ISteamNetworking de Valve](https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/public/steam/isteamnetworking.h), [ISteamMatchmaking de Valve](https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/public/steam/isteammatchmaking.h), [documentación de chat](https://partner.steamgames.com/doc/api/ISteamMatchmaking#GetLobbyChatEntry). El código adaptado procede de los archivos locales de T7Patch indicados arriba.
