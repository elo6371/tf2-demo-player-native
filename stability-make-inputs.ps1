# stability-make-inputs.ps1 -- build the corrupt-input fixtures for the T4
# stability matrix from the one real demo this machine can play.
#
# The fixtures are cut from a byte copy of the real snakewater demo, not from
# invented bytes: a fixture that shares no bytes with a real file proves only
# that the parser refuses garbage, not that it refuses *this recording with
# specific damage*. Every fixture records its size and sha256 next to the file
# so a later run can prove it is still the same input.
#
# Fixtures (all under <OutDir>):
#   empty.dem                  0 bytes
#   truncated-header.dem       a header cut before the 1072-byte minimum
#   truncated-packet.dem       a valid header plus a cut-short first packet
#   bad-magic.dem              the HL2DEMO signature replaced
#   bad-version.dem            the demo protocol version replaced
#   incomplete-netmsg.dem      the first packet's payload cut inside the
#                              network message it carries
#   mid-truncated.dem          the real recording cut to ~60% of its size
#
# Usage: powershell -File stability-make-inputs.ps1 -Demo <real.dem> -OutDir <dir>

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$Demo,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [int]$HeaderBytes = 260,
  [int]$TailKeepPercent = 60
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $Demo)) { throw "missing source demo: $Demo" }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$bytes = [System.IO.File]::ReadAllBytes($Demo)
if ($bytes.Length -lt 1072) { throw "source demo is shorter than a header: $($bytes.Length)" }

function Save-Fixture {
  param([string]$Name, [byte[]]$Data)
  $path = Join-Path $OutDir $Name
  [System.IO.File]::WriteAllBytes($path, $Data)
  $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower()
  $line = ("{0} bytes={1} sha256={2}" -f $Name, $Data.Length, $hash)
  Add-Content -LiteralPath (Join-Path $OutDir 'fixture-index.txt') -Value $line
  Write-Output $line
}

Set-Content -LiteralPath (Join-Path $OutDir 'fixture-index.txt') -Value ("source demo=$Demo bytes=$($bytes.Length) sha256=$((Get-FileHash -LiteralPath $Demo -Algorithm SHA256).Hash.ToLower())") -Encoding UTF8

# 1. empty
Save-Fixture -Name 'empty.dem' -Data (New-Object byte[] 0)

# 2. truncated header: take the first $HeaderBytes (< 1072) of the real header
$head = New-Object byte[] $HeaderBytes
[Array]::Copy($bytes, 0, $head, 0, $HeaderBytes)
Save-Fixture -Name 'truncated-header.dem' -Data $head

# Parse the first command so the packet-level fixtures cut at real boundaries.
# Layout (demo_header.cpp): magic 8, demoProtocol i32 @8, networkProtocol i32
# @12 ... mapName @536. Commands start at 1072: cmd byte, tick i32, then a slot
# byte when demoProtocol >= 4 and the command is not DEM_STOP(7), then
# 76 bytes of packet info + 8, then the packet length i32, then the payload.
$demoProtocol = [BitConverter]::ToInt32($bytes, 8)
$cmd = $bytes[1072]
$cursor = 1073 + 4
if ($demoProtocol -ge 4 -and $cmd -ne 7) { $cursor += 1 }
$packetInfoSize = 76 + 8
$cursor += $packetInfoSize
$packetLength = [BitConverter]::ToInt32($bytes, $cursor)
$cursor += 4
$payloadStart = $cursor
$payloadEnd = $payloadStart + $packetLength
if ($payloadEnd -gt $bytes.Length) { throw "source demo's first packet runs past the file (len=$packetLength)" }
Add-Content -LiteralPath (Join-Path $OutDir 'fixture-index.txt') -Value ("first_command cmd=$cmd demoProtocol=$demoProtocol packetLength=$packetLength payloadStart=$payloadStart") -Encoding UTF8

# 3. truncated packet: header + the first command's prefix cut before the
#    packet's declared payload ends (a packet shorter than its own length).
$cutAt = $payloadStart + [math]::Floor($packetLength / 2)
$tp = New-Object byte[] $cutAt
[Array]::Copy($bytes, 0, $tp, 0, $cutAt)
Save-Fixture -Name 'truncated-packet.dem' -Data $tp

# 4. bad magic
$bm = New-Object byte[] ($bytes.Length)
[Array]::Copy($bytes, 0, $bm, 0, $bytes.Length)
[Array]::Copy([System.Text.Encoding]::ASCII.GetBytes('XXL2DEMO'), 0, $bm, 0, 8)
Save-Fixture -Name 'bad-magic.dem' -Data $bm

# 5. bad version: a demo protocol no installed server ever wrote
$bv = New-Object byte[] ($bytes.Length)
[Array]::Copy($bytes, 0, $bv, 0, $bytes.Length)
$badProto = 99999
[Array]::Copy([BitConverter]::GetBytes($badProto), 0, $bv, 8, 4)
Save-Fixture -Name 'bad-version.dem' -Data $bv

# 6. incomplete network message: the first packet kept byte-complete up to the
#    start of its network data, then cut inside it
$netCut = $payloadStart + [math]::Max(1, [math]::Floor($packetLength / 4))
$nm = New-Object byte[] $netCut
[Array]::Copy($bytes, 0, $nm, 0, $netCut)
Save-Fixture -Name 'incomplete-netmsg.dem' -Data $nm

# 7. mid-recording truncation
$keep = [math]::Floor($bytes.Length * $TailKeepPercent / 100)
$mt = New-Object byte[] $keep
[Array]::Copy($bytes, 0, $mt, 0, $keep)
Save-Fixture -Name 'mid-truncated.dem' -Data $mt

Write-Output "fixtures written to $OutDir"
