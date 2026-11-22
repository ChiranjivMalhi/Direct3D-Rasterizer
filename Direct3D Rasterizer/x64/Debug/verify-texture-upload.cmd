@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++20 /MDd /Od /D_DEBUG /DUNICODE /D_UNICODE /I"packages\directxtk12_desktop_win10.2026.5.8.1\include" "Direct3D Rasterizer\x64\Debug\verify-texture-upload.cpp" /Fo"Direct3D Rasterizer\x64\Debug\verify-texture-upload.obj" /Fe"Direct3D Rasterizer\x64\Debug\verify-texture-upload.exe" /link /DEBUG:NONE /LIBPATH:"packages\directxtk12_desktop_win10.2026.5.8.1\native\lib\x64\Debug" DirectXTK12.lib d3d12.lib dxgi.lib dxguid.lib uuid.lib ole32.lib windowscodecs.lib
exit /b %errorlevel%
