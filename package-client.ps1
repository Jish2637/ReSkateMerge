param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build\protocol55-local'
$outputDir = Join-Path $buildDir $Configuration
$artifactDir = Join-Path $root 'artifacts'
$archive = Join-Path $artifactDir "MSC-ReSkate-Client-Protocol55-FirstPersonFix-$Configuration-Windows-x64.zip"
$stage = Join-Path $artifactDir '.stage-msc-client'

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}
function Assert-SourceBuild {
    $cache = Join-Path $buildDir 'CMakeCache.txt'
    Require-File $cache
    $data = Get-Content -LiteralPath $cache -Raw
    $match = [regex]::Match($data, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$')
    if (-not $match.Success) { throw "Invalid build cache: $cache" }
    $actual = [IO.Path]::GetFullPath($match.Groups[1].Value.Trim()).TrimEnd('\','/')
    $expected = [IO.Path]::GetFullPath($root).TrimEnd('\','/')
    if (-not [string]::Equals($actual, $expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Build cache belongs to '$actual', not '$expected'. Rebuild in this source directory."
    }
}
function Assert-X64PE([string]$Path) {
    Require-File $Path
    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a Windows executable: $Path" }
            $stream.Position = 0x3C
            $offset = $reader.ReadInt32()
            if ($offset -lt 0x40 -or $offset -gt ($stream.Length - 6)) {
                throw "Invalid PE header: $Path"
            }
            $stream.Position = $offset
            if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
                throw "Expected a Windows x64 executable: $Path"
            }
        } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

Push-Location -LiteralPath $root
try {
    Require-File (Join-Path $root 'CMakePresets.json')
    $header = Join-Path $root 'Extension\Multiplayer\Net\protocol.h'
    Require-File $header
    if (-not ((Get-Content -LiteralPath $header -Raw) -match 'protocol_version\s*=\s*55\s*;')) {
        throw 'These scripts require the protocol-55 merged MSC ReSkate source.'
    }
    if (-not $SkipBuild) {
        & (Join-Path $root 'build-client.ps1') -Configuration $Configuration
        if (-not $?) { throw 'Build failed; no archive created.' }
    }
    Assert-SourceBuild
    $items = @()
    $items += [pscustomobject]@{ Name = 'ReSkateLauncher.exe'; Path = (Join-Path $outputDir 'ReSkateLauncher.exe') }
    $items += [pscustomobject]@{ Name = 'ReSkate.dll'; Path = (Join-Path $outputDir 'ReSkate.dll') }
    foreach ($item in $items) { Assert-X64PE $item.Path }
    $presetName = 'scripts/Custom/FirstPersonNative/presets/bundled/recommended.json'
    $presetPath = Join-Path $outputDir $presetName
    Require-File $presetPath
    $items += [pscustomobject]@{ Name = $presetName; Path = $presetPath }

    New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    try {
        foreach ($item in $items) {
            $destination = Join-Path $stage $item.Name
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $item.Path -Destination $destination
        }
        if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
        # Zip only the contents, not a parent folder, so files extract directly to an install folder.
        Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -CompressionLevel Optimal

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        try {
            $names = @( $zip.Entries | Where-Object { $_.Name } | ForEach-Object { $_.FullName.Replace('\','/') })
            $expected = @($items | ForEach-Object { $_.Name })
            if ($names.Count -ne $expected.Count) { throw 'Package contains unexpected files.' }
            foreach ($name in $expected) {
                if ($names -cnotcontains $name) { throw "Missing archive entry: $name" }
            }
        } finally { $zip.Dispose() }
    } finally {
        if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    }
    Write-Host "Package created: $archive" -ForegroundColor Green
} finally {
    Pop-Location
}
