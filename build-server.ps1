param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [ValidateRange(1, 32)]
    [int]$Parallel = 2,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
$buildDir = Join-Path $root 'build\vs2026-server-release'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE."
    }
}

Push-Location -LiteralPath $root
try {
    if (-not (Test-Path -LiteralPath (Join-Path $root 'CMakePresets.json'))) {
        throw 'Put these scripts in the root of the merged ReSkate source (beside CMakePresets.json).'
    }
    if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
        throw 'cmake.exe was not found. Install Visual Studio 2026 with C++ and CMake support.'
    }
    $configureArgs = @('--preset', 'vs2026-server-release', '-DDINGOSDK_LAUNCHER_AUTO_UPDATE=OFF')
    if ($RunTests) { $configureArgs += '-DDINGOSDK_BUILD_MULTIPLAYER_TESTS=ON' }
    Invoke-Checked 'cmake.exe' $configureArgs
    Invoke-Checked 'cmake.exe' @('--build', $buildDir, '--config', $Configuration,
                                 '--target', 'dingosdk_server', '--parallel', [string]$Parallel)

    if ($RunTests) {
        if (-not (Get-Command ctest.exe -ErrorAction SilentlyContinue)) {
            throw 'ctest.exe was not found.'
        }
        Invoke-Checked 'cmake.exe' @('--build', $buildDir, '--config', $Configuration,
                                     '--target', 'dingosdk_server_config_tests',
                                     'dingosdk_server_plugin_tests', 'dingosdk_msc_protocol55_tests',
                                     '--parallel', [string]$Parallel)
        Invoke-Checked 'ctest.exe' @('--test-dir', $buildDir, '-C', $Configuration,
                                     '-R', '^(server_config|server_plugins|msc_protocol55)$',
                                     '--output-on-failure')
    }

    $exe = Join-Path (Join-Path $buildDir $Configuration) 'ReSkateServer.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Build finished but required output is missing: $exe"
    }
    Write-Host "MSC server compiled: $exe" -ForegroundColor Green
    Write-Host 'For a local MSC test, run the server with --no-update to avoid replacing this build.'
} finally {
    Pop-Location
}
