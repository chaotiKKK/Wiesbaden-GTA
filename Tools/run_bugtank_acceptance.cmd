@echo off
setlocal
set "ROOT=%~dp0.."
python "%~dp0bugtank_acceptance.py" --run
exit /b %ERRORLEVEL%
