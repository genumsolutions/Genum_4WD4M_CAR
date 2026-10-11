# Firmware syntax gate — xtensa-esp-elf-gcc -fsyntax-only over every translation unit.
#
# WHY THIS FILE EXISTS (F-61): the Arduino CI is the real build, but it only runs
# after a push and takes ~4 minutes, and an earlier round wrongly recorded it as
# unreachable ("private, API 404"). It is reachable and green — checked 2026-10-05,
# run 37280923047 — this gate is the local second opinion: it cannot prove
# behaviour, but it DOES prove the sources
# still parse against the real ESP32 core, and it catches a broken edit in seconds
# instead of waiting for CI.
#
# It is NOT a substitute for flashing. F-61 stands: green here is necessary, never
# sufficient.
#
# THE .ino IS IN THE SWEEP, AND IT MUST STAY IN THE SWEEP (U-96 gate break).
# This used to filter `-Filter *.cpp`, which compiled the nine .cpp files and
# reported "SYNTAX GATE PASS: 9 / 9" while the sketch itself did not build. That
# is not a hypothetical: U-96 landed a broken `WRITE_PERI_REG` on a brownout
# symbol that does not exist on core 3.3.x, in Genum_4WD4M_CAR.ino, and this
# gate stayed green through it. A gate that reports success over 9 of the 10
# files that make up the firmware is worse than no gate, because it converts an
# uncompiled file into a trusted one.
#
# The include set below was derived from the real Arduino build layout on this
# machine (core 3.3.12 + esp32-libs 3.3.12 + sketchbook libraries). Three
# orderings matter and are easy to get wrong:
#
#   1. -DESP32 must NOT be defined. Config.h declares
#      `enum class BoardType : uint8_t { ESP32 };` and a numeric ESP32 macro turns
#      that enumerator into a numeric constant, so EVERY file including Config.h
#      fails with a baffling "expected identifier before numeric constant".
#   2. The generated arch headers must precede the FreeRTOS kernel's own copies:
#      freertos/config/xtensa/include (FreeRTOSConfig_arch.h) before
#      freertos/config/include (FreeRTOSConfig.h) before
#      FreeRTOS-Kernel/include (FreeRTOS.h), and the xtensa portmacro.h before
#      the kernel root.
#
#   3. The .ino needs `-x c++`. gcc does not infer a language from the .ino
#      extension (it has none), so without it the driver declines to compile it
#      rather than treating it as C++. It also needs -I <repo> so the sketch's
#      own `#include "Config.h"` resolves; Arduino builds the sketch with the
#      sketch directory on the include path, so this matches the real build.
#
# Usage:  pwsh -File tools/check-firmware-syntax.ps1
#         pwsh -File tools/check-firmware-syntax.ps1 -Files WebServerComm.cpp
#         pwsh -File tools/check-firmware-syntax.ps1 -Files Genum_4WD4M_CAR.ino
# Exit 0 = every TU parses. Non-zero = first errors are printed.
#
# -Files narrows the sweep to named translation units. It exists for
# tools/mutate-gates.mjs, which needs to prove that a static_assert FIRES once per
# broken invariant, and paying 40 s for all ten units to learn something about one
# of them makes that harness too slow to run before every commit. Full sweeps (no
# -Files) still cover everything; this only changes how fast a targeted check is.

param([string[]]$Files = @())

$ErrorActionPreference = "Stop"

$repo   = Split-Path -Parent $PSScriptRoot
$coreV  = "3.3.12"
$libsV  = $coreV
$arduino = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32"
$fw     = Join-Path $arduino "hardware\esp32\$coreV"
$esp32libs = Join-Path $arduino "tools\esp32-libs\$libsV"
$sketchLib = Join-Path $env:USERPROFILE "Documents\Arduino\libraries"

$gcc = Join-Path $arduino "tools\esp-x32\2601\bin\xtensa-esp-elf-gcc.exe"
if (-not (Test-Path $gcc)) { Write-Error "xtensa gcc not found at $gcc"; exit 2 }
if (-not (Test-Path $esp32libs)) { Write-Error "esp32-libs not found at $esp32libs"; exit 2 }

# Arch/generated config first, then component includes shortest-first.
$first = @(
  "$esp32libs\include\freertos\config\include\freertos",
  "$esp32libs\include\freertos\config\xtensa\include",
  "$esp32libs\include\freertos\config\include",
  "$esp32libs\include\xtensa\esp32\include\xtensa",
  "$esp32libs\include\esp_rom\esp32\include",
  "$esp32libs\include\esp_rom\esp32\include\esp32",
  "$esp32libs\include\freertos\FreeRTOS-Kernel\portable\xtensa\include\freertos",
  "$esp32libs\include\freertos\FreeRTOS-Kernel\include",
  "$esp32libs\include\freertos\FreeRTOS-Kernel\include\freertos",
  "$esp32libs\include\freertos\esp_additions\include",
  "$esp32libs\include\freertos\esp_additions\include\freertos",
  "$esp32libs\include\freertos\esp_additions\include\esp_private",
  "$esp32libs\include\esp_hw_support\include",
  "$esp32libs\include\soc\esp32\register",
  "$esp32libs\include\soc\esp32\include\soc",
  "$esp32libs\include\newlib\platform_include",
  "$esp32libs\include\bt\host\bluedroid\api\include\api",
  "$esp32libs\include\bt\host\bluedroid\api\include",
  "$esp32libs\include\http_parser",
  "$esp32libs\include\protocomm\include\common",
  "$esp32libs\include\protocomm\include\security",
  "$esp32libs\include\protocomm\include\crypto",
  "$esp32libs\include\protocomm\include\transports",
  "$esp32libs\include\protocomm\proto-c",
  "$esp32libs\include\espressif__network_provisioning\include",
  "$esp32libs\include\espressif__mdns\include",
  "$esp32libs\qio_qspi\include"
)
# espressif__ extras are pulled in selectively above: the wholesale set drags in
# stacks this sketch does not link (and their own missing transitive headers).
$rest = Get-ChildItem "$esp32libs\include" -Directory -Recurse -Depth 5 -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq 'include' -and $_.FullName -notmatch 'espressif__|newlib\\platform_include\\sys' } |
        Sort-Object Length | ForEach-Object { $_.FullName }

