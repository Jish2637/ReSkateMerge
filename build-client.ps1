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
$buildDir = Join-Path $root 'build\protocol55-local'

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
    $configureArgs = @('--preset', 'msc-protocol55-local', '-DDINGOSDK_LAUNCHER_AUTO_UPDATE=OFF')
    if ($RunTests) { $configureArgs += '-DDINGOSDK_BUILD_MULTIPLAYER_TESTS=ON' }
    Invoke-Checked 'cmake.exe' $configureArgs

    # The runtime depends on the launcher; name both targets explicitly.
    Invoke-Checked 'cmake.exe' @('--build', $buildDir, '--config', $Configuration,
                                 '--target', 'dingosdk_launcher', 'dingosdk_runtime',
                                 '--parallel', [string]$Parallel)

    if ($RunTests) {
        if (-not (Get-Command ctest.exe -ErrorAction SilentlyContinue)) {
            throw 'ctest.exe was not found.'
        }
        $testTargets = @('dingosdk_msc_protocol55_tests', 'dingosdk_multiplayer_tests',
                         'dingosdk_throwdown_relay_tests', 'dingosdk_plugin_tests',
                         'dingosdk_trick_logger_tests', 'dingosdk_skate_rules_tests',
                         'dingosdk_terminology_tests', 'dingosdk_first_person_profile_queue_tests',
                         'dingosdk_first_person_stabilize_tests', 'dingosdk_first_person_cycle_tests',
                         'dingosdk_first_person_host_tests', 'dingosdk_first_person_fov_tests',
                         'dingosdk_first_person_camera_host_tests',
                         'dingosdk_mod_manager_tests', 'dingosdk_thunderstore_tests',
                         'dingosdk_thunderstore_package_tests', 'dingosdk_start_maps_tests')
        foreach ($suite in @('board_look', 'glance_toggle', 'look_smoothing', 'shared_settings',
                            'native_stick', 'camera_transition', 'native_glance', 'native_actions',
                            'native_comfort', 'bail_collision', 'native_buttons')) {
            $testTargets += "dingosdk_first_person_${suite}_tests"
        }
        Invoke-Checked 'cmake.exe' (@('--build', $buildDir, '--config', $Configuration, '--target') +
                                    $testTargets + @('--parallel', [string]$Parallel))
        Invoke-Checked 'ctest.exe' @('--test-dir', $buildDir, '-C', $Configuration,
                                     '-R', '^(msc_protocol55|multiplayer_protocol|throwdown_relay|plugins|trick_logger|skate_rules|trick_terminology|first_person_.*|launcher_mod_manager|launcher_thunderstore|thunderstore_package|launcher_start_maps)$', '--output-on-failure')
    }

    $outDir = Join-Path $buildDir $Configuration
    foreach ($name in @('ReSkateLauncher.exe', 'ReSkate.dll')) {
        $file = Join-Path $outDir $name
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Build finished but required output is missing: $file"
        }
    }
    Write-Host "MSC client compiled into: $outDir" -ForegroundColor Green
    Write-Host 'These binaries still require real Windows/game multiplayer testing.'
} finally {
    Pop-Location
}
