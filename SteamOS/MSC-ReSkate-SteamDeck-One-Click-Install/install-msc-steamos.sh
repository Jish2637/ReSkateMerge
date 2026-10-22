#!/usr/bin/env bash
# ReSkate SteamOS installer: downloads the pinned Steam depot itself.
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
LOG="$SCRIPT_DIR/log.txt"
INSTALL_DIR="$HOME/Games/ReSkate"
TOOLS_DIR="$HOME/.local/share/msc-steamos-installer"
APP=3354750
DEPOT=3354751
MANIFEST=4621099302092747785
# Game pins above are replaced with the selected MSC release requirements.
mkdir -p "$INSTALL_DIR" "$TOOLS_DIR"
# Log output without logging terminal input (including Steam passwords).
exec > >(tee "$LOG") 2>&1
trap 'rc=$?; printf "\nERROR: command failed near line %s (exit %s). See %s\n" "$LINENO" "$rc" "$LOG" >&2' ERR
trap 'rc=$?; if (( rc )); then printf "Installer failed (exit %s). Log: %s\n" "$rc" "$LOG"; fi' EXIT
printf 'Steam will close and restart automatically at the end. Finish active games before continuing.\n'
printf 'MSC SteamOS setup -- %s\nLog: %s\nInstall folder: %s\n' "$(date -Is)" "$LOG" "$INSTALL_DIR"
fail(){ printf '\nERROR: %s\n' "$*" >&2; exit 1; }
command -v python3 >/dev/null || fail 'python3 is needed to fetch release metadata.'
command -v curl >/dev/null || fail 'curl is needed.'
command -v unzip >/dev/null || fail 'unzip is needed.'
[[ "$(uname -m)" == x86_64 ]] || fail 'This installer requires x86_64 Linux.'
[[ -t 0 ]] || fail 'Steam sign-in requires an interactive terminal. Open this installer using Run in Konsole, or use the included desktop launcher.'
[[ -f "$SCRIPT_DIR/fetch-msc.py" && -f "$SCRIPT_DIR/configure-steam.py" ]] || fail 'Extract all installer files into the same folder.'
free_kib=$(df -Pk "$INSTALL_DIR" | awk 'END {print $4}')
if (( free_kib < 16 * 1024 * 1024 )); then
  printf 'WARNING: Less than 16 GiB free. Depot may fail to download.\n'
  read -r -p 'Continue? [y/N] ' cont
  [[ "$cont" =~ ^[yY]$ ]] || exit 1
fi
printf '\n[1/4] Retrieving latest MSC client release...\n'
python3 "$SCRIPT_DIR/fetch-msc.py" "$INSTALL_DIR" "$TOOLS_DIR"
python3 - "$TOOLS_DIR/msc-game.json" <<'PYGAME' > "$TOOLS_DIR/game-values.txt"
import json,sys
with open(sys.argv[1]) as f: game=json.load(f)
for key in ('app_id','depot_id','manifest_id','skate_sha256'): print(game[key])
PYGAME
mapfile -t GAME_VALUES < "$TOOLS_DIR/game-values.txt"
APP="${GAME_VALUES[0]}"
DEPOT="${GAME_VALUES[1]}"
MANIFEST="${GAME_VALUES[2]}"
GAME_SHA256="${GAME_VALUES[3]}"
printf '\n[2/4] Downloading Linux DepotDownloader...\n'
export TOOLS_DIR
python3 - <<'PY' > "$TOOLS_DIR/depot-url.txt"
import json,urllib.request,sys
req=urllib.request.Request('https://api.github.com/repos/SteamRE/DepotDownloader/releases/latest',headers={'User-Agent':'ReSkateSteamOSInstaller','Accept':'application/vnd.github+json'})
with urllib.request.urlopen(req,timeout=35) as response: data=json.load(response)
for a in data['assets']:
    print('Available DepotDownloader asset:',a['name'],file=sys.stderr)
assets=[a for a in data['assets'] if 'linux' in a['name'].lower() and 'x64' in a['name'].lower() and a['name'].lower().endswith(('.zip','.tar.gz','.tgz'))]
if len(assets)!=1: sys.exit(f'Cannot identify unique Linux x64 DepotDownloader archive: {[a["name"] for a in assets]}')
print(assets[0]['browser_download_url'])
PY
DEPOT_URL=$(cat "$TOOLS_DIR/depot-url.txt")
DEPOT_ARCHIVE="$TOOLS_DIR/$(basename "${DEPOT_URL%%\?*}")"
curl -fL --retry 3 --connect-timeout 25 "$DEPOT_URL" -o "$DEPOT_ARCHIVE"
DEPOT_BIN_DIR="$TOOLS_DIR/DepotDownloader"
mkdir -p "$DEPOT_BIN_DIR"
case "$DEPOT_ARCHIVE" in
  *.zip) unzip -oq "$DEPOT_ARCHIVE" -d "$DEPOT_BIN_DIR";;
  *.tar.gz|*.tgz) tar -xzf "$DEPOT_ARCHIVE" -C "$DEPOT_BIN_DIR";;
  *) fail "Unsupported archive: $DEPOT_ARCHIVE";;
