# stability-run-case.ps1 -- one case of the T4 stability/release matrix.
#
# Runs tf2_demo_native.exe under observation for a fixed wall-clock window,
# samples the process (CPU, working set, private bytes, threads), probes the
# window for responsiveness through SendMessageTimeout, watches the metrics
# CSV for render stalls, then asks the window to close and records how the
# process actually left. Everything a later gate needs to judge the case is
# written into the case directory as a named file, never inferred.
#
# WHY POWERSHELL AND NOT BASH
#   The readings the task asks for -- CPU time, working set, private bytes,
#   thread count, a window's responsiveness, the process exit code, a residual
#   process check -- are Win32 facts. The bash scripts that make up the rest of
#   the gate keep their role: this file is the instrument, the gate is still
#   stability-release-check.sh.
#
# FILES WRITTEN (case directory)
#   command.txt         the exact exe + arguments, one per line
#   env.txt             TF2_NATIVE_TRACE and other environment decisions
#   stdout.txt          the process's stdout
#   stderr.txt          the process's stderr
#   trace.txt           the app's own trace (TF2_NATIVE_TRACE)
#   metrics.csv         the app's metrics stream (--metrics-file)
#   sampler.csv         one row per sample: t, cpu_s, ws_mb, priv_mb, threads,
#                       resp (1 responsive / 0 hung / empty no-window), frames, tick
#   exit-code.txt       the process exit code
#   process-state.txt   NORMAL_EXIT | EARLY_EXIT | CRASH | KILLED_AT_END | HARNESS_ERROR
#   verdict.txt         COMPLETED | EARLY_EXIT | CRASH | KILLED_AT_END | HARNESS_ERROR
#   crash.txt           present only when a crash marker was observed
#   title-first.txt     the window title, read back through the API
#   title-last.txt
#   leftovers.txt       residual processes and leftover lock/tmp files
#   readings.json       the machine-readable summary the gate asserts on
#
# USAGE
#   powershell -ExecutionPolicy Bypass -NoProfile -File stability-run-case.ps1 `
#     -Case <name> -RunDir <dir> -Seconds <n> [-Demo <path>] [-Hidden] `
#     [-AudioDisabled] [-AudioDevice N] [-SampleSeconds 2] [-NoClose]
#
# Exit: 0 = the observation completed (the product verdict lives in verdict.txt
#        and readings.json); 1 = the harness itself failed.
#
# A NOTE ON THE WINDOW RESPONSIVENESS READING
#   The app's window exists before its asset load finishes, and a window whose
#   owner thread is inside that long load cannot answer a send in the sampler's
#   1.5 s budget. resp=0 is therefore only meaningful on rows whose frames
#   counter is non-negative -- the gate applies that filter; the raw rows are
#   kept so nothing is silently discarded.
#
# A NOTE ON OutputDebugString
#   The product's argument-rejection paths write their named reason to the
#   debug channel, not to stdout. Capturing that channel needs a debugger
#   attached to the child (DebugActiveProcess), and PowerShell 5.1 marshals
#   WaitForDebugEvent's [ref] struct with a NullReferenceException, so this
#   harness does not claim to capture it. For an argument-rejection case the
#   named evidence is therefore the exit code the product chose (13, set at
#   native/src/main.cpp:1060-1063) plus this note, and the empty captured
#   stdout/stderr is recorded rather than filled in.

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Case,
  [Parameter(Mandatory = $true)][string]$RunDir,
  [Parameter(Mandatory = $true)][int]$Seconds,
  [string]$Demo,
  [switch]$Hidden,
  [switch]$AudioDisabled,
  [int]$AudioDevice = -1,
  [int]$SampleSeconds = 2,
  [int]$CloseGraceSeconds = 30,
  [switch]$NoClose,
  [string]$Exe,
  [string]$TfRoot,
  [string]$WindowClass = 'TF2DemoNativeRenderer'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# The child process runs with the exe's directory as its working directory, so
# a relative RunDir would place trace and metrics paths under the build tree.
# Every evidence path is absolute from here down.
$RunDir = [System.IO.Path]::GetFullPath($RunDir)