$inc = @("$fw\cores\esp32", "$fw\variants\esp32", "$fw\libraries", "$fw")
$inc += (Get-ChildItem "$fw\libraries" -Directory | ForEach-Object { "$($_.FullName)\src" })
if (Test-Path $sketchLib) {
  $inc += (Get-ChildItem $sketchLib -Directory | ForEach-Object { "$($_.FullName)\src" })
  $inc += (Get-ChildItem $sketchLib -Directory | ForEach-Object { $_.FullName })
}
# The sketch directory, so the .ino's own `#include "Config.h"` resolves. Arduino
# compiles the sketch with this on the include path, so this is not an
# accommodation for the gate — it is what the real build does.
$inc += $repo
$inc += $first + $rest
$inc = $inc | Select-Object -Unique | ForEach-Object { '"' + ($_ -replace '\\', '/') + '"' }

$rsp = Join-Path $env:TEMP "genum-esp32-syntax-gate.rsp"
$flags = @(
  '-fsyntax-only', '-std=gnu++17', '-w',
  '-DARDUINO=10819', '-DARDUINO_ARCH_ESP32', '-DARDUINO_BOARD=ESP32_DEV',
  '-DARDUINO_VARIANT=ESP32', '-DESP32_ARDUINO_ARCH_ESP32', '-DNDEBUG',
  '-DWEBSOCKETS_NETWORK_TYPE=4'   # NETWORK_ESP32; the lib otherwise defaults to W5100
)
$flags += ($inc | ForEach-Object { "-I$_" })
Set-Content -Path $rsp -Value ($flags -join "`n") -Encoding ascii

# EVERY source the Arduino build compiles, not just the .cpp files. The sketch is
# part of the firmware; leaving it out is what let U-96's broken brownout symbol
# through this gate twice.
$sources = Get-ChildItem -Path $repo -Include *.cpp,*.ino -Recurse |
           Where-Object { $_.FullName -notmatch '\\build' }
if (-not $sources) { Write-Error "no .cpp/.ino found under $repo"; exit 2 }

# Coverage floor. The U-96 lesson is that a gate which quietly compiles fewer
# files than it used to is worse than no gate: it reports success over a hole.
# If the .ino ever stops being picked up — a renamed sketch, a moved file, a glob
# typo — this fires instead of the sweep shrinking to 9 and staying green.
$inoCount = @($sources | Where-Object { $_.Extension -eq '.ino' }).Count
if ($inoCount -lt 1) {
  Write-Error "SYNTAX GATE COVERAGE BREAK: no .ino found under $repo. The sketch is not being compiled. Refusing to report a pass over a partial build."
  exit 2
}

if ($Files.Count -gt 0) {
  $wanted = $Files | ForEach-Object { $_.ToLower() }
  $sources = $sources | Where-Object { $wanted -contains $_.Name.ToLower() }
  $missing = $wanted | Where-Object { $_ -notin @($sources | ForEach-Object { $_.Name.ToLower() }) }
  if ($missing) { Write-Error "no such translation unit(s): $($missing -join ', ')"; exit 2 }
}

$failed = @()
foreach ($f in $sources) {
  # gcc has no rule for the .ino extension; -x c++ is what tells it to treat the
  # file as C++ the way the Arduino builder does.
  $lang = if ($f.Extension -eq '.ino') { @('-x', 'c++') } else { @() }
  # U-96-CLASS DEFECT (found 2026-10-10, fixed here): under PowerShell 5.1 the
  # script-level $ErrorActionPreference = "Stop" turns gcc's stderr into a
  # TERMINATING NativeCommandError at this very line - BEFORE the FAIL reporting
  # below could ever run. On a failing TU the gate printed no diagnosis at all
  # and the foreach aborted, so the remaining translation units were never
  # checked: a partial-coverage run with no error named. Exit was 1, so it was
  # not a silent pass, but it was the U-96 class - a gate reporting over a hole.
  # Fix: capture stderr as DATA (2>&1) with EAP locally relaxed, so a broken TU
  # yields error lines in $out instead of killing the loop. The script-wide
  # Stop preference is restored immediately, so every other line still fails
  # fast. Proven both directions: broken BluetoothComm.cpp now prints
  # "FAIL BluetoothComm.cpp" + the gcc errors AND still checks the other 9 TUs;
  # the clean tree still reports PASS 10/10.
  $prevEAP = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $out = & $gcc "@$rsp" @lang $f.FullName 2>&1
  } finally {
    $ErrorActionPreference = $prevEAP
  }
  if ($LASTEXITCODE -ne 0) {
    $failed += $f.Name
    Write-Host "FAIL $($f.Name)"
    ($out | Select-String 'error:' | Select-Object -First 6) | ForEach-Object { Write-Host "     $_" }
  }
}

if ($failed.Count -gt 0) {
  Write-Host ""
  Write-Host "SYNTAX GATE FAILED: $($failed.Count) / $($sources.Count) translation units"
  exit 1
}
Write-Host "SYNTAX GATE PASS: $($sources.Count) / $($sources.Count) translation units parse (core $coreV)"
Write-Host "(F-61: this proves the sources parse. It proves nothing about behaviour.)"
exit 0