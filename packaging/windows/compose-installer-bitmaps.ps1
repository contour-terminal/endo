# SPDX-License-Identifier: Apache-2.0
#
# Composes the WiX UI bitmaps from the logo's icon theme PNGs. Run at build time
# by cmake/Packaging.cmake, so the installer artwork -- uncompressed BMP, which is
# what WixUI takes -- is derived from the logo rather than committed.
#
# WixUI draws its own text over both bitmaps: the banner's title and description
# along its left side, the Welcome and Finish text from about 180 px in. Each
# layout below keeps its art clear of that.

param(
    [Parameter(Mandatory)] [string] $IconTheme,
    [Parameter(Mandatory)] [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Layouts = @(
    # The banner atop every inner dialog: the icon at its right end.
    @{ File = 'installer-banner.bmp'; Width = 493; Height = 58; PanelWidth = 0; IconSize = 48; IconX = 437; IconY = 5 }
    # The image behind Welcome and Finish: the icon on a black panel along the left.
    @{ File = 'installer-dialog.bmp'; Width = 493; Height = 312; PanelWidth = 164; IconSize = 128; IconX = 18; IconY = 72 }
)

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
foreach ($layout in $Layouts) {
    $canvas = [System.Drawing.Bitmap]::new($layout.Width, $layout.Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($canvas)
    $icon = [System.Drawing.Image]::FromFile((Join-Path $IconTheme "$($layout.IconSize)x$($layout.IconSize)/apps/endo.png"))
    try {
        $graphics.Clear([System.Drawing.Color]::White)
        if ($layout.PanelWidth -gt 0) {
            $graphics.FillRectangle([System.Drawing.Brushes]::Black, 0, 0, $layout.PanelWidth, $layout.Height)
        }
        # Drawn at its own size with no resampling, so the PNG's pixels land as rendered.
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $graphics.DrawImage($icon, $layout.IconX, $layout.IconY, $layout.IconSize, $layout.IconSize)
        $canvas.Save((Join-Path $OutputDirectory $layout.File), [System.Drawing.Imaging.ImageFormat]::Bmp)
    }
    finally {
        $icon.Dispose()
        $graphics.Dispose()
        $canvas.Dispose()
    }
}
