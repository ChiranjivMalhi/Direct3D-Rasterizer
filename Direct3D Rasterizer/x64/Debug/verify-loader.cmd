@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cl /nologo /DUNICODE /D_UNICODE /EHsc /std:c++20 "Direct3D Rasterizer\x64\Debug\verify-loader.cpp" /Fo"Direct3D Rasterizer\x64\Debug\verify-loader.obj" /Fe"Direct3D Rasterizer\x64\Debug\verify-loader.exe" /link /DEBUG:NONE user32.lib
if errorlevel 1 exit /b %errorlevel%
"Direct3D Rasterizer\x64\Debug\verify-loader.exe"
exit /b %errorlevel%


