@echo off
title C2-Vita Patcher
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0data\patcher.ps1" %*
if errorlevel 1 pause
