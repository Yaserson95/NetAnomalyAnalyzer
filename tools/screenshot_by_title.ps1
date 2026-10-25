param(
    [string]$ProcessName = "NetAnomalyAnalyzer",
    [string]$TitleContains = "Справка",
    [string]$OutFile = "$env:TEMP\opencode\win_shot.png"
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Win32Grab2 {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lp);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$proc = Get-Process $ProcessName -ErrorAction Stop
$global:WinGrabTarget = [IntPtr]::Zero
$global:WinGrabTitle = $TitleContains
$cb = [Win32Grab2+EnumWindowsProc]{
    param($h, $lp)
    $procId = [uint32]($lp.ToInt64())
    $pid2 = 0
    [Win32Grab2]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
    if ($pid2 -eq $procId -and [Win32Grab2]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 512
        [Win32Grab2]::GetWindowText($h, $sb, 512) | Out-Null
        if ($sb.ToString().Contains($global:WinGrabTitle)) {
            $global:WinGrabTarget = $h
            return $false
        }
    }
    return $true
}
[Win32Grab2]::EnumWindows($cb, $proc.Id) | Out-Null
$target = $global:WinGrabTarget
if ($target -eq [IntPtr]::Zero) { throw "Window '$TitleContains' not found for $ProcessName" }
$r = New-Object Win32Grab2+RECT
[Win32Grab2]::GetWindowRect($target, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$hgt = $r.Bottom - $r.Top
if ($w -le 0 -or $hgt -le 0) { throw "Bad window rect" }
$bmp = New-Object System.Drawing.Bitmap($w, $hgt)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[Win32Grab2]::PrintWindow($target, $hdc, 2) | Out-Null   # 2 = PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved $OutFile ($w x $hgt)"
