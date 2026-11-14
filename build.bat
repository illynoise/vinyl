@echo off
rem Vinyl Studio - Base Jase Plugins: configure, build Release, package (Windows, VST3).
rem Run from a "x64 Native Tools Command Prompt for VS 2022". Installing into
rem C:\Program Files\Common Files\VST3 after the build needs an elevated prompt;
rem set NO_INSTALL=1 to skip that copy.
setlocal enabledelayedexpansion
cd /d "%~dp0"
for /f "tokens=4 delims=() " %%v in ('findstr /b /c:"project(VinylStudio VERSION" CMakeLists.txt') do set VERSION=%%v
set NAME=BaseJasePlugins-VinylStudio-%VERSION%
set DIST=dist\%NAME%
set COPY=ON
if defined NO_INSTALL set COPY=OFF

cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DVS_COPY_AFTER_BUILD=%COPY% || exit /b 1
cmake --build build --config Release --parallel || exit /b 1

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%\VST3"
for %%v in (VinylStudio VinylStudioMono VinylStudioLite VinylStudioLiteMono) do (
  for /d %%p in ("build\%%v_artefacts\Release\VST3\*.vst3") do xcopy /e /i /q /y "%%p" "%DIST%\VST3\%%~nxp" >nul
  for %%e in ("build\%%v_artefacts\Release\Standalone\*.exe") do copy /y "%%e" "%DIST%\" >nul
)
if not defined SKIP_TESTS build\vs_tests_artefacts\Release\vs_tests.exe test-output > "%DIST%\test-report.txt"
copy /y README.md "%DIST%\" >nul
copy /y docs\UserGuide.md "%DIST%\" >nul
xcopy /e /i /q /y docs\images "%DIST%\images" >nul

if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "PATH=%PATH%;%ProgramFiles(x86)%\Inno Setup 6"
where iscc >nul 2>nul
if %errorlevel%==0 (
  iscc /Qp /DAppVersion=%VERSION% /DSourceDir="%CD%\%DIST%\VST3" /DOutDir="%CD%\%DIST%" installer\windows\VinylStudio.iss || exit /b 1
) else (
  echo Inno Setup not found - writing a zip instead.
)
powershell -NoProfile -Command "Compress-Archive -Force -Path '%DIST%' -DestinationPath 'dist\%NAME%.zip'"
echo Done: %DIST%
