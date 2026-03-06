# OpenClaw 一键安装工具

> 关注抖音：**低调的吹个牛**

[![Build](https://github.com/pengge/openclaw-installer/actions/workflows/build.yml/badge.svg)](https://github.com/pengge/openclaw-installer/actions/workflows/build.yml)

## 功能特性

- ✅ **自动检测**：已安装的软件直接显示版本号，**不会重复下载**
- ✅ **静态编译**：单文件 EXE，无需 VC++ 运行时
- ✅ **64位优化**：专为 Windows x64 编译
- ✅ **国内加速**：Git 下载支持选择 gh-proxy.org 加速镜像
- ✅ **GitHub Actions**：推送代码自动编译，打 Tag 自动发布 Release

## 安装组件

| 组件 | 版本 | 说明 |
|------|------|------|
| Node.js | v22.22.1 | 从 nodejs.org 官网下载 |
| Git | v2.53.0 | 支持直连或国内加速 |
| Python | v3.14.3 | 从 python.org 官网下载 |
| VC++ Redist | 2022 | 微软官方 C++ 运行库 |
| OpenClaw | latest | 通过 npm 全局安装 |

## 快速开始

### 使用预编译版本

1. 前往 [Releases](https://github.com/pengge/openclaw-installer/releases) 下载最新版
2. 右键 `openclaw-installer.exe` → **以管理员身份运行**
3. 按屏幕提示操作

### 自行编译

#### 环境要求
- Visual Studio 2019/2022 或 Build Tools（含 C++ 工具集）
- Windows SDK

#### 编译命令
```batch
:: 在 x64 Native Tools Command Prompt 中执行
mkdir output
cl.exe /nologo /W3 /O2 /EHsc /MT /DUNICODE /D_UNICODE ^
    /D_CRT_SECURE_NO_WARNINGS /Feoutput\openclaw-installer.exe ^
    src\main.cpp ^
    /link /MACHINE:X64 /SUBSYSTEM:CONSOLE /LTCG ^
    kernel32.lib user32.lib shell32.lib advapi32.lib ^
    wininet.lib shlwapi.lib ole32.lib
```

### GitHub Actions 自动编译

1. Fork 本仓库
2. 推送代码到 `main` 分支，Actions 自动编译
3. 打版本 Tag 自动发布 Release：
   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

## 项目结构

```
openclaw-installer/
├── src/
│   └── main.cpp              # 主程序源码
├── .github/
│   └── workflows/
│       └── build.yml         # GitHub Actions 编译流程
└── README.md
```

## 技术说明

- 使用 `/MT` 标志静态链接 CRT，无需额外运行时
- 通过 Windows 注册表 + 命令行双重检测软件安装状态
- 使用 WinINet API 实现文件下载，支持 HTTPS
- 使用 ShellExecuteEx + UAC 提权运行安装程序

---
关注抖音：**低调的吹个牛**
