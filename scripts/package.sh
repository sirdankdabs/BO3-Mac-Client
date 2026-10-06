#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode="${1:-full}"
if [[ $# -gt 1 || ( "$mode" != full && "$mode" != --binaries-only ) ]]; then
    echo "Usage: $0 [--binaries-only]" >&2
    exit 2
fi
for binary in BO3MacFix.dylib installer/Install_BO3MacFix launcher/AppBundleExe; do
    [[ -f "$binary" ]] || { echo "Missing $binary; run make binaries first." >&2; exit 1; }
done
if [[ "$mode" == full && ( ! -d assets/cod2map || ! -d assets/zone ) ]]; then
    echo "Missing assets/cod2map or assets/zone. Use --binaries-only for an update package." >&2
    exit 1
fi
mkdir -p release
stage=$(mktemp -d "${TMPDIR:-/tmp}/bo3macfix-package.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir "$stage/BO3MacFix"
cp BO3MacFix.dylib installer/Install_BO3MacFix launcher/AppBundleExe "$stage/BO3MacFix/"
cp docs/T7PATCH-PORT.md "$stage/BO3MacFix/LEEME.md"
cp docs/SYMBOLS-NEEDED.txt "$stage/BO3MacFix/"
cp LICENSE.txt "$stage/BO3MacFix/"
version=$(sed -n 's/^#define MACFIX_VERSION "\(.*\)"/\1/p' source/versioninfo.h)
[[ "$version" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "Invalid version" >&2; exit 1; }
archive="BO3MacFix-${version}-binaries.tar.gz"
if [[ "$mode" == full ]]; then
    cp -R assets/. "$stage/BO3MacFix/"
    archive="BO3MacFix-${version}.tar.gz"
fi
chmod 0644 "$stage/BO3MacFix/BO3MacFix.dylib"
chmod 0755 "$stage/BO3MacFix/Install_BO3MacFix" "$stage/BO3MacFix/AppBundleExe"
(cd "$stage/BO3MacFix" && shasum -a 256 BO3MacFix.dylib AppBundleExe Install_BO3MacFix > SHA256SUMS)
COPYFILE_DISABLE=1 tar --exclude='.DS_Store' -czf "release/$archive" -C "$stage" BO3MacFix
printf 'Created release/%s\n' "$archive"
