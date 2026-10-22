MSC SteamOS installer

Extract ALL files together in Desktop Mode. Open Run-MSC-Setup.desktop.
Allow execution if SteamOS asks. Your working Python/Konsole launcher is retained.
Alternatively: bash install-msc-steamos.sh
Finish active Steam games first: Steam closes and restarts automatically at the end.

Each run fetches the latest stable client release from Jish2637/ReSkateMerge,
checks its GitHub SHA-256 digest, and installs its launcher, DLL, bundled scripts
and license files. Game requirements come from the SAME release's launcher.json.
Source archives and server packages are excluded.

Uses ~/Games/ReSkate, the SAME directory as your original installer. This replaces
the original ReSkate launcher/DLL with MSC, rather than making another game copy.
Existing game files are verified; only missing or changed depot files download.
Replaced client files are backed up under ~/.local/share/msc-steamos-installer/.
Close ReSkate before running this installer. Keep the launcher's MSC updates enabled.

Scan the QR code with Steam mobile > Steam Guard. No username-saving prompt.
Disposable authentication storage is removed at exit. Steam's current sign-in
is not reused. Old saved tokens from earlier installers are not erased.

At the end, it automatically backs up Steam configuration, sets skate.'s launch
options and Proton Experimental, and restarts Steam. The normal Steam skate.
installation is required for its Play entry. If Steam cannot close, setup stops.
A choice is needed only if there is no unique recent account among multiple users.
Native Steam/SteamOS is supported; Flatpak Steam is not handled.

To retry only Steam settings:
python3 configure-steam.py steam-launch-options.txt

Local tests cover the actual MSC release ZIP, required scripts, backup, Steam config
changes, and Bash syntax. Actual SteamOS QR sign-in/Proton launching remains untested.
