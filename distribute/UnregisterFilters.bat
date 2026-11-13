@echo off
rem 同梱 LAV Filters / MPC Video Renderer の HKCU 登録を解除し、自動登録を無効化します。
rem 別途 LAV Filters / MPC VR をインストーラで導入する場合に実行してください。
if exist "%~dp0PCRPlayer64.exe" "%~dp0PCRPlayer64.exe" /unregister-filters
if exist "%~dp0PCRPlayer.exe"   "%~dp0PCRPlayer.exe"   /unregister-filters
