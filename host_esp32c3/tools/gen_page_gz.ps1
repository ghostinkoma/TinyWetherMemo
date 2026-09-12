<#
  gen_page_gz.ps1 - src/web_ui.cpp の SPA(R"HTML(...)HTML") を抽出し UTF-8 gzip して
                    src/web_page_gz.h を再生成する。HTML/JS を編集したら必ず実行。
  例:  powershell -ExecutionPolicy Bypass -File tools\gen_page_gz.ps1
#>
$ErrorActionPreference = "Stop"
$src = Join-Path $PSScriptRoot "..\src\web_ui.cpp"
$out = Join-Path $PSScriptRoot "..\src\web_page_gz.h"

$text = [System.IO.File]::ReadAllText($src, [System.Text.Encoding]::UTF8)
$startTok = 'R"HTML('
$endTok = ')HTML"'
$si = $text.IndexOf($startTok); if ($si -lt 0) { throw "start token not found" }
$si += $startTok.Length
$ei = $text.IndexOf($endTok, $si); if ($ei -lt 0) { throw "end token not found" }
$html = $text.Substring($si, $ei - $si)

$bytes = [System.Text.Encoding]::UTF8.GetBytes($html)
$ms = New-Object System.IO.MemoryStream
$gz = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionLevel]::Optimal)
$gz.Write($bytes, 0, $bytes.Length)
$gz.Close()
$comp = $ms.ToArray()

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("// AUTO-GENERATED: gzip of the SPA HTML (UTF-8). Regenerate after editing web_ui.cpp HTML.")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("#include <Arduino.h>")
[void]$sb.AppendLine("static const unsigned WLB_PAGE_GZ_LEN = $($comp.Length);")
[void]$sb.Append("static const uint8_t WLB_PAGE_GZ[] PROGMEM = {")
for ($i = 0; $i -lt $comp.Length; $i++) {
  if ($i) { [void]$sb.Append(',') }
  [void]$sb.Append('0x' + $comp[$i].ToString('x2'))
}
[void]$sb.AppendLine("};")
[System.IO.File]::WriteAllText($out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Wrote $out : html=$($bytes.Length)B gz=$($comp.Length)B"
# sanity: gunzip round-trip and check a Japanese token survives
$in = New-Object System.IO.MemoryStream(,$comp)
$dz = New-Object System.IO.Compression.GZipStream($in, [System.IO.Compression.CompressionMode]::Decompress)
$rd = New-Object System.IO.StreamReader($dz, [System.Text.Encoding]::UTF8)
$round = $rd.ReadToEnd(); $rd.Close()
Write-Host ("roundtrip ok=" + ($round -eq $html) + " has_home_ja=" + $round.Contains("ホーム"))
