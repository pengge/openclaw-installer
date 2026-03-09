/*
 * OpenClaw 一键安装工具
 * 关注抖音:低调的吹个牛
 * 
 * 静态编译 64位 Windows 安装器
 * 支持检测已安装软件版本，跳过重复安装
 */

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

// ============================================================
// 版本与配置
// ⚠ TOOL_VERSION：本地编译时手动维护，发布时由 CI 自动从 Git Tag 注入。
//   发布流程：更新此处版本号 → commit → git tag vX.Y.Z → git push --tags
// ============================================================
#define TOOL_VERSION        L"1.0.0"
#define NODEJS_VERSION      L"22.22.1"
#define GIT_VERSION         L"2.53.0"
#define PYTHON_VERSION      L"3.14.3"
#define VC_REDIST_VERSION   L"2022"

#define NODEJS_URL          L"https://nodejs.org/dist/v22.22.1/node-v22.22.1-x64.msi"
#define GIT_URL_DIRECT      L"https://github.com/git-for-windows/git/releases/download/v2.53.0.windows.1/Git-2.53.0-64-bit.exe"
#define GIT_URL_PROXY       L"https://gh-proxy.org/https://github.com/git-for-windows/git/releases/download/v2.53.0.windows.1/Git-2.53.0-64-bit.exe"
#define PYTHON_URL          L"https://www.python.org/ftp/python/3.14.3/python-3.14.3-amd64.exe"
#define VCREDIST_URL        L"https://aka.ms/vs/17/release/vc_redist.x64.exe"

#define TEMP_DIR            L"%TEMP%\\openclaw_installer"

// 控制台颜色
#define COLOR_RESET     7
#define COLOR_GREEN     10
#define COLOR_YELLOW    14
#define COLOR_RED       12
#define COLOR_CYAN      11
#define COLOR_WHITE     15
#define COLOR_MAGENTA   13

// ============================================================
// 全局句柄
// ============================================================
static HANDLE g_hConsole = INVALID_HANDLE_VALUE;
static wchar_t g_tempDir[MAX_PATH] = {0};
static BOOL g_debugMode = FALSE;

// ============================================================
// 控制台工具函数
// ============================================================
void SetColor(int color) {
    if (g_hConsole != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(g_hConsole, (WORD)color);
}

void PrintDebug(const wchar_t* fmt, ...) {
    if (!g_debugMode) return;
    SetColor(COLOR_MAGENTA);
    wprintf(L"  [DBG] ");
    SetColor(COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
}

void PrintBanner() {
    SetColor(COLOR_CYAN);
    wprintf(L"\n");
    wprintf(L"  ╔══════════════════════════════════════════════════════════════╗\n");
    wprintf(L"  ║         OpenClaw 一键安装工具  v%s                     ║\n", TOOL_VERSION);
    wprintf(L"  ║              关注抖音: 低调的吹个牛                            ║\n");
    wprintf(L"  ╚══════════════════════════════════════════════════════════════╝\n");
    wprintf(L"\n");
    SetColor(COLOR_RESET);
}

void PrintStep(int step, const wchar_t* name) {
    SetColor(COLOR_YELLOW);
    wprintf(L"\n  ▶ [步骤 %d] %s\n", step, name);
    SetColor(COLOR_RESET);
}

void PrintOK(const wchar_t* fmt, ...) {
    SetColor(COLOR_GREEN);
    wprintf(L"  ✓ ");
    SetColor(COLOR_WHITE);
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
    SetColor(COLOR_RESET);
}

void PrintInfo(const wchar_t* fmt, ...) {
    SetColor(COLOR_CYAN);
    wprintf(L"  → ");
    SetColor(COLOR_RESET);
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
}

void PrintWarn(const wchar_t* fmt, ...) {
    SetColor(COLOR_YELLOW);
    wprintf(L"  ⚠ ");
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
    SetColor(COLOR_RESET);
}

void PrintError(const wchar_t* fmt, ...) {
    SetColor(COLOR_RED);
    wprintf(L"  ✗ ");
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
    SetColor(COLOR_RESET);
}

// ============================================================
// 注册表查询工具
// ============================================================
BOOL RegGetStringValue(HKEY hRootKey, const wchar_t* subKey, const wchar_t* valueName, wchar_t* outBuf, DWORD outBufSize) {
    HKEY hKey;
    BOOL result = FALSE;
    if (RegOpenKeyExW(hRootKey, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        DWORD type = REG_SZ;
        DWORD size = outBufSize * sizeof(wchar_t);
        if (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)outBuf, &size) == ERROR_SUCCESS)
            result = TRUE;
        RegCloseKey(hKey);
    }
    // 也尝试32位注册表
    if (!result && RegOpenKeyExW(hRootKey, subKey, 0, KEY_READ | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS) {
        DWORD type = REG_SZ;
        DWORD size = outBufSize * sizeof(wchar_t);
        if (RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)outBuf, &size) == ERROR_SUCCESS)
            result = TRUE;
        RegCloseKey(hKey);
    }
    return result;
}

