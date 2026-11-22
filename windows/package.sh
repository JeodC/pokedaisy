#!/usr/bin/env bash
# Packs PokeDaisy for Windows as a release ships it: PokeDaisy-<version>-windows-x86_64.zip with PokeDaisy.exe, its three
# cores, SDL2.dll, the MinGW runtime DLLs they name, LICENSE / NOTICE, licenses/ and SOURCE.txt. Run it on Fedora with
# the mingw64 packages (.github/workflows/windows.yml's container, or docs/RELEASING.md's local recipe), after the four
# builds windows/CMakeLists.txt lists:
#   windows/package.sh <build dir> <out dir> [version] [commit]
# <build dir> holds gbc/ (libretro), gba/ (libretro/armgba), ds/ (windows/ds-core) and host/ (windows). The version
# defaults to app/build.gradle.kts's versionName, the commit to HEAD. Every DLL the exe and the cores import must be
# Windows' own or in the zip, else it stops: a missing one shows only on a player's PC.
set -euo pipefail

build=$1
out=$2
root=$(cd "$(dirname "$0")/.." && pwd)
version=${3:-$(sed -n 's/^ *versionName = "\(.*\)"/\1/p' "$root/app/build.gradle.kts")}
commit=${4:-$(git -C "$root" rev-parse HEAD)}
name="PokeDaisy-${version}-windows-x86_64"
mingw=/usr/x86_64-w64-mingw32/sys-root/mingw/bin
licenses=/usr/share/licenses
objdump=x86_64-w64-mingw32-objdump

stage=$(mktemp -d)
dest="$stage/$name"
mkdir -p "$dest/licenses" "$out"

cp "$build/host/PokeDaisy.exe" "$build/host/SDL2.dll" \
   "$build/gba/pokedaisy_gba_libretro.dll" "$build/gbc/pokedaisy_gbc_libretro.dll" "$build/ds/pokedaisy_ds_libretro.dll" \
   "$dest/"

# The MinGW runtime: whatever the exe and the cores import from it (libgcc's unwinder, winpthreads, zlib so far).
# (objdump's output read whole first: `grep -q` stopping early would SIGPIPE it, which pipefail counts as a failure.)
imports=$(for f in "$dest"/*.exe "$dest"/*.dll; do "$objdump" -p "$f" | sed -n 's/.*DLL Name: //p'; done | tr 'A-Z' 'a-z' | sort -u)
for dll in libgcc_s_seh-1.dll libwinpthread-1.dll zlib1.dll libstdc++-6.dll; do
  if grep -qx "$dll" <<< "$imports"; then cp "$mingw/$dll" "$dest/"; fi
done

cp "$root/LICENSE" "$root/NOTICE" "$dest/"
l="$dest/licenses"
cp "$root/third_party/mgba/LICENSE" "$l/mGBA-MPL-2.0.txt"
cp "$root/third_party/mgba/src/third-party/blip_buf/license.txt" "$l/blip_buf-LGPL-2.1.txt"
cp "$root/third_party/mgba/src/third-party/inih/LICENSE.txt" "$l/inih-BSD.txt"
cp "$root/third_party/armgba/LICENSES/MPL-2.0.txt" "$l/armGBA-MPL-2.0.txt"
cp "$root/third_party/melonds/LICENSE" "$l/melonDS-GPL-3.0.txt"
cp "$root/third_party/zlib/LICENSE" "$l/zlib.txt"
cp "$licenses/mingw64-SDL2/LICENSE.txt" "$l/SDL2-zlib.txt"
cp "$licenses/mingw64-winpthreads/COPYING" "$l/winpthreads.txt"
cp "$licenses/mingw64-libgcc/COPYING3" "$l/libgcc-GPL-3.0.txt"
cp "$licenses/mingw64-libgcc/COPYING.RUNTIME" "$l/libgcc-runtime-exception.txt"
echo 'Pixel Operator by Jayvee Enaguas. CC0. See NOTICE.' > "$l/PixelOperator.txt"
printf 'Source: https://github.com/lidor30/pokedaisy\nVersion: %s\nCommit: %s\nBuild: windows/CMakeLists.txt at this commit (MinGW-w64)\n' \
  "$version" "$commit" > "$dest/SOURCE.txt"

# Every imported DLL is Windows' own or one of the zip's.
missing=0
have=$(ls "$dest" | tr 'A-Z' 'a-z')
for f in "$dest"/*.exe "$dest"/*.dll; do
  for dll in $("$objdump" -p "$f" | sed -n 's/.*DLL Name: //p'); do
    lower=$(echo "$dll" | tr 'A-Z' 'a-z')
    case "$lower" in
      kernel32.dll|user32.dll|gdi32.dll|msvcrt.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|winmm.dll|imm32.dll|\
      version.dll|setupapi.dll|ws2_32.dll|bcrypt.dll|ntdll.dll|opengl32.dll|shlwapi.dll|cfgmgr32.dll|dinput8.dll|\
      comdlg32.dll|dwmapi.dll|uxtheme.dll|hid.dll|api-ms-win-*) ;;
      *) grep -qx "$lower" <<< "$have" || { echo "missing: $(basename "$f") imports $dll" >&2; missing=1; } ;;
    esac
  done
done
[ "$missing" = 0 ] || exit 1

rm -f "$out/$name.zip"
(cd "$stage" && zip -qr "$out/$name.zip" "$name")
rm -rf "$stage"
ls -l "$out/$name.zip"
