@echo off
setlocal
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
    echo Run this script from the x64 Native Tools Command Prompt for Visual Studio.
    exit /b 1
)

set "DESCENT_SOURCE=%~dp0"
set "DESCENT_SDK=%~dp0..\PluginSDK\SDK-0.4.0\include"
if not "%~1"=="" set "DESCENT_SDK=%~1\include"
if not exist "%DESCENT_SDK%\D2RLPlugin\api.h" (
    echo Pass the root directory of D2RLoader PluginSDK 0.4.0 as the first argument.
    exit /b 1
)
set "DESCENT_BUILD=%~dp0..\.build\endgame-descent-1.2.3"
if not exist "%DESCENT_BUILD%" mkdir "%DESCENT_BUILD%"

cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /I"%DESCENT_SDK%" /c "%DESCENT_SOURCE%src\plugin.cpp" /Fo"%DESCENT_BUILD%\plugin.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /I"%DESCENT_SDK%" /c "%DESCENT_SOURCE%src\boss_room.cpp" /Fo"%DESCENT_BUILD%\boss_room.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /c "%DESCENT_SOURCE%src\checkpoint_text.cpp" /Fo"%DESCENT_BUILD%\checkpoint_text.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /I"%DESCENT_SDK%" /c "%DESCENT_SOURCE%src\dungeon_layout.cpp" /Fo"%DESCENT_BUILD%\dungeon_layout.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /I"%DESCENT_SDK%" /c "%DESCENT_SOURCE%src\dungeon_modifiers.cpp" /Fo"%DESCENT_BUILD%\dungeon_modifiers.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /I"%DESCENT_SDK%" /c "%DESCENT_SOURCE%src\dungeon_loot.cpp" /Fo"%DESCENT_BUILD%\dungeon_loot.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /I"%DESCENT_SDK%" /c "%DESCENT_SOURCE%src\dungeon_entrance.cpp" /Fo"%DESCENT_BUILD%\dungeon_entrance.obj"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /utf-8 /O2 /MT /DNDEBUG /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I"%DESCENT_SDK%" /I"%DESCENT_SOURCE%third_party" /c "%DESCENT_SOURCE%src\installer.cpp" /Fo"%DESCENT_BUILD%\installer.obj"
if errorlevel 1 exit /b %errorlevel%
rc.exe /nologo /I"%DESCENT_SDK%" /I"%DESCENT_SOURCE%src" /fo"%DESCENT_BUILD%\plugin.res" "%DESCENT_SOURCE%src\plugin.rc"
if errorlevel 1 exit /b %errorlevel%
link.exe /nologo /DLL /INCREMENTAL:NO /Brepro /MACHINE:X64 /OUT:"%DESCENT_BUILD%\d2rl-endgame-descent.dll" "%DESCENT_BUILD%\plugin.obj" "%DESCENT_BUILD%\boss_room.obj" "%DESCENT_BUILD%\checkpoint_text.obj" "%DESCENT_BUILD%\dungeon_layout.obj" "%DESCENT_BUILD%\dungeon_modifiers.obj" "%DESCENT_BUILD%\dungeon_loot.obj" "%DESCENT_BUILD%\dungeon_entrance.obj" "%DESCENT_BUILD%\installer.obj" "%DESCENT_BUILD%\plugin.res" bcrypt.lib
exit /b %errorlevel%
