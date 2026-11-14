# Run after zig build mods il2cpp -Dtarget=x86_64-windows-msvc -Dmoddbg=true -Dlto=false.
# Uses production MapKey/ModifierKey objects from mods.lib; does not launch or modify the game.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    if (-not (Test-Path -LiteralPath 'zig-out/lib/mods.lib') -or -not (Test-Path -LiteralPath 'zig-out/lib/il2cpp.lib')) {
        throw 'Build the mods and il2cpp targets with -Dlto=false first.'
    }
    # Use Microsoft link.exe for the Windows static CRT and import libraries.
    & clang++ --driver-mode=cl -fuse-ld=link /std:c++latest /EHsc /MT /Imods/src /Ilibs/il2cpp/include `
        tests/shortcut_hint_cache.cc /Fozig-out/bin/shortcut_hint_cache.obj /Fezig-out/bin/shortcut_hint_cache.exe `
        /link /LTCG /LIBPATH:libs/il2cpp zig-out/lib/mods.lib zig-out/lib/il2cpp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Shortcut hint test compilation failed.' }
    & ./zig-out/bin/shortcut_hint_cache.exe
    if ($LASTEXITCODE -ne 0) { throw 'Shortcut hint cache regression failed.' }
} finally {
    Pop-Location
}