// ============================================================
// 运行命令并获取输出
// ============================================================
BOOL RunCommandGetOutput(const wchar_t* cmd, wchar_t* outBuf, DWORD outBufSize) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
        return FALSE;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};
    // 用 cmd.exe /c 执行，支持 .cmd 脚本（npm、openclaw 等都是 .cmd 文件）
    wchar_t cmdBuf[2048];
    swprintf(cmdBuf, 2047, L"cmd.exe /c \"%s\"", cmd);

    BOOL ok = CreateProcessW(NULL, cmdBuf, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWritePipe);
    if (!ok) { CloseHandle(hReadPipe); return FALSE; }

    char ansiOut[8192] = {0};
    DWORD bytesRead = 0, total = 0;
    while (ReadFile(hReadPipe, ansiOut + total, sizeof(ansiOut) - total - 1, &bytesRead, NULL) && bytesRead > 0)
        total += bytesRead;
    ansiOut[total] = 0;

    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    // 去除换行
    for (int i = (int)total - 1; i >= 0; i--) {
        if (ansiOut[i] == '\r' || ansiOut[i] == '\n') ansiOut[i] = 0;
        else break;
    }

    MultiByteToWideChar(CP_ACP, 0, ansiOut, -1, outBuf, (int)outBufSize);
    return total > 0;
}

// ============================================================
// 检测各软件安装状态
// ============================================================

// 检测 Node.js
BOOL CheckNodeJS(wchar_t* versionOut, DWORD versionOutSize) {
    wchar_t ver[256] = {0};
    if (RunCommandGetOutput(L"node --version", ver, 255)) {
        // 格式: v22.22.1
        if (ver[0] == L'v') {
            wcsncpy(versionOut, ver + 1, versionOutSize - 1);
        } else {
            wcsncpy(versionOut, ver, versionOutSize - 1);
        }
        return TRUE;
    }
    // 尝试注册表
    wchar_t regVer[256] = {0};
    if (RegGetStringValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Node.js", L"Version", regVer, 255)) {
        wcsncpy(versionOut, regVer, versionOutSize - 1);
        return TRUE;
    }
    return FALSE;
}