if ([string]::IsNullOrEmpty($Exe)) {
  $Exe = (Resolve-Path (Join-Path $PSScriptRoot 'native\build-nmake\tf2_demo_native.exe')).Path
}
if ([string]::IsNullOrEmpty($TfRoot)) {
  $TfRoot = 'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf'
}
if (-not (Test-Path -LiteralPath $Exe)) { Write-Output "HARNESS_ERROR missing exe: $Exe"; exit 1 }
# The TF root is deliberately NOT checked for existence: "the tf root is
# missing / moved / unreadable" is exactly what several cases must be able to
# start the program with, and a harness that refuses to start them would
# replace the reading with its own refusal.

# ---------------------------------------------------------------- evidence dir
$dir = $RunDir
New-Item -ItemType Directory -Path $dir -Force | Out-Null
# Truncate, never reuse: a leftover file from an earlier attempt must not be
# able to satisfy a later gate. Deleting outright is avoided because the OS's
# safe-delete wrapper can refuse a bulk remove; per-file removal is reliable.
foreach ($name in @('command.txt','env.txt','stdout.txt','stderr.txt','trace.txt','metrics.csv',
                    'sampler.csv','exit-code.txt','process-state.txt','verdict.txt','crash.txt',
                    'title-first.txt','title-last.txt','leftovers.txt','readings.json')) {
  $p = Join-Path $dir $name
  if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
}
$env:TF2_NATIVE_TRACE = (Join-Path $dir 'trace.txt')
$settingsFile = Join-Path $dir 'settings.ini'

# ---------------------------------------------------------------- arguments
$argList = New-Object System.Collections.Generic.List[string]
$argList.Add('--tf-root'); $argList.Add($TfRoot)
if ($Demo) { $argList.Add('--demo'); $argList.Add($Demo) }
if ($Hidden) { $argList.Add('--hidden-window') }
if ($AudioDisabled) { $argList.Add('--audio-disabled') }
if ($AudioDevice -ge 0) { $argList.Add('--audio-device'); $argList.Add("$AudioDevice") }
$argList.Add('--metrics-file'); $argList.Add((Join-Path $dir 'metrics.csv'))
$argList.Add('--settings-file'); $argList.Add($settingsFile)

# The command file is written in a form a human can re-run; inner quotes are
# escaped so the line survives a copy-paste into a shell.
$cmdLines = New-Object System.Collections.Generic.List[string]
$cmdLines.Add("`"$Exe`"")
foreach ($a in $argList) {
  if ($a -match '\s') { $cmdLines.Add("`"$a`"") } else { $cmdLines.Add($a) }
}
Set-Content -LiteralPath (Join-Path $dir 'command.txt') -Value $cmdLines -Encoding UTF8
Set-Content -LiteralPath (Join-Path $dir 'env.txt') -Value @(
  "TF2_NATIVE_TRACE=$env:TF2_NATIVE_TRACE",
  "settings_file=$settingsFile",
  "note=the product's argument-rejection reason is written to the debug channel (OutputDebugString), which this harness does not capture; the exit code is the record"
) -Encoding UTF8

