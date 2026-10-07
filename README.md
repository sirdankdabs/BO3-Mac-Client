# BO3 Mac Client

Play **Call of Duty: Black Ops III for Mac** (the Steam version) on the community dedicated servers listed by the BOIII master server — the same servers Windows players join.

![platform](https://img.shields.io/badge/macOS-14%2B-black) ![game](https://img.shields.io/badge/requires-Steam%20copy%20of%20BO3-orange)

## What it does

- **Server browser** — every server on the community master (`master.ezz.lol`), with map, mode, players, bots and ping. Search, filter (Zombies / Multiplayer, hide full/empty/modded) and sort.
- **One-click join** — picks the server, starts the game through Steam with the client engine loaded, and connects as soon as you open the matching mode (Zombies or Multiplayer) in the game menu. If the game is already running from the app, it joins right away.
- **Player name** — choose the name other players see (up to 16 characters), or keep your Steam name.

## Requirements

- macOS 14 or later (Apple silicon or Intel).
- Call of Duty: Black Ops III for Mac, installed and owned on Steam. Steam must be running and signed in.

## Install

1. Download `BO3 Mac Client.app` and move it to `/Applications`.
2. The app is not notarized: the first time, right-click it → **Open** → **Open**.
3. Pick a server and press **Join**.

Logs, the player identity key and settings live in `~/Library/Application Support/BO3 Mac Client/`.

## Current limitations

- Servers that require Workshop mods or custom maps can't be joined yet (marked **Mod**).
- Password-protected servers are not supported yet (marked **Locked**).
- The in-game pause menu (Esc) may not open on the first map after joining; it works after the next map loads.
- Multiplayer servers are listed and joinable but have had far less testing than Zombies.

## How it works

The app is a small SwiftUI launcher. The real work happens in **the engine** (`engine/`), a dylib injected into the game process (the same way BO3MacFix works) that:

- speaks the community servers' connection protocol: a persistent P-521 identity key, an ECDSA (SHA-512) signature over the server challenge, and a fragmented `connect` message;
- routes the game's traffic to the server directly, because the Mac network layer refuses unknown addresses;
- finds the server id the Windows server never sends to the Mac client (only its high nibble is checked, so the client tries the 15 possible values while holding acknowledgements so the server doesn't resend the map);
- keeps the connection alive through the Mac build's long level loads (skips cycled-out reliable commands, skips the intro movie once the level is loaded);
- resolves chat names the server sends as client numbers.

The launcher and the engine talk through two files in the data folder: `join.request` (written by the app) and `status` (heartbeat written by the engine).

No code from other clients is included; the protocol was implemented from its public documentation and source. The Steam ownership check is **not** bypassed — you need your own copy of the game.

## Build

Requires Xcode command line tools.

```bash
./build.sh
```

The app is written to `dist/BO3 Mac Client.app`.

## Credits and license

- **BO3MacFix** by Emma / InvoxiPlayGames and Anthony / serious — the engine is a modified version of BO3MacFix (LGPL-2.1, see `engine/LICENSE.txt`). Modifications: T7Patch 3.09 fixes for the Mac build, community server client, launcher link and data directory.
- **T7Patch** authors — the security and gameplay fixes ported to the Mac build.
- [Dobby](https://github.com/jmpews/Dobby) (Apache-2.0), [LibTomCrypt / LibTomMath](https://github.com/libtom) (public domain / Unlicense).
- The community server project behind `master.ezz.lol` for the servers and master list.

Not affiliated with or endorsed by Activision, Treyarch, Aspyr or Valve. Call of Duty and Black Ops are trademarks of Activision Publishing, Inc.
