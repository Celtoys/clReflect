@echo off

rmdir /s /q release
md release
md release\bin

copy README.md release
copy LICENSE release
copy AUTHORS release
copy build\bin\release\clscan.exe* release\bin
copy build\bin\release\clmerge.exe* release\bin
copy build\bin\release\clexport.exe* release\bin

xcopy inc\* release\inc /s /y /i
xcopy src\clReflectCpp\*.cpp release\src\clReflectCpp /s /y /i
xcopy src\clReflectCpp\*.h release\src\clReflectCpp /s /y /i
xcopy src\clReflectUtil\*.cpp release\src\clReflectUtil /s /y /i
xcopy src\clReflectUtil\*.h release\src\clReflectUtil /s /y /i
