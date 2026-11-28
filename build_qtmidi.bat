call "E:\DEV\VC\Auxiliary\Build\vcvarsall.bat" x64
set PATH=E:\Qt\6.10.2\msvc2022_64\bin;%PATH%
cd qtmidi
if exist .qmake.stash del .qmake.stash
qmake -spec win32-msvc
nmake
nmake install
