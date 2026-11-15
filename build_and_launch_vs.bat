@echo off
echo [1/4] Configuration Qt 6 et Visual Studio x64...
set PATH=E:\Qt\6.10.2\msvc2022_64\bin;%PATH%
call "E:\DEV\VC\Auxiliary\Build\vcvarsall.bat" x64

echo [2/4] Nettoyage de logviewer (sans BOM) et generation via qmake...
powershell -Command "$utf8NoBom = New-Object System.Text.UTF8Encoding $false; Get-ChildItem -Recurse -Include '*.pro','*.pri' | ForEach-Object { $lines = (Get-Content $_.FullName) -replace '^(\s*[^#].*logviewer.*)$', '# $1'; [System.IO.File]::WriteAllLines($_.FullName, $lines, $utf8NoBom) }"
qmake -tp vc -r -spec win32-msvc entropypianotuner.pro

echo [3/4] Securisation automatique des fichiers .vcxproj generes...
powershell -Command "Get-ChildItem -Recurse -Filter '*.vcxproj' | ForEach-Object { $lines = Get-Content $_.FullName; $seen = @{}; $clean = foreach ($line in $lines) { if ($line -match '<None Include=\".*\.xml\"') { if (-not $seen.ContainsKey($line)) { $seen[$line] = $true; $line } } else { $line } }; $clean | Set-Content $_.FullName -Encoding UTF8 }"

echo [4/4] Ouverture de la solution dans Visual Studio...
start "" "entropypianotuner.sln"
pause