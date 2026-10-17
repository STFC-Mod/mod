[CmdletBinding()]
param([string]$TomlInclude, [string]$JsonInclude)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $repoRoot 'build/settings-test'
$fixtureRoot = Join-Path $testRoot ([guid]::NewGuid().ToString())

function Invoke-SettingsFixture {
    param([string]$Name, [string[]]$Sources, [string[]]$TestArguments = @(),
        [string[]]$IncludeDirectories = @(), [string[]]$LinkArguments = @())
    $executable = Join-Path $testRoot ($Name + '.exe')
    $compilerArguments = @('--driver-mode=cl', '/std:c++latest', '/EHsc', '/MT', '/DNOMINMAX')
    $compilerArguments += @($IncludeDirectories | ForEach-Object { "/I$_" })
    $compilerArguments += @('/Imods/src', '/Itests', '/Ithird_party/libil2cpp', "/I$TomlInclude",
        "/Fe$executable", "/Fo$testRoot/", '-Wno-deprecated-literal-operator')
    & clang++ @compilerArguments @Sources @LinkArguments
    if ($LASTEXITCODE -ne 0) { throw "Settings fixture compilation failed: $Name" }
    & $executable @TestArguments
    if ($LASTEXITCODE -ne 0) { throw "Settings fixture failed: $Name" }
}

function Get-PermissionState([string]$Path) {
    $acl = Get-Acl -LiteralPath $Path
    [ordered]@{
        Owner = $acl.Owner
        Group = $acl.Group
        Protected = $acl.AreAccessRulesProtected
        Rules = @($acl.Access | ForEach-Object {
            [ordered]@{
                Identity = $_.IdentityReference.Value
                Rights = $_.FileSystemRights.ToString()
                Type = $_.AccessControlType.ToString()
                Inherited = $_.IsInherited
                Inheritance = $_.InheritanceFlags.ToString()
                Propagation = $_.PropagationFlags.ToString()
            }
        })
    } | ConvertTo-Json -Depth 5 -Compress
}

