$repo = "https://github.com/a5021/stm32l-discovery-clock.git"
$local = Split-Path -Parent $MyInvocation.MyCommand.Path

if (Test-Path (Join-Path $local ".git")) {
    Set-Location -LiteralPath $local
    git pull
} else {
    git clone $repo $local
}
