@echo off
rem 同梱 LAV Filters / MPC Video Renderer の HKCU 登録を再有効化します。
rem 別途インストーラ版を導入する場合は先に UnregisterFilters.bat を実行してください。
if exist "%~dp0PCRPlayer64.exe" "%~dp0PCRPlayer64.exe" /register-filters
if exist "%~dp0PCRPlayer.exe"   "%~dp0PCRPlayer.exe"   /register-filters
