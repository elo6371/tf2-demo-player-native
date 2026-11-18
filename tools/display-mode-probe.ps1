# display-mode-probe.ps1 -- print the current display mode on one line.
#
# WHY THIS IS TRACKED: it is the read-only precondition probe of the t49
# display-stability gate (fullscreen-quiet-check.sh mode_stable), which runs it
# twice, one second apart, and requires the two strings to be non-empty and EQUAL:
# a mode assertion taken while the session is changing resolution is worthless in
# either direction. It used to live at .scratch/t49/mode-probe.ps1 -- a path git
# does not track -- so the gate refused on any clean checkout ("mode probe
# missing"), and after .scratch was cleaned out of this worktree the whole
# fullscreen gate could no longer measure anything. Restored here from the contract
# the gate uses and from the DEVMODE layout desktop-guard.ps1:119-138 already
# marshals; evidence/t49/T49-FULLSCREEN-REPORT.md:24 names the four fields it must
# carry (width/height/refresh/bpp).
$src = @"
using System;
using System.Runtime.InteropServices;
public static class ModeProbe {
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct DEVMODE {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
    public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
    public int dmFields;
    public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
    public short dmLogPixels;
    public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
    public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2;
  }
  public static string Current() {
    DEVMODE d = new DEVMODE();
    d.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
    if (!EnumDisplaySettings(null, -1, ref d)) { return null; }
    return String.Format("{0}x{1}@{2} {3}bpp", d.dmPelsWidth, d.dmPelsHeight, d.dmDisplayFrequency, d.dmBitsPerPel);
  }
}
"@
Add-Type -TypeDefinition $src
$m = [ModeProbe]::Current()
if ([string]::IsNullOrEmpty($m)) { Write-Error "EnumDisplaySettings(null, ENUM_CURRENT_SETTINGS) failed"; exit 1 }
Write-Output $m
