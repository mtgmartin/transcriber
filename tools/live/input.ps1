# Sends mouse/keyboard input. Coordinates are physical screen pixels.
# Usage examples:
#   input.ps1 click 100 200            input.ps1 rclick 100 200     input.ps1 dclick 100 200
#   input.ps1 drag 100 200 400 200     input.ps1 move 100 200       input.ps1 scroll 100 200 -3
#   input.ps1 key ctrl+shift+m         input.ps1 key space          input.ps1 type "120"
#   input.ps1 wait 500
# Several actions can be chained with ";" inside one string: input.ps1 seq "click 10 10; key tab"
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)

if (-not ("Inp" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Threading;
public static class Inp {
  [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNION u; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] static extern short VkKeyScan(char ch);

  static void Mouse(uint flags, uint data = 0) {
    var i = new INPUT[1]; i[0].type = 0; i[0].u.mi.dwFlags = flags; i[0].u.mi.mouseData = data;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Key(ushort vk, bool up) {
    var i = new INPUT[1]; i[0].type = 1; i[0].u.ki.wVk = vk; i[0].u.ki.dwFlags = up ? 2u : 0u;
    // Extended keys (arrows, Insert/Delete, Home/End, PgUp/PgDn) need the extended flag.
    if ((vk >= 0x21 && vk <= 0x28) || vk == 0x2D || vk == 0x2E) i[0].u.ki.dwFlags |= 1u;
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Move(int x, int y) { SetCursorPos(x, y); Thread.Sleep(30); }
  public static void Click(int x, int y, bool right) {
    Move(x, y); Mouse(right ? 8u : 2u); Thread.Sleep(40); Mouse(right ? 16u : 4u); Thread.Sleep(60);
  }
  public static void Drag(int x1, int y1, int x2, int y2, int steps) {
    Move(x1, y1); Mouse(2u); Thread.Sleep(80);
    for (int s = 1; s <= steps; s++) { SetCursorPos(x1 + (x2 - x1) * s / steps, y1 + (y2 - y1) * s / steps); Thread.Sleep(15); }
    Thread.Sleep(120); Mouse(4u); Thread.Sleep(100);
  }
  public static void Scroll(int x, int y, int clicks) { Move(x, y); Mouse(0x0800u, (uint)(clicks * 120)); Thread.Sleep(60); }
  public static void TypeUni(char c) {
    var i = new INPUT[2]; i[0].type = 1; i[0].u.ki.wScan = c; i[0].u.ki.dwFlags = 4u; i[1].type = 1; i[1].u.ki.wScan = c; i[1].u.ki.dwFlags = 6u;
    SendInput(2, i, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(25);
  }
  public static void TypeChar(char c) {
    short r = VkKeyScan(c); ushort vk = (ushort)(r & 0xff); bool shift = (r & 0x100) != 0;
    if (shift) Key(0x10, false); Key(vk, false); Key(vk, true); if (shift) Key(0x10, true); Thread.Sleep(25);
  }
}
"@
}
[Inp]::SetProcessDPIAware() | Out-Null

$vkNames = @{
  ctrl=0x11; shift=0x10; alt=0x12; win=0x5B; enter=0x0D; return=0x0D; esc=0x1B; escape=0x1B; tab=0x09; space=0x20;
  backspace=0x08; delete=0x2E; del=0x2E; insert=0x2D; home=0x24; end=0x23; pageup=0x21; pagedown=0x22;
  left=0x25; up=0x26; right=0x27; down=0x28; comma=0xBC; period=0xBE; minus=0xBD; plus=0xBB
}
for ($i = 1; $i -le 12; $i++) { $vkNames["f$i"] = 0x6F + $i }

function Get-Vk([string]$name) {
  $n = $name.ToLower()
  if ($vkNames.ContainsKey($n)) { return [uint16]$vkNames[$n] }
  if ($n.Length -eq 1) { return [uint16][int][char]$n.ToUpper() }
  throw "Unknown key '$name'"
}

function Run-Action([string]$line) {
  $p = $line.Trim() -split '\s+', 2
  if (-not $p[0]) { return }
  $rest = if ($p.Count -gt 1) { $p[1] } else { "" }
  $n = @($rest -split '\s+' | Where-Object { $_ -ne '' })
  switch ($p[0].ToLower()) {
    'click'  { [Inp]::Click([int]$n[0], [int]$n[1], $false) }
    'rclick' { [Inp]::Click([int]$n[0], [int]$n[1], $true) }
    'dclick' { [Inp]::Click([int]$n[0], [int]$n[1], $false); [Inp]::Click([int]$n[0], [int]$n[1], $false) }
    'move'   { [Inp]::Move([int]$n[0], [int]$n[1]) }
    'drag'   { [Inp]::Drag([int]$n[0], [int]$n[1], [int]$n[2], [int]$n[3], 25) }
    # slowdrag x1 y1 x2 y2 steps: ~15 ms per step, for gradual value changes
    'slowdrag' { [Inp]::Drag([int]$n[0], [int]$n[1], [int]$n[2], [int]$n[3], [int]$n[4]) }
    'scroll' { [Inp]::Scroll([int]$n[0], [int]$n[1], [int]$n[2]) }
    'wait'   { Start-Sleep -Milliseconds ([int]$n[0]) }
    'type'   { foreach ($c in $rest.Trim('"').ToCharArray()) { [Inp]::TypeChar($c) } }
    # utype <file>: types the text of a UTF-8 file as Unicode characters (accents, quotes, dashes)
    'utype'  { foreach ($c in ([IO.File]::ReadAllText($rest.Trim(), [Text.Encoding]::UTF8)).ToCharArray()) { [Inp]::TypeUni($c) } }
    'key'    {
      $keys = @($rest.Trim() -split '\+' | ForEach-Object { Get-Vk $_ })
      foreach ($k in $keys) { [Inp]::Key($k, $false); Start-Sleep -Milliseconds 20 }
      for ($i = $keys.Count - 1; $i -ge 0; $i--) { [Inp]::Key($keys[$i], $true); Start-Sleep -Milliseconds 20 }
      Start-Sleep -Milliseconds 60
    }
    default  { throw "Unknown action '$($p[0])'" }
  }
}

$all = ($Args -join ' ')
if ($all.StartsWith('seq ')) { $all = $all.Substring(4) }
foreach ($a in ($all -split ';')) { Run-Action $a }
"ok: $all"
