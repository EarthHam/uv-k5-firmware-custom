@echo off
where bash >nul 2>nul
if errorlevel 1 (
  echo Bash is required. Run package-release.sh from Git Bash or WSL.
  exit /b 2
)
bash "%~dp0package-release.sh" %*
