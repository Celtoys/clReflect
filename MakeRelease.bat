@echo off

md release
md release\bin

copy README release
copy LICENSE release
copy build\bin\release\clscan.exe* release\bin
copy build\bin\release\clmerge.exe* release\bin
copy build\bin\release\clexport.exe* release\bin

xcopy inc\* release\inc /s /y /i
xcopy src\clReflectCpp\*.cpp release\src /s /y /i
xcopy src\clReflectCpp\*.h release\src /s /y /i
