@echo off
title myShell Counter Demo
echo Counter process started.
for /L %%i in (1,1,20) do (
    echo Counter: %%i
    ping -n 2 127.0.0.1 > nul
)
echo Counter process finished.
