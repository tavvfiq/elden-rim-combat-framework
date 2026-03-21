@echo off
setlocal enabledelayedexpansion

set "repoRoot=%~dp0"

rem Remove trailing backslash for consistent Join logic (optional)
if "%repoRoot:~-1%"=="\" set "repoRoot=%repoRoot:~0,-1%"

set "commonlibDir="
set "target=ercf"
set "withTests=0"

for %%A in (%*) do (
  rem Simple arg parsing
  if /I "%%~A"=="-WithTests" set "withTests=1"
)

rem Optional: user can set COMMONLIBSSE_DIR env var or pass it as first arg like:
rem   build_ercf.bat D:\Modding\CommonLibSSE-NG
if not "%~1"=="" (
  set "commonlibDir=%~1"
)

if "%commonlibDir%"=="" (
  rem Default: repo is next to CommonLibSSE-NG
  set "commonlibDir=%repoRoot%\..\CommonLibSSE-NG"
)

if not exist "%commonlibDir%\" (
  echo CommonLibSSE-NG not found: "%commonlibDir%"
  echo Usage:
  echo   build_ercf.bat "D:\Modding\CommonLibSSE-NG" -WithTests
  exit /b 1
)

pushd "%repoRoot%"

echo Configuring xmake with CommonLibSSE-NG: "%commonlibDir%"
xmake f --commonlibsse_dir="%commonlibDir%"
if errorlevel 1 exit /b %errorlevel%

echo Building target: "%target%"
xmake build %target%
if errorlevel 1 exit /b %errorlevel%

if "%withTests%"=="1" (
  echo Building tests target: combat_math_tests
  xmake build combat_math_tests
  if errorlevel 1 exit /b %errorlevel%
)

popd

endlocal

