@echo off
setlocal

set MSBUILD="D:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
set SLN="D:\Resident evil 1 3d analog\analog3d.sln"
set DLL_SRC="D:\Resident evil 1 3d analog\Release\analog3d.dll"
set DLL_DST="D:\GOG Galaxy\Games\Resident Evil\mod_analog3d\analog3d.dll"

echo [1/2] Building...
%MSBUILD% %SLN% /p:Configuration=Release /p:Platform=x86 /nologo /v:minimal
if %ERRORLEVEL% neq 0 (
    echo BUILD FAILED with error %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
echo BUILD OK

echo [2/2] Deploying...
copy /Y %DLL_SRC% %DLL_DST%
if %ERRORLEVEL% neq 0 (
    echo DEPLOY FAILED
    exit /b %ERRORLEVEL%
)
echo DEPLOYED: %DLL_DST%

endlocal
