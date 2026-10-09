#!/bin/bash
# Installs BO3 Mac Client and BO3MacFix from this folder.
# Optional: put your own identity key (identity-key-*.dat) and game settings
# (game-settings/) in "3 - Personal (keep private)" to restore them too.
# That folder is ignored by git: never commit or share it.
cd "$(dirname "$0")" || exit 1
APPS="${APPS:-/Applications}"
DMG="1 - Install/BO3 Mac Client.dmg"
PERSONAL="3 - Personal (keep private)"
DATA="$HOME/Library/Application Support/BO3 Mac Client"
GAME_PLAYERS="$HOME/Library/Application Support/Call of Duty Black Ops III/players"

echo "== BO3 Mac Client installer =="
[ -f "$DMG" ] || { echo "Missing $DMG"; exit 1; }

echo "1/4 Installing the app into $APPS ..."
MOUNT="$(mktemp -d)"
hdiutil attach -nobrowse -readonly -quiet -mountpoint "$MOUNT" "$DMG" || { echo "Could not open the disk image"; exit 1; }
osascript -e 'quit app "BO3 Mac Client"' >/dev/null 2>&1
rm -rf "$APPS/BO3 Mac Client.app"
cp -R "$MOUNT/BO3 Mac Client.app" "$APPS/" && echo "   Installed."
hdiutil detach -quiet "$MOUNT"; rmdir "$MOUNT" 2>/dev/null
# Downloaded or copied from another Mac the app may be quarantined (it isn't notarized).
xattr -dr com.apple.quarantine "$APPS/BO3 Mac Client.app" 2>/dev/null

echo "2/4 Identity key (rank / VIP on community servers) ..."
KEY="$(ls "$PERSONAL"/identity-key-*.dat 2>/dev/null | head -1)"
if [ -n "$KEY" ]; then
  mkdir -p "$DATA"
  if [ -f "$DATA/boiii_key.dat" ] && ! cmp -s "$KEY" "$DATA/boiii_key.dat"; then
    mv "$DATA/boiii_key.dat" "$DATA/boiii_key.backup-$(date +%s).dat"
    echo "   (the previous key on this Mac was kept as a backup)"
  fi
  cp "$KEY" "$DATA/boiii_key.dat" && echo "   Restored $(basename "$KEY")."
else
  echo "   No key in \"$PERSONAL\"; a new identity will be created on first join."
fi

echo "3/4 BO3MacFix (patches for playing from Steam: T7Patch, audio, XP, server list)"
GAME=""
STEAM="$HOME/Library/Application Support/Steam"
while IFS= read -r lib; do
  candidate="$lib/steamapps/common/Call of Duty Black Ops III"
  if [ -x "$candidate/CoDBlkOps3.app/Contents/MacOS/CoDBlkOps3_Exe" ]; then GAME="$candidate"; break; fi
done < <({ echo "$STEAM"; grep '"path"' "$STEAM/steamapps/libraryfolders.vdf" 2>/dev/null | awk -F'"' '{print $4}'; })
if [ -z "$GAME" ]; then
  echo "   Black Ops III not found in your Steam libraries; install it and run this again."
elif [ -d "1 - Install/BO3MacFix" ]; then
  read -r -p "   Install BO3MacFix into the game? [Y/n] " answer
  if [[ ! "$answer" =~ ^[Nn] ]]; then
    mkdir -p "$GAME/BO3MacFix"
    cp -p "1 - Install/BO3MacFix/"* "$GAME/BO3MacFix/"
    [ -n "$KEY" ] && cp "$KEY" "$GAME/BO3MacFix/boiii_key.dat"
    MACOS="$GAME/CoDBlkOps3.app/Contents/MacOS"
    # Same steps as Install_BO3MacFix: keep Steam's launcher, put ours in place.
    if [ ! -f "$MACOS/AppBundleExe_backup" ]; then mv "$MACOS/AppBundleExe" "$MACOS/AppBundleExe_backup"; fi
    cp -p "$GAME/BO3MacFix/AppBundleExe" "$MACOS/AppBundleExe"
    chmod 755 "$MACOS/AppBundleExe"; chmod 644 "$GAME/BO3MacFix/BO3MacFix.dylib"
    xattr -d com.apple.quarantine "$MACOS/AppBundleExe" "$GAME/BO3MacFix/BO3MacFix.dylib" 2>/dev/null
    echo "   Installed in $GAME/BO3MacFix (launching BO3 from Steam now uses it)."
  else
    echo "   Skipped."
  fi
fi

echo "4/4 Game settings (graphics, key bindings)"
if [ -d "$PERSONAL/game-settings" ]; then
  read -r -p "   Restore your Black Ops III settings? Launch the game once first. [y/N] " answer
  if [[ "$answer" =~ ^[YySs] ]]; then
    mkdir -p "$GAME_PLAYERS"
    for f in "$PERSONAL/game-settings/config.ini" "$PERSONAL/game-settings/"bindings_*.cfg; do
      [ -f "$f" ] || continue
      name="$(basename "$f")"
      [ -f "$GAME_PLAYERS/$name" ] && cp "$GAME_PLAYERS/$name" "$GAME_PLAYERS/$name.before-restore"
      cp "$f" "$GAME_PLAYERS/$name"
    done
    echo "   Restored."
  else
    echo "   Skipped."
  fi
fi

echo
echo "Done. Open BO3 Mac Client from Applications (Steam and Black Ops III must be installed)."
[ -z "$NO_OPEN" ] && open "$APPS/BO3 Mac Client.app"
