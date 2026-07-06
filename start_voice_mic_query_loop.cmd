@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_voice_mic_query_loop.ps1" %*
pause