Push-Location $repoRoot
try {
    if (-not $TomlInclude) {
        $packageRoot = Join-Path $env:LOCALAPPDATA '.xmake/packages/t/toml++'
        $header = Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter toml.h |
            Where-Object { $_.Directory.Name -eq 'toml++' } | Select-Object -First 1
        if (-not $header) { throw 'Build the mod first, or supply -TomlInclude.' }
        $TomlInclude = $header.Directory.Parent.FullName
    }
    if (-not $JsonInclude) {
        $packageRoot = Join-Path $env:LOCALAPPDATA '.xmake/packages/n/nlohmann_json'
        $header = Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter json.hpp |
            Where-Object { $_.Directory.Name -eq 'nlohmann' } | Select-Object -First 1
        if (-not $header) { throw 'Build the mod first, or supply -JsonInclude.' }
        $JsonInclude = $header.Directory.Parent.FullName
    }
    New-Item -ItemType Directory -Force $fixtureRoot | Out-Null
    $packageIncludes = @('s/simdutf', 'e/eastl') | ForEach-Object {
        $package = Join-Path $env:LOCALAPPDATA ('.xmake/packages/' + $_)
        Get-ChildItem -LiteralPath $package -Directory -Recurse |
            Where-Object Name -eq 'include' | Select-Object -First 1 -ExpandProperty FullName
    }
    foreach ($fixture in @('boolean_settings', 'boolean_view', 'native_boolean_callback', 'page_catalog',
        'page_sections', 'settings_search', 'choice_setting', 'slider_setting', 'shortcut_editor',
        'settings_metadata', 'persisted_setting', 'settings_maintenance', 'input_release')) {
        Invoke-SettingsFixture -Name $fixture -Sources @("tests/${fixture}_test.cc")
    }
    Invoke-SettingsFixture -Name 'upstream_settings' -Sources @('tests/upstream_settings_test.cc',
        'mods/src/settings/upstream_settings.cc', 'mods/src/settings/warp_mode.cc',
        'mods/src/settings/forbidden_tech.cc', 'mods/src/config_filters.cc') `
        -IncludeDirectories (@('tests/upstream_settings_fixture', $JsonInclude) + $packageIncludes) `
        -LinkArguments @('/link', '/NODEFAULTLIB:GameAssembly.lib') `
        -TestArguments @('docs/settings-coverage.json', (Join-Path $fixtureRoot 'catalog'))
    Invoke-SettingsFixture -Name 'runtime_config' -Sources @('tests/runtime_config_test.cc')
    Invoke-SettingsFixture -Name 'runtime_config_writer' -Sources @('tests/runtime_config_writer_test.cc')
    Invoke-SettingsFixture -Name 'shortcut_options' -Sources @('tests/shortcut_hint_cache.cc',
        'mods/src/patches/mapkey.cc', 'mods/src/patches/modifierkey.cc') -IncludeDirectories $packageIncludes `
        -LinkArguments @('/link', '/NODEFAULTLIB:GameAssembly.lib')
    Invoke-SettingsFixture -Name 'config_save' -Sources @('tests/config_save_test.cc', 'mods/src/config_save.cc') `
        -TestArguments @(Join-Path $fixtureRoot 'created')
    Invoke-SettingsFixture -Name 'config_save_failure' -Sources @('tests/config_save_failure_test.cc') `
        -TestArguments @(Join-Path $fixtureRoot 'failures')
    Invoke-SettingsFixture -Name 'toml_editor' -Sources @('tests/toml_editor_test.cc',
        'mods/src/toml_editor.cc', 'mods/src/config_save.cc') -TestArguments @(Join-Path $fixtureRoot 'editor')
    Invoke-SettingsFixture -Name 'runtime_config_persistence' -Sources @('tests/runtime_config_persistence_test.cc',
        'mods/src/runtime_config_writer.cc', 'mods/src/toml_editor.cc', 'mods/src/config_save.cc') `
        -TestArguments @(Join-Path $fixtureRoot 'reload')

    # Verify actual replacement permissions, including explicit ACLs; only this
    # run's newly created fixture files are modified.
    $permissionRoot = Join-Path $fixtureRoot 'permissions'
    New-Item -ItemType Directory -Path $permissionRoot | Out-Null
    $permissionFile = Join-Path $permissionRoot 'settings.toml'
    Set-Content -LiteralPath $permissionFile -Value 'enabled = false'
    if (-not ((Get-Acl -LiteralPath $permissionFile).Access | Where-Object IsInherited)) {
        throw 'Fixture must have inherited permission entries.'
    }
    $inherited = Get-PermissionState $permissionFile
    & (Join-Path $testRoot 'config_save.exe') $permissionRoot
    if ($LASTEXITCODE -ne 0 -or (Get-PermissionState $permissionFile) -ne $inherited) {
        throw 'Inherited ACL regression failed.'
    }
    $acl = Get-Acl -LiteralPath $permissionFile
    $acl.SetAccessRuleProtection($true, $true)
    Set-Acl -LiteralPath $permissionFile -AclObject $acl
    $explicit = Get-PermissionState $permissionFile
    & (Join-Path $testRoot 'config_save.exe') $permissionRoot
    if ($LASTEXITCODE -ne 0 -or (Get-PermissionState $permissionFile) -ne $explicit) {
        throw 'Explicit ACL regression failed.'
    }

    foreach ($mode in @('idle', 'deadline', 'finished', 'missing-handle')) {
        $outputPath = Join-Path $fixtureRoot ('force-' + $mode + '.txt')
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $child = Start-Process -FilePath (Join-Path $testRoot 'runtime_config.exe') `
            -ArgumentList $mode -WindowStyle Hidden -PassThru -RedirectStandardOutput $outputPath
        if (-not $child.WaitForExit(5000)) {
            $child.Kill()
            throw "Force-close fixture stalled: $mode"
        }
        $child.Refresh()
        if ($child.ExitCode -ne 1) { throw "Force-close fixture did not terminate: $mode" }
        if ($mode -eq 'deadline' -and ($timer.ElapsedMilliseconds -lt 450 -or
            (Get-Content $outputPath -Raw) -notmatch 'pending cancellation requested')) {
            throw 'Deadline did not allow best effort and request cancellation.'
        }
    }
    Write-Output 'All local settings fixtures passed, including persistence, ACLs and force-close deadlines.'
} finally {
    Pop-Location
}
