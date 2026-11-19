<#
.SYNOPSIS
    Runs liboncrpc's tests and examples from a Visual Studio build.

.DESCRIPTION
    Runs test_xdr, test_xspec, test_gen and test_rpc, then each example: the
    example's server is started on a test port, its client is run against it,
    and the server is stopped. 06_portmap is skipped, as Windows has no port
    mapper (rpcbind). With -Soak, test_soak runs as well: many clients
    against small server limits, with UDP through a lossy relay (about ten
    seconds at scale 1). The exit status is 0 if everything passed.

    Build the solution first (Build > Build Solution, or msbuild oncrpc.sln).

.PARAMETER Configuration
    Which build to run: Release (the default) or Debug.

.PARAMETER BinDir
    Where the programs are; by default build\x64\<Configuration>.

.PARAMETER BasePort
    The examples use ports BasePort+1 to BasePort+8 on 127.0.0.1
    (default 39123).

.PARAMETER Suffix
    File name suffix of the programs (default .exe).

.PARAMETER Soak
    Also run test_soak.

.PARAMETER Seed
    test_soak's random seed (default 1).

.PARAMETER Scale
    Multiplies test_soak's number of calls (default 1).

.EXAMPLE
    .\tests\run_tests.ps1
.EXAMPLE
    .\tests\run_tests.ps1 -Configuration Debug
.EXAMPLE
    .\tests\run_tests.ps1 -Soak -Seed 7 -Scale 10
#>
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$BinDir = '',
    [int]$BasePort = 39123,
    [string]$Suffix = '.exe',
    [switch]$Soak,
    [int]$Seed = 1,
    [int]$Scale = 1
)

$ErrorActionPreference = 'Stop'

if (-not $BinDir) {
    $BinDir = Join-Path $PSScriptRoot (Join-Path '..' (Join-Path 'build' (Join-Path 'x64' $Configuration)))
}
if (-not (Test-Path $BinDir)) {
    Write-Host "No build in $BinDir. Build oncrpc.sln ($Configuration | x64) first."
    exit 1
}
$BinDir = (Resolve-Path $BinDir).Path
$failed = $false

function Program([string]$name) {
    return Join-Path $BinDir ($name + $Suffix)
}

# ---- Tests --------------------------------------------------------------------

foreach ($test in 'test_xdr', 'test_xspec', 'test_gen', 'test_rpc') {
    Write-Host "== $test"
    & (Program $test)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "-- ${test}: FAILED"
        $failed = $true
    }
}

if ($Soak) {
    Write-Host "== test_soak"
    & (Program 'test_soak') $Seed $Scale
    if ($LASTEXITCODE -ne 0) {
        Write-Host "-- test_soak: FAILED"
        $failed = $true
    }
}

# ---- Examples -------------------------------------------------------------------

$examples = '01_add', '02_strings', '03_shapes', '04_lists', '05_async', '07_errors'

foreach ($example in $examples) {
    $port = $BasePort + [int]$example.Substring(0, 2)
    $log = Join-Path $BinDir "${example}_server.log"
    Write-Host "== $example"

    $server = Start-Process -FilePath (Program "${example}_server") -ArgumentList "$port" `
                            -NoNewWindow -PassThru -RedirectStandardOutput $log
    Start-Sleep -Milliseconds 500

    & (Program "${example}_client") 127.0.0.1 $port
    if ($LASTEXITCODE -eq 0) {
        Write-Host "-- ${example}: ok"
    } else {
        Write-Host "-- ${example}: FAILED (server log follows)"
        Get-Content $log | Write-Host
        $failed = $true
    }

    Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    $server.WaitForExit()
}

Write-Host "== 06_portmap"
Write-Host "-- 06_portmap: skipped (needs a port mapper, rpcbind, which Windows does not have)"

Write-Host "== 08_xdr"
& (Program '08_xdr_demo')
if ($LASTEXITCODE -eq 0) {
    Write-Host "-- 08_xdr: ok"
} else {
    Write-Host "-- 08_xdr: FAILED"
    $failed = $true
}

if ($failed) {
    Write-Host "== some tests or examples FAILED"
    exit 1
}
Write-Host "== all tests and examples passed"
exit 0
