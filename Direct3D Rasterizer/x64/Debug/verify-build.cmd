@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "Direct3D Rasterizer\Direct3D Rasterizer.vcxproj" /p:Configuration=Debug /p:Platform=x64 /verbosity:minimal /nologo
exit /b %errorlevel%
