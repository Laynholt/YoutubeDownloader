$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\stuff\browser-login.ps1"

$cookies = @(
    @{ domain='.youtube.com'; path='/'; name='SAPISID'; value='test-session'; secure=$true; httpOnly=$true; expires=2000000000.5 },
    @{ domain='www.youtube.com'; path='/'; name='session'; value='test'; secure=$false; httpOnly=$false; expires=-1 },
    @{ domain='.example.com'; path='/'; name='other'; value='private'; secure=$false; httpOnly=$false; expires=-1 },
    @{ domain='.notyoutube.com'; path='/'; name='other'; value='private'; secure=$false; httpOnly=$false; expires=-1 },
    @{ domain='.youtube.com'; path='/'; name='bad'; value="line`nbreak"; secure=$false; httpOnly=$false; expires=-1 }
)
$text = ConvertTo-YouTubeCookieFile $cookies
if (-not $text.StartsWith('# Netscape HTTP Cookie File')) { throw 'Missing cookie file header' }
if (-not $text.Contains("#HttpOnly_.youtube.com`tTRUE`t/`tTRUE`t2000000000`tSAPISID`ttest-session")) { throw 'Incorrect persistent cookie' }
if (-not $text.Contains("www.youtube.com`tFALSE`t/`tFALSE`t0`tsession`ttest")) { throw 'Incorrect session cookie' }
if ($text.Contains('private') -or $text.Contains('line')) { throw 'Unrelated cookies or invalid fields were exported' }
if (-not (Test-YouTubeLogin $cookies)) { throw 'YouTube login cookie not recognized' }
if (Test-YouTubeLogin @(@{domain='.example.com';name='SAPISID';value='test'})) { throw 'Unrelated site was treated as YouTube login' }
foreach ($address in 'https://www.youtube.com', 'https://www.youtube.com/watch?v=x', 'youtube.com', 'm.youtube.com/') {
    if (-not (Test-YouTubeAddress $address)) { throw "YouTube address not recognized: $address" }
}
foreach ($address in 'https://accounts.google.com/v3/signin/identifier?service=youtube', 'https://www.youtube.com/signin?action_handle_signin=true',
    'https://notyoutube.com/', 'https://example.com/?u=youtube.com', 'user@youtube.com', '') {
    if (Test-YouTubeAddress $address) { throw "Non-YouTube address treated as signed in: $address" }
}
# Simulate a completed manual login and verify export starts only after that
# browser exits. No real account or browser is used in this regression check.
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ytd-login-' + [Guid]::NewGuid().ToString('N'))
$script:launches = @()
function Start-Process($FilePath, $ArgumentList, $WindowStyle, [switch]$PassThru) {
    $script:launches += @{arguments=$ArgumentList;style=$WindowStyle}
    $exporting = $script:launches.Count -eq 2
    if ($exporting) { [IO.File]::WriteAllText((Join-Path $testRoot 'profile\DevToolsActivePort'), "54321`n") }
    # The login window stays open until the app closes it after YouTube opens.
    $process = [PSCustomObject]@{HasExited=$false}
    $process | Add-Member ScriptMethod Dispose {}
    $process | Add-Member ScriptMethod CloseMainWindow { $script:autoClosed = $true; $this.HasExited = $true; return $true }
    $process | Add-Member ScriptMethod WaitForExit { return $true }
    return $process
}
function Get-BrowserAddresses($Browser) { return @('https://www.youtube.com') }
function Get-LocalJson($Port, $Path) { return @{type='page';webSocketDebuggerUrl='ws://127.0.0.1:54321/test'} }
function Open-CdpSocket($Url, $Port) {
    $socket = [PSCustomObject]@{}
    $socket | Add-Member ScriptMethod Dispose {}
    return $socket
}
function Invoke-Cdp($Socket, $Method, $Params) { return @{cookies=$cookies} }
try {
    Start-YouTubeLogin (Join-Path $PSHOME 'powershell.exe') (Join-Path $testRoot 'profile') (Join-Path $testRoot 'edge.cookies')
    if ($launches.Count -ne 2) { throw 'Login must finish before a separate export process starts' }
    if ($launches[0].arguments -match 'remote-debugging|headless|enable-automation') { throw 'Manual login must not use browser automation' }
    if ($launches[0].style -ne 'Normal') { throw 'Manual login must be visible' }
    if (-not $script:autoClosed) { throw 'Login window must close itself once YouTube opens' }
    foreach ($launch in $launches) {
        if ($launch.arguments -notmatch '--disable-extensions' -or $launch.arguments -notmatch '--disable-sync') { throw 'Browser must start without extensions and sync' }
    }
    if ($launches[0].arguments -notmatch 'https://accounts\.google\.com/ServiceLogin\?' -or $launches[0].arguments -match 'google\.ru') { throw 'Sign-in must start on google.com' }
    if ($launches[1].arguments -notmatch 'headless' -or $launches[1].arguments -notmatch 'about:blank') { throw 'Export must run without a login page' }
    if ($launches[1].style -ne 'Hidden') { throw 'Export must not open another visible window' }
    $savedBytes = [IO.File]::ReadAllBytes((Join-Path $testRoot 'edge.cookies'))
    if ([Text.Encoding]::UTF8.GetString($savedBytes).Contains('test-session')) { throw 'Session must not be stored as plaintext' }
    if (-not (Unprotect-CookieText $savedBytes).Contains('test-session')) { throw 'Session was not exported' }
    $saved = [Convert]::ToBase64String($savedBytes)
    $cookies = @()
    $script:launches = @()
    $failed = $false
    try { Start-YouTubeLogin (Join-Path $PSHOME 'powershell.exe') (Join-Path $testRoot 'profile') (Join-Path $testRoot 'edge.cookies') }
    catch { if ($_.Exception.Message -ne 'dialog.cookies_login_closed') { throw }; $failed = $true }
    if (-not $failed) { throw 'Closing without signing in must not succeed' }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $testRoot 'edge.cookies'))) -ne $saved) { throw 'Failed login replaced the previous session' }
} finally {
    if ([IO.Directory]::Exists($testRoot)) { [IO.Directory]::Delete($testRoot, $true) }
}
'Browser cookie checks passed'
