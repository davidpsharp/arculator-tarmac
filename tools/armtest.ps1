<#
.SYNOPSIS
  Run !ARMTest inside Arculator over HostCmd and collect the report.

.DESCRIPTION
  Starts arculator.exe with the given configuration (unless something is
  already listening on the HostCmd port), waits for RISC OS to reach the
  desktop, runs HostFS::HostFS.$.!ARMTest with ARMTest$NoWait set, copies
  the report out of the HostFS root and prints the PASS/FAIL/OBSERVE line.
  The exit code is the FAIL count (0 = clean), or 100+ for harness failures.

  Requires perl (Git for Windows or MSYS2) for tools/arc-run.pl.

.EXAMPLE
  tools\armtest.ps1 -RunDir C:\arculator-run -Config A5000 -Out reports\ARMReport-stock.txt -Quit
#>
param(
	[string]$RunDir = (Split-Path -Parent $PSScriptRoot),
	[string]$Config = "A5000",
	[int]$Port = 15600,
	[string]$Out = "",
	[int]$BootTimeout = 120,
	[switch]$Quit
)
$ErrorActionPreference = "Stop"
$arcrun = Join-Path $PSScriptRoot "arc-run.pl"
# perl is usually not on the Windows PATH; look where Git for Windows and MSYS2 put it.
$perl = (Get-Command perl -ErrorAction SilentlyContinue).Source
if (-not $perl) {
	$perl = @("$env:ProgramFiles\Git\usr\bin\perl.exe", "C:\msys64\usr\bin\perl.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $perl) { Write-Error "perl not found (install Git for Windows or MSYS2)"; exit 100 }
$exe = Join-Path $RunDir "arculator.exe"
$hostfs = Join-Path $RunDir "hostfs"
if (-not (Test-Path $exe)) { Write-Error "no arculator.exe in $RunDir"; exit 101 }
if (-not (Test-Path (Join-Path $hostfs "!ARMTest"))) { Write-Error "no !ARMTest in $hostfs"; exit 102 }

function ArcRun([string]$cmd, [int]$timeout = 120) {
	$global:arcOut = & $perl $arcrun --quiet --port $Port --timeout $timeout -- $cmd 2>&1
	return $LASTEXITCODE
}

$launched = $null
$listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if (-not $listening) {
	Write-Host "Starting $exe $Config"
	$launched = Start-Process -FilePath $exe -ArgumentList $Config -WorkingDirectory $RunDir -PassThru
	& $perl $arcrun --wait --port $Port --timeout $BootTimeout 2>$null
	if ($LASTEXITCODE -ne 0) { Write-Error "HostCmd port $Port never answered"; exit 103 }
}

# Wait for the desktop: !ARMTest's !Run uses WimpSlot, which needs a task.
$deadline = (Get-Date).AddSeconds($BootTimeout)
do {
	$rc = ArcRun 'Echo <Wimp$State>' 10
	$state = ($arcOut | Where-Object { "$_".Trim() -ne "" } | Select-Object -Last 1)
	if ($rc -eq 0 -and $state -match 'desktop') { break }
	Start-Sleep -Seconds 1
} while ((Get-Date) -lt $deadline)
if ($state -notmatch 'desktop') { Write-Error "RISC OS did not reach the desktop (Wimp`$State='$state')"; exit 104 }

[void](ArcRun 'Set ARMTest$NoWait 1')
$report = Join-Path $hostfs "ARMReport"
if (Test-Path $report) { Remove-Item $report }
$t0 = Get-Date
$rc = ArcRun 'Run HostFS::HostFS.$.!ARMTest' 300
$elapsed = (Get-Date) - $t0
$arcOut | ForEach-Object { Write-Host $_ }
if (-not (Test-Path $report)) { Write-Error "!ARMTest produced no report (rc=$rc)"; exit 105 }

# The report ends with "=== Summary ===" then PASS / FAIL / OBSERVE on separate lines.
$text = Get-Content $report -Raw
$summary = [regex]::Match($text, '=== Summary ===\s+PASS\s+(\d+)\s+FAIL\s+(\d+)\s+OBSERVE\s+(\d+)')
if ($Out) {
	$dir = Split-Path -Parent $Out
	if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
	Copy-Item $report $Out -Force
	Write-Host "Report saved to $Out"
}
if ($Quit -and $launched) { Stop-Process -Id $launched.Id -Force }

if (-not $summary.Success) { Write-Error "no summary in report"; exit 106 }
$fails = [int]$summary.Groups[2].Value
Write-Host ("PASS {0}  FAIL {1}  OBSERVE {2}  ({3:n1} s)" -f $summary.Groups[1].Value, $fails, $summary.Groups[3].Value, $elapsed.TotalSeconds)
exit $fails
