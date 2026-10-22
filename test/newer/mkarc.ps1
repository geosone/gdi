# NEWARC.D01 of test/newer from ARCKEEP.DLL and ARCTAKE.DLL (tools/GdiDisk.ps1)
param([string]$GdiDisk = (Join-Path $PSScriptRoot '../../tools/GdiDisk.ps1'))
. $GdiDisk
$list = New-Object 'System.Collections.Generic.List[GeosOne.Gdi.GdaEntry]'
foreach ($n in 'ARCKEEP.DLL', 'ARCTAKE.DLL') {
    $list.Add((New-Object GeosOne.Gdi.GdaEntry($n, [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $n)), (Get-Date))))
}
$parts = [GeosOne.Gdi.Gda]::Build($list, [Func[int, long]] { param($n) 1000000 }, 1234)
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'NEWARC.D01'), $parts[0])