# ---------------------------------------------------------------- Win32 layer
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class T4Win {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindowW(string cls, string win);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")]
  public static extern bool EnumWindows(EnumWindowsProc proc, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint msg, IntPtr w, IntPtr l,
                                                  uint flags, uint timeout, out IntPtr result);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")]
  public static extern uint GetWindowThreadProcessId(IntPtr h, out uint processId);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowTextW(IntPtr h, StringBuilder text, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassNameW(IntPtr h, StringBuilder name, int max);
}
'@ -ReferencedAssemblies System.Text.Encoding

# Process.MainWindowHandle only reports *visible* top-level windows, and a
# --hidden-window run is exactly SW_HIDE, so the window is found through
# EnumWindows by class name and verified against the child's own pid: a gate
# that matched any window of the class could find another task's run.
$script:foundWindow = [IntPtr]::Zero

function Get-OurWindow {
  param([int]$ProcessId)
  $script:foundWindow = [IntPtr]::Zero
  $callback = [T4Win+EnumWindowsProc]{
    param($h, $l)
    $pidValue = 0
    [void][T4Win]::GetWindowThreadProcessId($h, [ref]$pidValue)
    if ($pidValue -ne $ProcessId) { return $true }
    $clsName = New-Object System.Text.StringBuilder 256
    [void][T4Win]::GetClassNameW($h, $clsName, 256)
    if ($clsName.ToString() -ne $WindowClass) { return $true }
    $script:foundWindow = $h
    return $false
  }
  [void][T4Win]::EnumWindows($callback, [IntPtr]::Zero)
  return $script:foundWindow
}

function Get-WindowTitle {
  param([IntPtr]$Handle)
  if ($Handle -eq [IntPtr]::Zero) { return '' }
  $title = New-Object System.Text.StringBuilder 2048
  [void][T4Win]::GetWindowTextW($Handle, $title, 2048)
  return $title.ToString()
}

function Test-WindowResponds {
  param([IntPtr]$Handle)
  if ($Handle -eq [IntPtr]::Zero) { return $null }
  # The Win32 constants are written as values, not read through the Add-Type
  # constant class: inside this function's scope PowerShell resolves
  # [T4Const]::SMTO_ABORTIFHUNG as a property access and throws
  # PropertyNotFoundException, which the try/catch below would silently turn
  # into "responsive" for every sample. WM_NULL=0x0000, SMTO_ABORTIFHUNG=0x0002,
  # SMTO_NORMAL=0x0000 -- these are fixed API values.
  try {
    $msg = [uint32]0
    $flags = [uint32]2
    $timeout = [uint32]1500
    $res = [IntPtr]::Zero
    $ok = [T4Win]::SendMessageTimeoutW($Handle, $msg, [IntPtr]::Zero, [IntPtr]::Zero,
      $flags, $timeout, [ref]$res)
    if ($ok -ne [IntPtr]::Zero) { return 1 }
    return 0
  } catch {
    return 0
  }
}

function Send-WindowClose {
  # The close is posted, not sent: SendMessageTimeout to a window whose thread
  # is inside a long load only returns when the load finishes, which would
  # manufacture a "window hung" reading out of a load. A post puts WM_CLOSE on
  # the queue, where it is picked up by the message loop exactly as a click on
  # the X would be.
  param([IntPtr]$Handle)
  if ($Handle -eq [IntPtr]::Zero) { return $false }
  try {
    $msg = [uint32]0x0010 # WM_CLOSE, written as a value for the reason Test-WindowResponds documents
    return [T4Win]::PostMessageW($Handle, $msg, [IntPtr]::Zero, [IntPtr]::Zero)
  } catch {
    return $false
  }
}

function Get-LastMetricsRow {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return $null }
  $last = $null
  try {
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
    while (-not $sr.EndOfStream) { $last = $sr.ReadLine() }
    $sr.Close(); $fs.Close()
  } catch { return $null }
  return $last
}

# ---------------------------------------------------------------- start
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
# Windows PowerShell 5.1's ProcessStartInfo has no ArgumentList, so the
# argument string is built here: every value containing whitespace is quoted.
$psi.Arguments = ($argList | ForEach-Object {
  if ($_ -match '\s') { '"{0}"' -f ($_ -replace '"', '\"') } else { $_ }
}) -join ' '
$psi.WorkingDirectory = Split-Path $Exe -Parent
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
# TF2_NATIVE_TRACE reaches the child through plain environment inheritance:
# it is set in this session (above), and a UseShellExecute=false child gets
# the parent's block. ProcessStartInfo.EnvironmentVariables cannot be used
# here -- on this host the getter returns null and the property takes no
# setter -- and writing through it killed every case at ~1 s, before the
# product ever started. stability-root-check.ps1 already proves inherited
# delivery: its child writes the exact trace path it inherited.

try {
  $proc = [System.Diagnostics.Process]::Start($psi)
} catch {
  Set-Content -LiteralPath (Join-Path $dir 'process-state.txt') -Value 'HARNESS_ERROR' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'verdict.txt') -Value 'HARNESS_ERROR' -Encoding UTF8
  Write-Output "HARNESS_ERROR process did not start: $($_.Exception.Message)"
  exit 1
}
$outTask = $proc.StandardOutput.ReadToEndAsync()
$errTask = $proc.StandardError.ReadToEndAsync()

