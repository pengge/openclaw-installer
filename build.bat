@echo off
chcp 65001 >nul
echo.
echo  ==========================================
echo    OpenClaw 安装工具 - 本地编译脚本
echo    关注抖音: 低调的吹个牛
echo  ==========================================
echo.

:: 检查是否在 MSVC 环境中
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 cl.exe，请在以下环境中运行本脚本:
    echo        Visual Studio x64 Native Tools Command Prompt
    echo.
    echo  或者执行:
    echo  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    pause
    exit /b 1
)

:: 读取并显示当前 TOOL_VERSION，提醒开发者核对版本号
for /f "tokens=3 delims= " %%V in ('findstr /r "^#define TOOL_VERSION" src\main.cpp') do set CURRENT_VER=%%~V
:: 去掉首尾的 L" 和 "
set CURRENT_VER=%CURRENT_VER:~2,-1%
echo [版本] 当前 TOOL_VERSION = %CURRENT_VER%
echo [提示] 发布前请确认版本号与 Git Tag 一致，例如: git tag v%CURRENT_VER%
echo.

:: 创建输出目录
if not exist output (
    mkdir output
    echo [信息] 已创建 output 目录
)

echo [编译] 正在静态编译 64位 EXE...
echo.

cl.exe ^
    /nologo /W3 /O2 /EHsc /MT ^
    /utf-8 ^
    /DUNICODE /D_UNICODE ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /Fosrc\ ^
    /Feoutput\openclaw-installer.exe ^
    src\main.cpp ^
    /link /MACHINE:X64 /SUBSYSTEM:CONSOLE ^
    kernel32.lib user32.lib shell32.lib ^
    advapi32.lib wininet.lib shlwapi.lib ole32.lib

if %errorlevel% equ 0 (
    echo.
    echo  ✓ 编译成功！
    echo  ✓ 输出文件: output\openclaw-installer.exe
    for %%F in (output\openclaw-installer.exe) do echo  ✓ 文件大小: %%~zF 字节
    echo.
    echo  关注抖音: 低调的吹个牛
) else (
    echo.
    echo  ✗ 编译失败，请检查错误信息
)

:: 清理中间文件
if exist main.obj del main.obj
if exist output\openclaw-installer.exp del output\openclaw-installer.exp 2>nul
if exist output\openclaw-installer.lib del output\openclaw-installer.lib 2>nul

echo.
pause
