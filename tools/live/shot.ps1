# Captures the whole virtual screen (all monitors) to a PNG, optionally scaled down.
param([string]$Out, [double]$Scale = 0.5, [int]$X = -1, [int]$Y = -1, [int]$W = 0, [int]$H = 0)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System.Runtime.InteropServices;
public static class Dpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
"@
[Dpi]::SetProcessDPIAware() | Out-Null

$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
if ($W -le 0) { $X = $vs.X; $Y = $vs.Y; $W = $vs.Width; $H = $vs.Height }

$bmp = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($X, $Y, 0, 0, $bmp.Size)
$g.Dispose()

$ow = [int]($W * $Scale); $oh = [int]($H * $Scale)
$small = New-Object System.Drawing.Bitmap $ow, $oh
$g2 = [System.Drawing.Graphics]::FromImage($small)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g2.DrawImage($bmp, 0, 0, $ow, $oh)
$g2.Dispose()
$small.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"screen origin=($X,$Y) size=${W}x${H} saved ${ow}x${oh} scale=$Scale"