// 检测 Git
BOOL CheckGit(wchar_t* versionOut, DWORD versionOutSize) {
    wchar_t ver[256] = {0};
    if (RunCommandGetOutput(L"git --version", ver, 255)) {
        // 格式: git version 2.53.0.windows.1
        wchar_t* p = wcsstr(ver, L"version ");
        if (p) p += 8;
        else p = ver;
        wcsncpy(versionOut, p, versionOutSize - 1);
        return TRUE;
    }
    wchar_t regVer[256] = {0};
    if (RegGetStringValue(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\GitForWindows", L"CurrentVersion", regVer, 255)) {
        wcsncpy(versionOut, regVer, versionOutSize - 1);
        return TRUE;
    }
    return FALSE;
}

// 检测 Python
BOOL CheckPython(wchar_t* versionOut, DWORD versionOutSize) {
    wchar_t ver[256] = {0};
    if (RunCommandGetOutput(L"python --version", ver, 255)) {
        wchar_t* p = wcsstr(ver, L"Python ");
        if (p) p += 7;
        else p = ver;
        wcsncpy(versionOut, p, versionOutSize - 1);
        return TRUE;
    }
    if (RunCommandGetOutput(L"python3 --version", ver, 255)) {
        wchar_t* p = wcsstr(ver, L"Python ");
        if (p) p += 7;
        else p = ver;
        wcsncpy(versionOut, p, versionOutSize - 1);
        return TRUE;
    }
    return FALSE;
}

// 检测 VC Redist
BOOL CheckVCRedist(wchar_t* versionOut, DWORD versionOutSize) {
    wchar_t ver[256] = {0};
    // 检查 VC++ 2015-2022 x64
    const wchar_t* keys[] = {
        L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
        L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\X64",
        NULL
    };
    for (int i = 0; keys[i]; i++) {
        if (RegGetStringValue(HKEY_LOCAL_MACHINE, keys[i], L"Version", ver, 255)) {
            wcsncpy(versionOut, ver, versionOutSize - 1);
            return TRUE;
        }
    }
    return FALSE;
}

// 检测 OpenClaw (npm全局包)
// 从可能包含多行警告的输出中提取最后一个非空行
static void ExtractLastLine(wchar_t* buf) {
    wchar_t* lastLine = buf;
    for (wchar_t* p = buf; *p; p++) {
        if (*p == L'\n' && *(p + 1) != L'\0')
            lastLine = p + 1;
    }
    // 去除行末 \r
    wchar_t* cr = wcschr(lastLine, L'\r');
    if (cr) *cr = L'\0';
    if (lastLine != buf)
        wmemmove(buf, lastLine, wcslen(lastLine) + 1);
}

BOOL CheckOpenClaw(wchar_t* versionOut, DWORD versionOutSize) {
    wchar_t ver[2048] = {0};

    // 方法1：直接调用 openclaw（依赖 PATH）
    BOOL ok = RunCommandGetOutput(L"openclaw --version", ver, 2047);
    PrintDebug(L"openclaw --version => ok=%d raw=[%s]", ok, ok ? ver : L"(未找到)");
    if (ok) {
        ExtractLastLine(ver);
        PrintDebug(L"ExtractLastLine => [%s]", ver);
        if (*ver) {
            wcsncpy(versionOut, ver, versionOutSize - 1);
            return TRUE;
        }
    }

    // 方法2：管理员运行时用户 PATH 可能缺失 npm 全局 bin，
    //        通过 "npm config get prefix" 拿到真实安装路径再调用
    wchar_t npmPrefix[MAX_PATH] = {0};
    ok = RunCommandGetOutput(L"npm config get prefix", npmPrefix, MAX_PATH - 1);
    PrintDebug(L"npm config get prefix => ok=%d raw=[%s]", ok, ok ? npmPrefix : L"(失败)");
    if (ok && *npmPrefix) {
        // 去掉末尾空白/换行
        for (int i = (int)wcslen(npmPrefix) - 1; i >= 0; i--) {
            if (npmPrefix[i] <= L' ') npmPrefix[i] = 0;
            else break;
        }
        wchar_t clawCmd[MAX_PATH + 32];
        swprintf(clawCmd, MAX_PATH + 31, L"\"%s\\openclaw.cmd\" --version", npmPrefix);
        PrintDebug(L"尝试完整路径: %s", clawCmd);
        ver[0] = 0;
        ok = RunCommandGetOutput(clawCmd, ver, 2047);
        PrintDebug(L"完整路径结果 => ok=%d raw=[%s]", ok, ok ? ver : L"(失败)");
        if (ok) {
            ExtractLastLine(ver);
            if (*ver) {
                wcsncpy(versionOut, ver, versionOutSize - 1);
                return TRUE;
            }
        }
    }

    // 方法3：npm list -g
    ver[0] = 0;
    ok = RunCommandGetOutput(L"npm list -g openclaw --depth=0", ver, 2047);
    PrintDebug(L"npm list -g => ok=%d raw=[%s]", ok, ok ? ver : L"(失败)");
    if (ok) {
        wchar_t* p = wcsstr(ver, L"openclaw@");
        if (p) {
            p += 9;
            wchar_t* end = wcspbrk(p, L"\r\n");
            if (end) *end = L'\0';
            wcsncpy(versionOut, p, versionOutSize - 1);
            return TRUE;
        }
    }

    return FALSE;
}

// ============================================================
// 下载文件
// ============================================================
BOOL DownloadFile(const wchar_t* url, const wchar_t* destPath) {
    HINTERNET hInternet = InternetOpenW(L"OpenClaw-Installer/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return FALSE;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE |
        INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    HANDLE hFile = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return FALSE;
    }

    char buf[65536];
    DWORD bytesRead, totalBytes = 0;
    BOOL ok = TRUE;
    wprintf(L"  ");
    int dotCount = 0;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        DWORD written;
        WriteFile(hFile, buf, bytesRead, &written, NULL);
        totalBytes += bytesRead;
        // 进度显示
        if ((totalBytes / 65536) > (DWORD)dotCount) {
            dotCount++;
            SetColor(COLOR_CYAN);
            wprintf(L".");
            SetColor(COLOR_RESET);
        }
    }
    wprintf(L" %.1f MB\n", totalBytes / 1048576.0f);

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return (totalBytes > 0);
}

