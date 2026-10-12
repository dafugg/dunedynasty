@ECHO OFF
REM Medium: Am5x86-133, Pentium 75-133. 640x480 VESA, VLB or PCI video.
REM 8 sounds at once, fast mixing.
DUNEDYN.EXE -res 640x480 -voices 8 -quality 0 %1 %2 %3 %4 %5 %6
