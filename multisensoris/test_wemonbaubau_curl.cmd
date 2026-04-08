@echo off
setlocal EnableExtensions

REM =============================================================
REM Curl test script for wemonbaubau.php (Windows CMD)
REM Usage:
REM   %~nx0 weather
REM   %~nx0 weather_lidar_batch
REM   %~nx0 flow
REM   %~nx0 direction
REM
REM Optional overrides (set before running):
REM   set WBB_SERVER=31.97.66.191
REM   set WBB_KPATH=/Wemon_BauBau/wemonbaubau.php
REM   set WBB_AUTH=YOUR_HTTP_USER:YOUR_HTTP_PASSWORD
REM =============================================================

if not defined WBB_SERVER set "WBB_SERVER=31.97.66.191"
if not defined WBB_KPATH set "WBB_KPATH=/Wemon_BauBau/wemonbaubau.php"
if not defined WBB_AUTH set "WBB_AUTH=YOUR_HTTP_USER:YOUR_HTTP_PASSWORD"
if not defined WBB_DEBUG set "WBB_DEBUG=0"

set "TYPE=%~1"
if "%TYPE%"=="" goto :help

if /I "%TYPE%"=="weather" goto :weather
if /I "%TYPE%"=="weather_lidar_batch" goto :weather_lidar_batch
if /I "%TYPE%"=="flow" goto :flow
if /I "%TYPE%"=="direction" goto :direction

goto :help

:weather
REM Weather payload (must include all params expected by PHP)
set "URL=http://%WBB_SERVER%%WBB_KPATH%?windir=90&windavg=1.23&windmax=2.34&rain1h=0.00&rain24h=0.10&suhu=30.50&humidity=70.20&pressure=1012.34&distance=123&waterheight=45&waveheight=5"
goto :run

:weather_lidar_batch
REM Weather + Lidar distance batch payload (POST JSON)
set "URL=http://%WBB_SERVER%%WBB_KPATH%"
set "JSON={\"type\":\"weather_lidar_distance_batch\",\"sample_interval_ms\":100,\"windir\":90,\"windavg\":1.23,\"windmax\":2.34,\"rain1h\":0.00,\"rain24h\":0.10,\"suhu\":30.50,\"humidity\":70.20,\"pressure\":1012.34,\"lidar_distance_cm\":[123,124,125,126,127,128,129,130,131,132]}"
goto :run_post

:flow
REM Water Flow minimal payload: enc_rpm only
set "URL=http://%WBB_SERVER%%WBB_KPATH%?enc_rpm=12.34"
goto :run

:direction
REM Water Direction minimal payload: ang_dir only (1=CW, -1=CCW, 0=STOP)
set "URL=http://%WBB_SERVER%%WBB_KPATH%?ang_dir=1"
goto :run

:run
if "%WBB_DEBUG%"=="1" set "URL=%URL%&debug=1"
echo.
echo === Request ===
echo URL: "%URL%"
echo Auth: %WBB_AUTH%
echo.
echo === Response ===
curl -i --max-time 10 -u "%WBB_AUTH%" "%URL%"
echo.
echo ExitCode: %ERRORLEVEL%
echo.
endlocal
exit /b %ERRORLEVEL%

:run_post
if "%WBB_DEBUG%"=="1" set "URL=%URL%?debug=1"
echo.
echo === Request ===
echo URL: "%URL%"
echo Auth: %WBB_AUTH%
echo JSON: %JSON%
echo.
echo === Response ===
curl -i --max-time 10 -u "%WBB_AUTH%" -H "Content-Type: application/json" --data "%JSON%" "%URL%"
echo.
echo ExitCode: %ERRORLEVEL%
echo.
endlocal
exit /b %ERRORLEVEL%

:help
echo.
echo Usage:
echo   %~nx0 weather
echo   %~nx0 weather_lidar_batch
echo   %~nx0 flow
echo   %~nx0 direction
echo.
echo Current config:
echo   WBB_SERVER=%WBB_SERVER%
echo   WBB_KPATH=%WBB_KPATH%
echo   WBB_AUTH=%WBB_AUTH%
echo   WBB_DEBUG=%WBB_DEBUG%
echo.
endlocal
exit /b 2


@REM .\test_wemonbaubau_curl.cmd flow
@REM .\test_wemonbaubau_curl.cmd direction
@REM .\test_wemonbaubau_curl.cmd weather