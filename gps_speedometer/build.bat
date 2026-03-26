@echo off
REM GPS测速仪 - Windows构建脚本
REM 用法: build.bat [sim|test|clean]

if not exist build mkdir build

set CC=gcc
set CFLAGS=-Wall -Wextra -std=c99 -g -O0 -DPLATFORM_SIM
set INCLUDES=-I. -Iconfig -Iplatform -Ilib -Iapp

set APP_SRC=app/app_main.c app/app_speed.c app/app_display.c app/app_ble_hid.c app/app_key.c
set LIB_SRC=lib/nmea_parser.c
set SIM_SRC=platform/platform_sim/hal_sim.c platform/platform_sim/sim_main.c

if "%1"=="clean" (
    echo 清理...
    rmdir /s /q build 2>nul
    del sim_flash.bin 2>nul
    echo 清理完成
    goto :eof
)

if "%1"=="test" (
    echo 编译测试...
    set TEST_SRC=tests/test_nmea.c tests/test_speed.c tests/test_main.c
    %CC% %CFLAGS% %INCLUDES% -o build/gps_test.exe %LIB_SRC% app/app_speed.c platform/platform_sim/hal_sim.c tests/test_nmea.c tests/test_speed.c tests/test_main.c -lm
    if %errorlevel% equ 0 (
        echo 运行测试...
        build\gps_test.exe
    )
    goto :eof
)

REM 默认编译模拟器
echo 编译模拟器...
%CC% %CFLAGS% %INCLUDES% -o build/gps_sim.exe %LIB_SRC% %APP_SRC% %SIM_SRC% -lm
if %errorlevel% equ 0 (
    echo 编译成功: build\gps_sim.exe
    echo 运行: build\gps_sim.exe
)
