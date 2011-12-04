@echo off
set BINDIR = .\
set CLSCAN=%BINDIR%clscan.exe
set CLMERGE=%BINDIR%clmerge.exe
set CLEXPORT=%BINDIR%clexport.exe
set TMPDIR=.\Tmp\
set SOURCEDIR=.\
set INCDIR=..\inc

@echo on
%CLSCAN% %SOURCEDIR%test1.h -output %TMPDIR%test1.csv -ast_log %TMPDIR%test1_astlog.txt -spec_log %TMPDIR%test1_speclog.txt -i "%INCDIR%"
%CLMERGE% %TMPDIR%database.csv %TMPDIR%test1.csv
%CLEXPORT% %TMPDIR%database.csv -cpp %TMPDIR%database.cppbin -cpp_log %TMPDIR%database_cpplog.txt

rem clscan.exe test1.h -output .\Tmp\clscan_test1.csv -ast_log .\Tmp\clscan_test1_astlog.txt -spec_log .\Tmp\clscan_test1_speclog.txt -i "..\inc"
