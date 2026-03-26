# GPS测速仪 - PowerShell构建脚本
# 用法: .\build.ps1 [sim|gui|test|clean]

param([string]$target = "gui")

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptDir

try {
    if (-not (Test-Path "build")) { New-Item -ItemType Directory -Path "build" -Force | Out-Null }

    $CC = "gcc"
    $CFLAGS = "-Wall -Wextra -std=c99 -g -O0 -DPLATFORM_SIM"
    $INCLUDES = "-I. -Iconfig -Iplatform -Ilib -Iapp"

    switch ($target) {
        "clean" {
            Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
            Remove-Item sim_flash.bin -ErrorAction SilentlyContinue
            Write-Host "clean done"
        }
        "test" {
            Write-Host "compiling tests..."
            $src = "lib/nmea_parser.c app/app_speed.c platform/platform_sim/hal_sim.c tests/test_nmea.c tests/test_speed.c tests/test_main.c"
            Invoke-Expression "$CC $CFLAGS $INCLUDES -o build/gps_test.exe $src -lm"
            if ($LASTEXITCODE -eq 0) {
                Write-Host "running tests..."
                & ".\build\gps_test.exe"
            }
        }
        "sim" {
            Write-Host "compiling CLI simulator..."
            $src = "lib/nmea_parser.c app/app_main.c app/app_speed.c app/app_display.c app/app_ble_hid.c app/app_key.c platform/platform_sim/hal_sim.c platform/platform_sim/sim_main.c"
            Invoke-Expression "$CC $CFLAGS $INCLUDES -o build/gps_sim.exe $src -lm"
            if ($LASTEXITCODE -eq 0) { Write-Host "done: build\gps_sim.exe" }
        }
        default {
            # GUI模式 (默认)
            Write-Host "compiling GUI simulator..."
            $src = "lib/nmea_parser.c app/app_main.c app/app_speed.c app/app_display.c app/app_ble_hid.c app/app_key.c platform/platform_sim/hal_sim.c platform/platform_sim/sim_gui.c"
            Invoke-Expression "$CC $CFLAGS $INCLUDES -o build/gps_gui.exe $src -lm -lgdi32 -mwindows"
            if ($LASTEXITCODE -eq 0) {
                Write-Host "done: build\gps_gui.exe"
                Write-Host "launching..."
                Start-Process ".\build\gps_gui.exe"
            }
        }
    }
} finally {
    Pop-Location
}
