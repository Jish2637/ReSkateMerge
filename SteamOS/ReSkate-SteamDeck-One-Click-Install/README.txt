Extract all files into your SteamOS Transfer folder and use the same desktop launcher.
Finish any running Steam games before starting the installer.

Scan the QR code once using Steam mobile > Steam Guard.
There is no username-saving prompt or remembered-login option.
The download runs with disposable authentication storage that is removed on exit.
The installer does not reuse saved sign-ins from previous installer versions.
Existing saved logins from older versions are not erased by this update.

After downloading, it automatically closes Steam normally, backs up its config,
sets skate.'s launch command and Proton Experimental, then restarts Steam.
If multiple accounts exist without a unique most-recent account, it asks which
account to configure. If Steam will not exit, it stops instead of editing live files.
All three program files must stay together. The desktop launcher is unchanged.

To retry just the Steam settings after download:
python3 configure-steam.py steam-launch-options.txt

Local syntax and simulated Steam configuration checks passed.
Actual SteamOS QR sign-in and Proton launching remain untested here.
