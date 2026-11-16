@echo off
rem Puts the client on the machine without a person at the keyboard.
rem
rem The package of the client is installed with msiexec in the quiet mode: the windows of
rem the installer do not open, and the license of the program is taken as accepted.
rem
rem Usage:
rem     deploy-client.cmd <package.msi>

setlocal

set "package=%~1"

if "%package%"=="" (
    echo The package of the client is needed: deploy-client.cmd ^<package.msi^>
    exit /b 1
)

if not exist "%package%" (
    echo No such file: %package%
    exit /b 1
)

rem The installation writes to Program Files and takes the previous version away, so it needs
rem the rights of an administrator. A silent installation cannot ask for them: the window of
rem the request is not shown in the quiet mode.
net session > nul 2>&1
if errorlevel 1 (
    echo The rights of an administrator are needed. Run it from a command prompt as an administrator.
    exit /b 1
)

echo Installing %package%...
msiexec /i "%package%" /qn /norestart

rem 3010 means the machine must be restarted, 1641 means it was already started: neither
rem of them is a failure.
if errorlevel 1641 if not errorlevel 1642 goto done
if errorlevel 3010 if not errorlevel 3011 goto done
if errorlevel 1 (
    echo The installation failed.
    exit /b 1
)

:done
echo The client is installed.
endlocal
