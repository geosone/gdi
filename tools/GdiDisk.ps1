# GdiDisk.ps1 - loads the disk set tools of the GeosOne DOS Installer
# (GdiDisk.cs: FAT12 images, SZDD/KWAJ expand, GDA archives).
#   . ./GdiDisk.ps1      (dot-source; Windows PowerShell 5.1 or pwsh 7)
# Copyright (C) 2026 GeosOne.  GNU General Public License version 3.

if (-not ('GeosOne.Gdi.Gda' -as [type])) {
    $src = Get-Content -Raw -Path (Join-Path $PSScriptRoot 'GdiDisk.cs')
    if ($PSVersionTable.PSEdition -eq 'Core') {
        Add-Type -TypeDefinition $src -Language CSharp
    } else {
        Add-Type -AssemblyName System.IO.Compression
        Add-Type -TypeDefinition $src -Language CSharp -ReferencedAssemblies System.IO.Compression
    }
}
