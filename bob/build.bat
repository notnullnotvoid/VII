@echo off
pushd "%~dp0"
	del bob.exe
	clang -std=c++11 -D_CRT_SECURE_NO_DEPRECATE -I../lib -Wall -Os -o bob.exe bob.cpp
	set exit_status=%errorlevel%
	rem bob.exe -r .. -t base.ninja -o bob/out.ninja -v
popd
exit /b %exit_status%
