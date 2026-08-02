# Regression checks for llvm-msvc issue #176 (32-bit Linux ISel crash).
param(
  [Parameter(Mandatory = $true)][string]$InstallPrefix,
  [Parameter(Mandatory = $true)][string]$RepoRoot
)

$ErrorActionPreference = 'Stop'

$Bin = Join-Path $InstallPrefix 'bin'
$Clang = Join-Path $Bin 'clang++.exe'
$Llc = Join-Path $Bin 'llc.exe'
$TestLl = Join-Path $RepoRoot 'llvm\test\CodeGen\X86\issue176-abort-i686-linux.ll'

foreach ($tool in @($Clang, $Llc)) {
  if (-not (Test-Path $tool)) {
    throw "Missing tool: $tool"
  }
}

if (-not (Test-Path $TestLl)) {
  throw "Missing test IR: $TestLl"
}

$Tmp = Join-Path $env:RUNNER_TEMP "issue176-$PID"
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

function Run([scriptblock]$Block, [string]$Label) {
  Write-Host ">>> $Label"
  & $Block
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed: $Label (exit $LASTEXITCODE)"
  }
}

Run { & $Clang --version } 'clang++ --version'
Run { & $Llc --version } 'llc --version'

$Cpp = Join-Path $Tmp 'issue176.cpp'
$Obj = Join-Path $Tmp 'issue176.o'
Set-Content -Path $Cpp -Value 'void f() { __builtin_abort(); }' -NoNewline

Run {
  & $Clang --target=i686-pc-linux-gnu -O1 -c $Cpp -o $Obj
} 'clang++ i686-linux __builtin_abort @ -O1'

if (-not (Test-Path $Obj) -or (Get-Item $Obj).Length -eq 0) {
  throw "Object file was not produced: $Obj"
}

$Triples = @(
  'i686-pc-linux-gnu',
  'i686-unknown-linux-gnu',
  'i686-linux-android'
)

foreach ($Triple in $Triples) {
  $Asm = Join-Path $Tmp ("issue176-" + ($Triple -replace '[^a-zA-Z0-9]', '_') + '.s')
  Run {
    Get-Content -Raw $TestLl | & $Llc -mtriple=$Triple -relocation-model=pic -o $Asm
  "llc $Triple"
  if (-not (Select-String -Path $Asm -Pattern 'calll' -Quiet)) {
    throw "Expected calll in assembly for $Triple"
  }
}

Write-Host 'issue #176 regression checks passed'
