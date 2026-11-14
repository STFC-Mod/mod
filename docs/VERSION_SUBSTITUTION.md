# Launcher version generation

`src/mod/version.h` defines the four version components.
The launcher xmake target reads them during configuration and replaces `${VERSION}` in `src/launcher/macos/Info.plist.template`.
The generated `Info.plist` supplies `CFBundleShortVersionString` and `CFBundleVersion` to the app bundle.

Run `python3 scripts/verify_version_substitution.py` to check template substitution against the header.
Generated plist files are build outputs and must not be committed.