esac
DEPOT_EXE=$(find "$DEPOT_BIN_DIR" -type f -name DepotDownloader -print -quit)
DEPOT_DLL=$(find "$DEPOT_BIN_DIR" -type f -name DepotDownloader.dll -print -quit)
if [[ -n "$DEPOT_EXE" ]]; then
  chmod +x "$DEPOT_EXE"
  downloader=("$DEPOT_EXE")
elif [[ -n "$DEPOT_DLL" ]] && command -v dotnet >/dev/null; then
  downloader=(dotnet "$DEPOT_DLL")
else
  fail 'DepotDownloader binary not found (or the archive requires dotnet, which is not installed).'
fi
printf '\n[3/4] Downloading pinned skate. Windows depot\nApp: %s  Depot: %s  Manifest: %s\n' "$APP" "$DEPOT" "$MANIFEST"
# QR sign-in is used only for this install. DepotDownloader writes auth state
# even without remember-password, so give it a disposable home/data directory.
umask 077
printf 'Open Steam mobile > Steam Guard, scan the QR code, and approve sign-in.\n'
SESSION_HOME=$(mktemp -d "$TOOLS_DIR/qr-session.XXXXXXXX")
cleanup_session() {
    if [[ -n "${SESSION_HOME:-}" && "$SESSION_HOME" == "$TOOLS_DIR"/qr-session.* ]]; then
        rm -rf -- "$SESSION_HOME"
    fi
}
trap 'rc=$?; cleanup_session; if (( rc )); then printf "Installer failed (exit %s). Log: %s\n" "$rc" "$LOG"; fi' EXIT
mkdir -p "$SESSION_HOME/data" "$SESSION_HOME/config" "$SESSION_HOME/cache" "$SESSION_HOME/state"
# Keep the sign-in output in Konsole, outside the log. No saved-login option.
(cd "$DEPOT_BIN_DIR" && env HOME="$SESSION_HOME" XDG_DATA_HOME="$SESSION_HOME/data" XDG_CONFIG_HOME="$SESSION_HOME/config" XDG_CACHE_HOME="$SESSION_HOME/cache" XDG_STATE_HOME="$SESSION_HOME/state" DOTNET_CLI_HOME="$SESSION_HOME" "${downloader[@]}" -app "$APP" -depot "$DEPOT" -manifest "$MANIFEST" -os windows -osarch 64 -dir "$INSTALL_DIR" -qr -validate) > /dev/tty 2>&1
cleanup_session
SESSION_HOME=''
[[ -s "$INSTALL_DIR/Skate.exe" ]] || fail 'DepotDownloader exited, but Skate.exe was not found in the ReSkate folder.'
printf '%s  %s\n' "$GAME_SHA256" "$INSTALL_DIR/Skate.exe" | sha256sum -c - || fail 'Skate.exe does not match the MSC-supported build.'
printf 'Depot download verified: %s (%s bytes)\n' "$INSTALL_DIR/Skate.exe" "$(stat -c %s "$INSTALL_DIR/Skate.exe")"
printf '\n[4/4] Preparing Steam configuration\n'
# Save launch options before applying them to Steam.
LAUNCH_OPTIONS="bash -c 'exec \"\${@/*EAAntiCheat.GameServiceLauncher.exe/$INSTALL_DIR/ReSkateLauncher.exe}\"' -- %command%"
printf '%s\n' "$LAUNCH_OPTIONS" > "$SCRIPT_DIR/steam-launch-options.txt"
printf 'Steam launch options saved: %s\n' "$SCRIPT_DIR/steam-launch-options.txt"
if command -v wl-copy >/dev/null 2>&1; then
    printf '%s' "$LAUNCH_OPTIONS" | wl-copy || true
    printf 'Launch options copied to clipboard (Wayland).\n'
elif command -v xclip >/dev/null 2>&1; then
    printf '%s' "$LAUNCH_OPTIONS" | xclip -selection clipboard || true
fi
printf '\nSUCCESS: MSC launcher and matching game depot installed in %s\n' "$INSTALL_DIR"
printf '\nApplying Steam launch options and Proton Experimental...\n'
python3 "$SCRIPT_DIR/configure-steam.py" "$SCRIPT_DIR/steam-launch-options.txt" || fail 'Game files are installed, but Steam settings were not applied. See the error above; you can rerun configure-steam.py without downloading again.'
printf 'Steam setup complete. Start skate. from your Steam library.\n' 
printf 'Installer log: %s\n' "$LOG"
