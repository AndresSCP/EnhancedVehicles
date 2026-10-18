param(
 [Parameter(Mandatory=$true)][string]$GameDirectory,
 [Parameter(Mandatory=$true)][string]$CpeSourceDirectory
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot
$compiler=Join-Path $GameDirectory 'Tools\Papyrus Compiler\PapyrusCompiler.exe'
$gameSource=Join-Path $GameDirectory 'Data\Scripts\Source'
$flags=Join-Path $gameSource 'Starfield_Papyrus_Flags.flg'
$source=Join-Path $projectRoot 'scripts\Source'
$output=Join-Path $projectRoot 'Data\Scripts'
foreach($required in @($compiler,$gameSource,$flags,$CpeSourceDirectory)){if(!(Test-Path -LiteralPath $required)){throw "Missing build dependency: $required"}}
New-Item -ItemType Directory -Force -Path $output|Out-Null
foreach($name in @('RV8VehicleSettings','RV8VehicleFleet')){
 & $compiler $name "-i=$source;$gameSource;$CpeSourceDirectory" "-o=$output" "-f=$flags" -threadlimit=1
 if($LASTEXITCODE -ne 0){throw "Papyrus compilation failed: $name"}
}
