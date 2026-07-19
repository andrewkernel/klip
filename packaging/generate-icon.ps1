[CmdletBinding()]
param(
  [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $repositoryRoot "assets\klip.ico"
}
$destination = [System.IO.Path]::GetFullPath($OutputPath)
$destinationDirectory = Split-Path -Parent $destination
New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null

function New-KlipPng([int]$Size) {
  $bitmap = [System.Drawing.Bitmap]::new($Size, $Size,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  try {
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $scale = $Size * 0.82 / 36.0
    $offsetX = ($Size - 36.0 * $scale) / 2.0
    $offsetY = ($Size - 34.0 * $scale) / 2.0

    $leftBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 139, 67, 247))
    $lightBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 177, 90, 255))
    $darkBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 103, 48, 219))
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    try {
      $radius = 4.0 * $scale
      $diameter = 2.0 * $radius
      $left = $offsetX
      $top = $offsetY
      $width = 11.0 * $scale
      $height = 34.0 * $scale
      $path.AddArc($left, $top, $diameter, $diameter, 180, 90)
      $path.AddArc($left + $width - $diameter, $top, $diameter, $diameter, 270, 90)
      $path.AddArc($left + $width - $diameter, $top + $height - $diameter, $diameter, $diameter, 0, 90)
      $path.AddArc($left, $top + $height - $diameter, $diameter, $diameter, 90, 90)
      $path.CloseFigure()
      $graphics.FillPath($leftBrush, $path)

      $upper = @(
        [System.Drawing.PointF]::new($offsetX + 8 * $scale, $offsetY + 15 * $scale),
        [System.Drawing.PointF]::new($offsetX + 24 * $scale, $offsetY),
        [System.Drawing.PointF]::new($offsetX + 35 * $scale, $offsetY),
        [System.Drawing.PointF]::new($offsetX + 17 * $scale, $offsetY + 18 * $scale)
      )
      $lower = @(
        [System.Drawing.PointF]::new($offsetX + 9 * $scale, $offsetY + 18 * $scale),
        [System.Drawing.PointF]::new($offsetX + 18 * $scale, $offsetY + 10 * $scale),
        [System.Drawing.PointF]::new($offsetX + 36 * $scale, $offsetY + 34 * $scale),
        [System.Drawing.PointF]::new($offsetX + 23 * $scale, $offsetY + 34 * $scale)
      )
      $graphics.FillPolygon($lightBrush, $upper)
      $graphics.FillPolygon($darkBrush, $lower)
    } finally {
      $path.Dispose()
      $leftBrush.Dispose()
      $lightBrush.Dispose()
      $darkBrush.Dispose()
    }

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    return $stream.ToArray()
  } finally {
    $graphics.Dispose()
    $bitmap.Dispose()
  }
}

$sizes = @(16, 24, 32, 48, 64, 256)
$images = @()
foreach ($size in $sizes) {
  $images += ,([byte[]](New-KlipPng $size))
}
$headerSize = 6 + 16 * $images.Count
$offset = $headerSize
$stream = [System.IO.File]::Open($destination, [System.IO.FileMode]::Create,
  [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
  $writer.Write([uint16]0)
  $writer.Write([uint16]1)
  $writer.Write([uint16]$images.Count)
  for ($index = 0; $index -lt $images.Count; ++$index) {
    $size = $sizes[$index]
    $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
    $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$index].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$index].Length
  }
  foreach ($image in $images) {
    $writer.Write($image)
  }
} finally {
  $writer.Dispose()
  $stream.Dispose()
}

Write-Host "Generated $destination"
