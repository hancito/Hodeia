$ErrorActionPreference = "Continue"
$lib = "C:\Users\garco\Documents\Developpement\Hodeia\vcpkg_installed\x64-windows\lib\libmupdf.lib"
$dumpbin = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\dumpbin.exe"

Write-Host ("lib: " + (Get-Item $lib).LastWriteTime + "  " + (Get-Item $lib).Length + " bytes")

$syms = & $dumpbin /symbols $lib 2>$null
Write-Host ("total lines: " + $syms.Count)

# Search broadly for the base names (decorated or not)
Write-Host ""
Write-Host "=== ALL lines containing 'table_hunt' ==="
$syms | Select-String -Pattern "table_hunt" | ForEach-Object { $_.Line }

Write-Host ""
Write-Host "=== ALL lines containing 'block_iterator' ==="
$syms | Select-String -Pattern "block_iterator" | ForEach-Object { $_.Line }

# Save full dump for inspection
$out = "C:\Users\garco\Documents\Developpement\Hodeia\.qwen\tmp\libmupdf-symbols.txt"
$syms | Set-Content $out
Write-Host ""
Write-Host ("full dump saved to: " + $out + "  (" + $syms.Count + " lines)")
