# Tiny Shell - IT3070 Operating System Project

This repository contains a Windows Tiny Shell implementation for the Operating System course project.

## Main Features

- Read, parse, and execute commands from an interactive prompt.
- Run foreground commands and wait for them to finish.
- Run background commands by appending `&`.
- Track background processes with PID, command name, and status.
- Manage processes with `list`, `kill <pid>`, `stop <pid>`, and `resume <pid>`.
- Handle Ctrl+C to terminate the current foreground process without closing the shell.
- Run Windows `.bat`/`.cmd` files through `cmd.exe /C`.
- Run simple myShell script files with `run <file>`.
- Provide built-in commands: `help`, `exit`, `date`, `time`, `dir`, `path`, `addpath`, `setpath`.
- Add small OS-related utilities: `cd`, `pwd`, `env`, `setenv`, `unsetenv`, `which`, `history`, `clear`, `sleep`, `reap`.

## Build

Requirements:

- Windows 10/11
- CMake 3.16+
- MinGW-w64 or MSVC

Build with CMake:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Run:

```powershell
.\build\myShell.exe
```

## Quick Demo

Inside `myShell`:

```text
help
date
time
dir
build\ascii_worker.exe 30 &
list
stop <pid>
resume <pid>
kill <pid>
scripts\hello.bat
run scripts\demo.tsh
exit
```

## Documents

- `docs/bao_cao_tiny_shell.md`: project report in Vietnamese.
- `docs/huong_dan_chi_tiet_cho_nguoi_moi.md`: beginner guide explaining system logic, Windows APIs, and source code.
- `docs/phan_tich_sau_analyze_va_de_xuat.md`: post-analysis technical fixes and future enhancement suggestions.
- `docs/kich_ban_demo_cho_thay.md`: guided teacher demo script with explanations.
