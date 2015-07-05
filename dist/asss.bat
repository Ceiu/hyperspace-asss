@echo off

ECHO starting asss...

GOTO START

:START

bin\asss.exe

IF ERRORLEVEL 5 GOTO MODLOAD
IF ERRORLEVEL 4 GOTO MODCONF
IF ERRORLEVEL 3 GOTO OOM
IF ERRORLEVEL 2 GOTO GENERAL
IF ERRORLEVEL 1 GOTO RECYCLE
IF ERRORLEVEL 0 GOTO SHUTDOWN

ECHO unknown exit code: %ERRORLEVEL%.

GOTO END

:SHUTDOWN
ECHO asss exited with shutdown.
GOTO END

:RECYCLE
ECHO asss exited with recycle.
GOTO START

:GENERAL
ECHO asss exited with general error.
GOTO END

:OOM
ECHO asss out of memory. restarting.
GOTO START

:MODCONF
ECHO asss cannot start. bad modules.conf.
GOTO END

:MODLOAD
ECHO asss cannot start. error loading modules.
GOTO END

:END
