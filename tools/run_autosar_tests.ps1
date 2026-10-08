$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $env:TEMP 'ra8d1_autosar_tests.exe'
$gcc = 'C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\gcc.exe'
if (-not (Test-Path $gcc)) { throw "GCC not found: $gcc" }
& $gcc -std=c99 -Wall -Wextra -Werror -pedantic `
    (Join-Path $PSScriptRoot 'autosar_tests.c') `
    (Join-Path $root 'src\app\autosar_boot.c') `
    (Join-Path $root 'src\app\autosar_can_tp.c') `
    (Join-Path $root 'src\app\autosar_dcm.c') `
    (Join-Path $root 'bootloader\src\autosar_bootloader.c') `
    (Join-Path $root 'bootloader\src\autosar_bootloader_entry.c') `
    (Join-Path $root 'bootloader\src\autosar_secure_image.c') `
    (Join-Path $root 'bootloader\src\autosar_boot_update.c') `
    (Join-Path $root 'bootloader\src\autosar_boot_uds_update.c') `
    (Join-Path $root 'bootloader\src\autosar_boot_flash_fsp.c') `
    (Join-Path $root 'bootloader\src\autosar_boot_metadata_fsp.c') `
    (Join-Path $root 'bootloader\src\autosar_boot_rsip.c') `
    -o $out
if ($LASTEXITCODE -ne 0) { throw "AUTOSAR host compile failed" }
& $out
if ($LASTEXITCODE -ne 0) { throw "AUTOSAR host tests failed" }
Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
