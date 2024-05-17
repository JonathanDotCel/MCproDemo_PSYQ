
del *.sym
del *.cpe
del *.map
del *.elf
del *.o

call c:\psyq\pspaths.bat

ccpsx -O3 -Xo$80010000 main.c cyblib\OBJ\MCRDPRO.OBJ -omcprotest.cpe,mcprotest.sym,mcprotest.map

cpe2x mcprotest.cpe

