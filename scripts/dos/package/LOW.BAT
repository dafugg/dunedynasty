@ECHO OFF
REM Low: 486 DX2/DX4/5x86. Dune II's 320x240, any VGA card.
REM 4 sounds at once, fastest mixing.
DUNEDYN.EXE -res 320x240 -voices 4 -quality 0 %1 %2 %3 %4 %5 %6
