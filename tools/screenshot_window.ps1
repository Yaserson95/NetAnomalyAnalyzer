param(
    [string]$ProcessName = "NetAnomalyAnalyzer",
    [string]$OutFile = "$env:TEMP\opencode\app_shot.png",
    [int]$TabSwitches = 0
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Win32Grab {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$proc = Get-Process $ProcessName -ErrorAction Stop
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "No main window" }
[Win32Grab]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 700
if ($TabSwitches -gt 0) {
    $ws = New-Object -ComObject WScript.Shell
    for ($i = 0; $i -lt $TabSwitches; $i++) {
        $ws.SendKeys("^{TAB}")
        Start-Sleep -Milliseconds 400
    }
    Start-Sleep -Milliseconds 600
}
$rect = New-Object Win32Grab+RECT
[Win32Grab]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
# PrintWindow: рендеринг окна без доступа к рабочему столу (PW_RENDERFULLCONTENT=2)
$hdc = $gfx.GetHdc()
[Win32Grab]::PrintWindow($hwnd, $hdc, 2) | Out-Null
$gfx.ReleaseHdc($hdc)
New-Item -ItemType Directory -Force (Split-Path $OutFile) | Out-Null
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Output "saved $OutFile ($w x $h)"
