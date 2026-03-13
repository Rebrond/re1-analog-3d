@echo off
setlocal

set VSDEV="D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set SRC="D:\Resident evil 1 3d analog\dllmain.cpp"
set OBJ="D:\Resident evil 1 3d analog\Release\clean_dllmain.obj"
set PDB="D:\Resident evil 1 3d analog\Release\clean_build.pdb"
set OUT="D:\Resident evil 1 3d analog\Release\analog3d.dll"
set LIB="D:\Resident evil 1 3d analog\Release\analog3d.lib"
set DST="D:\GOG Galaxy\Games\Resident Evil\mod_analog3d\analog3d.dll"

call %VSDEV% -arch=x86 -host_arch=x86 >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo VsDevCmd failed
    exit /b 1
)

echo [1/3] Compiling...
cl /nologo /c /MT /O2 /Oy- /GL /Gd /TP /FC /W3 /D WIN32 /D NDEBUG /D _WINDOWS /D _USRDLL /D _WINDLL /D _MBCS /Fo%OBJ% /Fd%PDB% %SRC%
if %ERRORLEVEL% neq 0 (
    echo COMPILE FAILED
    exit /b 1
)
echo COMPILE OK

echo [2/3] Linking...
link /NOLOGO /DLL /OUT:%OUT% /IMPLIB:%LIB% /PDB:%PDB% /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MACHINE:X86 /LTCG %OBJ% kernel32.lib user32.lib
if %ERRORLEVEL% neq 0 (
    echo LINK FAILED
    exit /b 1
)
echo LINK OK

echo [3/3] Deploying...
copy /Y %OUT% %DST%
if %ERRORLEVEL% neq 0 (
    echo DEPLOY FAILED
    exit /b 1
)
echo DEPLOYED OK

endlocal