$samples = New-Object System.Collections.Generic.List[object]
$unresponsive = 0
$stallSamples = 0
$lastFrames = -1
$lastSampleCpu = 0.0
$maxCpuPct = 0.0
$maxWsMb = 0.0
$maxPrivateMb = 0.0
$maxThreads = 0
$titleFirst = ''
$titleLast = ''
$startedAt = Get-Date
$sampleIndex = 0

try {
  while ($true) {
    $elapsed = ((Get-Date) - $startedAt).TotalSeconds
    if ($elapsed -ge $Seconds) { break }
    if ($proc.HasExited) { break }

    $proc.Refresh()
    $cpuSeconds = if ($proc.HasExited) { $lastSampleCpu } else { $proc.TotalProcessorTime.TotalSeconds }
    $wsMb = if ($proc.HasExited) { $maxWsMb } else { [math]::Round($proc.WorkingSet64 / 1MB, 1) }
    $privMb = if ($proc.HasExited) { $maxPrivateMb } else { [math]::Round($proc.PrivateMemorySize64 / 1MB, 1) }
    $threads = if ($proc.HasExited) { $maxThreads } else { $proc.Threads.Count }
    if ($wsMb -gt $maxWsMb) { $maxWsMb = $wsMb }
    if ($privMb -gt $maxPrivateMb) { $maxPrivateMb = $privMb }
    if ($threads -gt $maxThreads) { $maxThreads = $threads }
    if ($sampleIndex -gt 0 -and -not $proc.HasExited) {
      $dcpu = $cpuSeconds - $lastSampleCpu
      $pct = if ($SampleSeconds -gt 0) { [math]::Round(($dcpu / $SampleSeconds) * 100 / [Environment]::ProcessorCount, 1) } else { 0 }
      if ($pct -gt $maxCpuPct) { $maxCpuPct = $pct }
    }
    $lastSampleCpu = $cpuSeconds

    $handle = Get-OurWindow -ProcessId $proc.Id
    $responds = Test-WindowResponds -Handle $handle
    if ($responds -eq 0) { $unresponsive++ }
    $title = Get-WindowTitle -Handle $handle
    if ($sampleIndex -eq 0 -and $title) { $titleFirst = $title }
    if ($title) { $titleLast = $title }

    $row = Get-LastMetricsRow -Path (Join-Path $dir 'metrics.csv')
    $frames = -1; $tick = -1
    if ($row -and $row -match ',') {
      $cols = $row.Split(',')
      if ($cols.Length -ge 4) {
        [void][int]::TryParse($cols[1], [ref]$frames)
        [void][int]::TryParse($cols[3], [ref]$tick)
      }
    }
    if ($frames -ge 0) {
      if ($lastFrames -ge 0 -and $frames -eq $lastFrames -and $tick -ge 0) {
        # A sample where the rendered frame count did not move. Recorded, not
        # judged: the gate knows whether the demo had reached its end tick by
        # this point (a finished demo legitimately stops drawing).
        $stallSamples++
      }
      $lastFrames = $frames
    }

    $samples.Add([pscustomobject]@{
      t      = [math]::Round(((Get-Date) - $startedAt).TotalSeconds, 1)
      cpu_s  = [math]::Round($cpuSeconds, 2)
      ws_mb  = $wsMb
      priv_mb = $privMb
      threads = $threads
      resp   = if ($null -eq $responds) { '' } else { "$responds" }
      frames = $frames
      tick   = $tick
    })
    $sampleIndex++

    Start-Sleep -Seconds $SampleSeconds
  }

  # ------------------------------------------------------------- close phase
  $closeRequested = $false
  $killed = $false
  $closeFailedReason = ''
  if (-not $proc.HasExited) {
    if ($NoClose) {
      # Observe only: the caller owns termination. No exit code is expected.
    } else {
      $closeRequested = $true
      $handle = Get-OurWindow -ProcessId $proc.Id
      if ($handle -ne [IntPtr]::Zero) {
        $posted = Send-WindowClose -Handle $handle
        if (-not $posted) { $closeFailedReason = 'post_failed' }
      } else {
        # No window of ours: an earlier phase rejected the run before any
        # window existed. A kill is the only way in, and it is recorded.
        $killed = $true
        $closeFailedReason = 'no_window'
      }
      $deadline = (Get-Date).AddSeconds($CloseGraceSeconds)
      while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
      }
      if (-not $proc.HasExited) {
        $killed = $true
        try { $proc.Kill() } catch { }
      }
    }
  }

  if (-not $proc.WaitForExit(15000)) {
    try { $proc.Kill() } catch { }
    $proc.WaitForExit(5000) | Out-Null
  }

  $exitCode = $proc.ExitCode
  # Windows process exit codes for a fatal fault: C0000005 (access violation),
  # C000001D/-0409 (GS/stack cookie), C0000374 (heap corruption), the abort()
  # code 3, and the VCRuntime's 0xE06D7363 shown as 0x80000003-ish. The trace
  # markers below are the product's own words and are checked as well.
  $crashCodes = @(-1073741819, -1073741811, -1073741571, -1073740943, -1073740791, 3)
  $isCrash = ($crashCodes -contains $exitCode)
  $traceHasCrash = $false
  $traceHasThrew = $false
  $tracePath = Join-Path $dir 'trace.txt'
  if (Test-Path -LiteralPath $tracePath) {
    $traceLines = Get-Content -LiteralPath $tracePath -ErrorAction SilentlyContinue
    if ($traceLines -match 'UNHANDLED|TERMINATE') { $traceHasCrash = $true; $isCrash = $true }
    if ($traceLines -match 'THREW') { $traceHasThrew = $true }
  }

  $state = 'NORMAL_EXIT'
  if ($isCrash) { $state = 'CRASH' }
  elseif ($killed) { $state = 'KILLED_AT_END' }
  elseif ($proc.ExitTime -lt $startedAt.AddSeconds($Seconds)) { $state = 'EARLY_EXIT' }

  if ($state -eq 'CRASH') {
    Set-Content -LiteralPath (Join-Path $dir 'crash.txt') -Value @(
      "exit_code=$exitCode",
      "trace_crash_marker=$traceHasCrash"
    ) -Encoding UTF8
  }

  # ---------------------------------------------------------------- leftovers
  $leftoverProcs = New-Object System.Collections.Generic.List[string]
  foreach ($p in [System.Diagnostics.Process]::GetProcessesByName('tf2_demo_native')) {
    try { if (-not $p.HasExited) { $leftoverProcs.Add("pid=$($p.Id)") } } catch { }
  }
  $leftoverFiles = New-Object System.Collections.Generic.List[string]
  $searchRoots = @(
    $dir,
    (Split-Path $Exe -Parent),
    (Join-Path $env:LOCALAPPDATA 'TF2 Demo Player')
  )
  foreach ($root in $searchRoots) {
    if (-not (Test-Path -LiteralPath $root)) { continue }
    foreach ($f in (Get-ChildItem -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -match '(?i)\.(lock|tmp)$' -or $_.Name -match '(?i)write-test' })) {
      $leftoverFiles.Add($f.FullName)
    }
  }
  $leftoverText = New-Object System.Collections.Generic.List[string]
  if ($leftoverProcs.Count -eq 0) { $leftoverText.Add('residual_processes=none') }
  else { foreach ($lp in $leftoverProcs) { $leftoverText.Add("residual_process=$lp") } }
  if ($leftoverFiles.Count -eq 0) { $leftoverText.Add('leftover_lock_or_tmp=none') }
  else { foreach ($lf in $leftoverFiles) { $leftoverText.Add("leftover_file=$lf") } }
  Set-Content -LiteralPath (Join-Path $dir 'leftovers.txt') -Value $leftoverText -Encoding UTF8

  # ---------------------------------------------------------------- write out
  Set-Content -LiteralPath (Join-Path $dir 'stdout.txt') -Value $outTask.Result -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'stderr.txt') -Value $errTask.Result -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'title-first.txt') -Value $titleFirst -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'title-last.txt') -Value $titleLast -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'exit-code.txt') -Value "$exitCode" -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'process-state.txt') -Value $state -Encoding UTF8
  $verdict = $state
  if ($state -eq 'NORMAL_EXIT' -and $closeRequested) { $verdict = 'COMPLETED' }
  Set-Content -LiteralPath (Join-Path $dir 'verdict.txt') -Value $verdict -Encoding UTF8

  $lines = New-Object System.Collections.Generic.List[string]
  $lines.Add('t,cpu_s,ws_mb,priv_mb,threads,resp,frames,tick')
  foreach ($s in $samples) {
    $lines.Add("$($s.t),$($s.cpu_s),$($s.ws_mb),$($s.priv_mb),$($s.threads),$($s.resp),$($s.frames),$($s.tick)")
  }
  Set-Content -LiteralPath (Join-Path $dir 'sampler.csv') -Value $lines -Encoding UTF8

  $firstTick = -1; $lastTick = -1; $firstFrames = -1; $lastFrames2 = -1
  if (Test-Path -LiteralPath (Join-Path $dir 'metrics.csv')) {
    $rows = Get-Content -LiteralPath (Join-Path $dir 'metrics.csv') | Where-Object { $_ -match ',' }
    $dataRows = @($rows | Select-Object -Skip 1)
    if ($dataRows.Count -gt 0) {
      $firstCols = $dataRows[0].Split(',')
      $lastCols = $dataRows[-1].Split(',')
      if ($firstCols.Length -ge 4 -and $lastCols.Length -ge 4) {
        [void][int]::TryParse($firstCols[1], [ref]$firstFrames)
        [void][int]::TryParse($lastCols[1], [ref]$lastFrames2)
        [void][int]::TryParse($firstCols[3], [ref]$firstTick)
        [void][int]::TryParse($lastCols[3], [ref]$lastTick)
      }
    }
  }

  $demoError = ''
  foreach ($candidate in @($titleLast, $titleFirst)) {
    if ($candidate -match 'Demo error: (?<rest>.+)$') { $demoError = $matches['rest'].Trim(); break }
  }

  $readings = [ordered]@{
    case                  = $Case
    started_at            = $startedAt.ToString('o')
    finished_at           = (Get-Date).ToString('o')
    requested_seconds     = $Seconds
    observed_seconds      = [math]::Round(((Get-Date) - $startedAt).TotalSeconds, 1)
    exit_code             = $exitCode
    process_state         = $state
    verdict               = $verdict
    close_requested       = $closeRequested
    close_failed_reason   = $closeFailedReason
    killed_at_end         = $killed
    samples               = $samples.Count
    max_cpu_percent_total = $maxCpuPct
    max_working_set_mb    = $maxWsMb
    max_private_mb        = $maxPrivateMb
    max_threads           = $maxThreads
    unresponsive_samples  = $unresponsive
    stall_samples         = $stallSamples
    first_tick            = $firstTick
    last_tick             = $lastTick
    first_rendered_frames = $firstFrames
    last_rendered_frames  = $lastFrames2
    title_first           = $titleFirst
    title_last            = $titleLast
    demo_error_from_title = $demoError
    trace_crash_marker    = $traceHasCrash
    trace_threw_marker    = $traceHasThrew
    residual_processes    = $(if ($leftoverProcs.Count -eq 0) { 'none' } else { ($leftoverProcs -join '; ') })
    leftover_files        = $(if ($leftoverFiles.Count -eq 0) { 'none' } else { ($leftoverFiles -join '; ') })
  }
  $readings | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dir 'readings.json') -Encoding UTF8

  Write-Output "CASE=$Case STATE=$state EXIT=$exitCode SAMPLES=$($samples.Count) CLOSE_REASON=$closeFailedReason"
  exit 0
} catch {
  # Never leave a child behind because the harness failed.
  try { if ($proc -and -not $proc.HasExited) { $proc.Kill() } } catch { }
  Set-Content -LiteralPath (Join-Path $dir 'process-state.txt') -Value 'HARNESS_ERROR' -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $dir 'verdict.txt') -Value 'HARNESS_ERROR' -Encoding UTF8
  Write-Output "HARNESS_ERROR $($_.Exception.Message)"
  Write-Output "HARNESS_DETAIL type=$($_.Exception.GetType().FullName) at=$($_.ScriptStackTrace)"
  exit 1
}