// ============================================================
// 运行安装程序（等待完成）
// ============================================================
BOOL RunInstaller(const wchar_t* path, const wchar_t* args) {
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.lpParameters = args;
    sei.nShow  = SW_SHOW;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED)
            PrintWarn(L"用户取消了UAC提权，跳过此步骤");
        else
            PrintError(L"启动安装程序失败，错误代码: %lu", err);
        return FALSE;
    }
    if (sei.hProcess) {
        PrintInfo(L"正在安装，请等待...");
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
        return (exitCode == 0 || exitCode == 3010); // 3010=需重启但成功
    }
    return TRUE;
}

// ============================================================
// 运行 cmd 命令（显示窗口）
// ============================================================
BOOL RunCommand(const wchar_t* cmd) {
    wchar_t fullCmd[2048];
    swprintf(fullCmd, 2047, L"cmd.exe /c \"%s\"", cmd);

    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessW(NULL, fullCmd, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        return FALSE;

    WaitForSingleObject(pi.hProcess, 120000); // 2分钟超时
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (exitCode == 0);
}

// ============================================================
// 初始化临时目录
// ============================================================
void InitTempDir() {
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    swprintf(g_tempDir, MAX_PATH - 1, L"%sopenclaw_installer", tempBase);
    CreateDirectoryW(g_tempDir, NULL);
}

// ============================================================
// 用户选择（简单菜单）
// ============================================================
int UserChoice(const wchar_t* prompt, const wchar_t** options, int count) {
    SetColor(COLOR_YELLOW);
    wprintf(L"\n  %s\n", prompt);
    SetColor(COLOR_RESET);
    for (int i = 0; i < count; i++) {
        wprintf(L"    [%d] %s\n", i + 1, options[i]);
    }
    SetColor(COLOR_CYAN);
    wprintf(L"  请输入选项编号: ");
    SetColor(COLOR_RESET);
    int choice = 0;
    wscanf(L"%d", &choice);
    if (choice < 1 || choice > count) choice = 1;
    return choice;
}

// ============================================================
// 各步骤安装函数
// ============================================================

void Step1_NodeJS() {
    PrintStep(1, L"安装 Node.js");
    wchar_t version[256] = {0};
    if (CheckNodeJS(version, 255)) {
        PrintOK(L"Node.js 已安装，版本: v%s，跳过下载安装", version);
        return;
    }
    PrintInfo(L"未检测到 Node.js，开始下载 v%s ...", NODEJS_VERSION);
    wchar_t destPath[MAX_PATH];
    swprintf(destPath, MAX_PATH - 1, L"%s\\node-v%s-x64.msi", g_tempDir, NODEJS_VERSION);
    if (!DownloadFile(NODEJS_URL, destPath)) {
        PrintError(L"Node.js 下载失败，请检查网络连接");
        return;
    }
    PrintInfo(L"正在安装 Node.js...");
    RunInstaller(destPath, L"/quiet /norestart");
    wchar_t ver2[256] = {0};
    if (CheckNodeJS(ver2, 255))
        PrintOK(L"Node.js v%s 安装成功", ver2);
    else
        PrintWarn(L"安装完成，可能需要重启终端后生效");
}

void Step2_Git() {
    PrintStep(2, L"安装 Git");
    wchar_t version[256] = {0};
    if (CheckGit(version, 255)) {
        PrintOK(L"Git 已安装，版本: %s，跳过下载安装", version);
        return;
    }
    const wchar_t* dlOptions[] = {
        L"直接下载 (从 GitHub 官方，适合能访问 GitHub 的用户)",
        L"加速下载 (通过 gh-proxy.org 加速，适合国内用户)"
    };
    int choice = UserChoice(L"请选择 Git 下载方式:", dlOptions, 2);
    const wchar_t* url = (choice == 1) ? GIT_URL_DIRECT : GIT_URL_PROXY;
    PrintInfo(L"正在下载 Git v%s ...", GIT_VERSION);
    wchar_t destPath[MAX_PATH];
    swprintf(destPath, MAX_PATH - 1, L"%s\\Git-%s-64-bit.exe", g_tempDir, GIT_VERSION);
    if (!DownloadFile(url, destPath)) {
        PrintError(L"Git 下载失败");
        return;
    }
    RunInstaller(destPath, L"/SILENT /NORESTART");
    wchar_t ver2[256] = {0};
    if (CheckGit(ver2, 255))
        PrintOK(L"Git %s 安装成功", ver2);
    else
        PrintWarn(L"安装完成，可能需要重启终端后生效");
}

void Step3_Python() {
    PrintStep(3, L"安装 Python");
    wchar_t version[256] = {0};
    if (CheckPython(version, 255)) {
        PrintOK(L"Python 已安装，版本: %s，跳过下载安装", version);
        return;
    }
    PrintInfo(L"未检测到 Python，开始下载 v%s ...", PYTHON_VERSION);
    wchar_t destPath[MAX_PATH];
    swprintf(destPath, MAX_PATH - 1, L"%s\\python-%s-amd64.exe", g_tempDir, PYTHON_VERSION);
    if (!DownloadFile(PYTHON_URL, destPath)) {
        PrintError(L"Python 下载失败");
        return;
    }
    // 静默安装，添加到PATH
    RunInstaller(destPath, L"/quiet InstallAllUsers=1 PrependPath=1 Include_test=0");
    wchar_t ver2[256] = {0};
    if (CheckPython(ver2, 255))
        PrintOK(L"Python %s 安装成功", ver2);
    else
        PrintWarn(L"安装完成，可能需要重启终端后生效");
}

void Step4_GitHttps() {
    PrintStep(4, L"配置 Git HTTPS 访问");
    const wchar_t* options[] = {
        L"可正常访问 GitHub（使用标准 HTTPS 配置）",
        L"无法访问 GitHub（使用代理 HTTPS 配置）"
    };
    int choice = UserChoice(L"请选择您的网络情况:", options, 2);
    BOOL ok;
    if (choice == 1) {
        PrintInfo(L"正在设置 Git HTTPS 访问（标准模式）...");
        ok = RunCommand(L"git config --global url.\"https://github.com/\".insteadOf \"ssh://git@github.com/\"");
    } else {
        PrintInfo(L"正在设置 Git HTTPS 访问（代理模式）...");
        ok = RunCommand(L"git config --global url.\"https://gh-proxy.org/https://github.com/\".insteadOf \"ssh://git@github.com/\"");
    }
    if (ok)
        PrintOK(L"Git HTTPS 访问配置完成");
    else
        PrintWarn(L"Git 配置命令执行异常，请确认 Git 已正确安装");
}

void Step5_VCRedist() {
    PrintStep(5, L"安装 Microsoft Visual C++ 运行库");
    wchar_t version[256] = {0};
    if (CheckVCRedist(version, 255)) {
        PrintOK(L"VC++ 运行库已安装，版本: %s，跳过下载安装", version);
        return;
    }
    PrintInfo(L"未检测到 VC++ 运行库，开始下载...");
    wchar_t destPath[MAX_PATH];
    swprintf(destPath, MAX_PATH - 1, L"%s\\vc_redist.x64.exe", g_tempDir);
    if (!DownloadFile(VCREDIST_URL, destPath)) {
        PrintError(L"VC++ 运行库下载失败");
        return;
    }
    RunInstaller(destPath, L"/install /quiet /norestart");
    wchar_t ver2[256] = {0};
    if (CheckVCRedist(ver2, 255))
        PrintOK(L"VC++ 运行库安装成功，版本: %s", ver2);
    else
        PrintOK(L"VC++ 运行库安装完成");
}

void Step6_OpenClaw() {
    PrintStep(6, L"安装 OpenClaw");
    wchar_t version[256] = {0};
    if (CheckOpenClaw(version, 255)) {
        PrintOK(L"OpenClaw 已安装，版本: %s，跳过重复安装", version);
        PrintInfo(L"如需更新，请手动执行: npm install -g openclaw@latest");
        return;
    }
    PrintInfo(L"正在通过 npm 安装 openclaw@latest ...");
    if (!RunCommand(L"npm install -g openclaw@latest")) {
        PrintError(L"openclaw 安装失败，请检查 Node.js 是否正确安装");
        return;
    }
    PrintInfo(L"正在执行 openclaw onboard --install-daemon ...");
    if (!RunCommand(L"openclaw onboard --install-daemon")) {
        PrintWarn(L"守护进程安装异常，请手动执行: openclaw onboard --install-daemon");
        return;
    }
    wchar_t ver2[256] = {0};
    if (CheckOpenClaw(ver2, 255))
        PrintOK(L"OpenClaw %s 安装并启动成功！", ver2);
    else
        PrintOK(L"OpenClaw 安装完成！");
}

// ============================================================
// 打印安装前检测报告，返回 TRUE 表示全部组件已安装
// ============================================================
BOOL PrintPreCheckReport() {
    SetColor(COLOR_YELLOW);
    wprintf(L"\n  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    wprintf(L"  系统检测报告\n");
    wprintf(L"  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    SetColor(COLOR_RESET);

    wchar_t ver[256];
    const wchar_t* items[][2] = {
        {L"Node.js", NODEJS_VERSION},
        {L"Git", GIT_VERSION},
        {L"Python", PYTHON_VERSION},
        {L"VC++ Redist", VC_REDIST_VERSION},
        {L"OpenClaw", L"latest"},
    };

    typedef BOOL(*CheckFunc)(wchar_t*, DWORD);
    CheckFunc checks[] = {CheckNodeJS, CheckGit, CheckPython, CheckVCRedist, CheckOpenClaw};

    int installedCount = 0;
    for (int i = 0; i < 5; i++) {
        ver[0] = 0;
        BOOL installed = checks[i](ver, 255);
        if (installed) {
            installedCount++;
            SetColor(COLOR_GREEN);
            wprintf(L"  ✓ %-20s 已安装  版本: %s\n", items[i][0], ver);
        } else {
            SetColor(COLOR_YELLOW);
            wprintf(L"  ○ %-20s 未安装  (将安装 %s)\n", items[i][0], items[i][1]);
        }
    }
    SetColor(COLOR_RESET);
    wprintf(L"\n");
    return (installedCount == 5);
}

// ============================================================
// 主函数
// ============================================================
int wmain(int argc, wchar_t* argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--debug") == 0 || wcscmp(argv[i], L"/debug") == 0)
            g_debugMode = TRUE;
    }

    // 设置控制台 —— 使用 UTF-16 宽字符模式，彻底绕过代码页限制
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin),  _O_U16TEXT);
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // 设置控制台标题
    SetConsoleTitleW(L"OpenClaw 一键安装工具 - 关注抖音:低调的吹个牛");

    // 调整控制台窗口大小
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(g_hConsole, &csbi)) {
        COORD bufSize = {100, 3000};
        SetConsoleScreenBufferSize(g_hConsole, bufSize);
        SMALL_RECT winRect = {0, 0, 99, 40};
        SetConsoleWindowInfo(g_hConsole, TRUE, &winRect);
    }

    // 检查是否以管理员身份运行（提示，不强制）
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &AdministratorsGroup)) {
        CheckTokenMembership(NULL, AdministratorsGroup, &isAdmin);
        FreeSid(AdministratorsGroup);
    }

    PrintBanner();

    if (!isAdmin) {
        PrintWarn(L"建议以管理员身份运行以确保安装成功");
        PrintWarn(L"右键点击本程序 -> 以管理员身份运行");
        wprintf(L"\n  按 Enter 继续，或关闭窗口后以管理员身份重新运行...");
        getwchar();
    }

    InitTempDir();
    PrintInfo(L"临时文件目录: %s", g_tempDir);

    // 预检测
    BOOL allInstalled = PrintPreCheckReport();

    if (allInstalled) {
        // 全部已安装，无需任何操作
        SetColor(COLOR_GREEN);
        wprintf(L"  ╔══════════════════════════════════════════════════════════════╗\n");
        wprintf(L"  ║       ✓  所有组件均已安装，无需重复安装！                   ║\n");
        wprintf(L"  ║              关注抖音: 低调的吹个牛                            ║\n");
        wprintf(L"  ╚══════════════════════════════════════════════════════════════╝\n");
        SetColor(COLOR_RESET);
        wprintf(L"\n  按 Enter 退出...\n");
        getwchar();
        return 0;
    }

    // 确认开始
    SetColor(COLOR_CYAN);
    wprintf(L"  按 Enter 开始安装所有未安装的组件...");
    SetColor(COLOR_RESET);
    getwchar();

    // 依次执行各步骤
    Step1_NodeJS();
    Step2_Git();
    Step3_Python();
    Step4_GitHttps();
    Step5_VCRedist();
    Step6_OpenClaw();

    // 完成
    SetColor(COLOR_GREEN);
    wprintf(L"\n  ╔══════════════════════════════════════════════════════════════╗\n");
    wprintf(L"  ║            ★  所有组件安装完成！                            ║\n");
    wprintf(L"  ║              关注抖音: 低调的吹个牛                            ║\n");
    wprintf(L"  ╚══════════════════════════════════════════════════════════════╝\n");
    SetColor(COLOR_RESET);
    wprintf(L"\n  按 Enter 退出...\n");
    getwchar();

    // 清理临时文件（可选）
    // RemoveDirectoryW(g_tempDir);

    return 0;
}
