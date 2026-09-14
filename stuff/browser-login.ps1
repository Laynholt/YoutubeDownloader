param([string]$BrowserExe, [string]$Profile, [string]$OutputFile)

function Test-YouTubeDomain([string]$Domain) {
    return $Domain -eq 'youtube.com' -or $Domain.EndsWith('.youtube.com', [StringComparison]::OrdinalIgnoreCase)
}

function ConvertTo-YouTubeCookieFile($Cookies) {
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('# Netscape HTTP Cookie File')
    foreach ($cookie in $Cookies) {
        if (-not (Test-YouTubeDomain $cookie.domain)) { continue }
        $fields = @([string]$cookie.domain, [string]$cookie.path, [string]$cookie.name, [string]$cookie.value)
        if (($fields | Where-Object { $_ -match "[`t`r`n\x00]" }).Count -ne 0) { continue }
        if (-not $cookie.name -or -not $cookie.path.StartsWith('/')) { continue }
        $domain = [string]$cookie.domain
        $subdomains = if ($domain.StartsWith('.')) { 'TRUE' } else { 'FALSE' }
        if ($cookie.httpOnly) { $domain = '#HttpOnly_' + $domain }
        $secure = if ($cookie.secure) { 'TRUE' } else { 'FALSE' }
        $expiry = [long][Math]::Max(0, [Math]::Floor([double]$cookie.expires))
        $lines.Add((@($domain, $subdomains, $cookie.path, $secure, $expiry, $cookie.name, $cookie.value) -join "`t"))
    }
    return ($lines -join "`r`n") + "`r`n"
}

function Test-YouTubeLogin($Cookies) {
    return @($Cookies | Where-Object {
        (Test-YouTubeDomain $_.domain) -and $_.value -and
        $_.name -in @('SAPISID', '__Secure-1PAPISID', '__Secure-3PAPISID')
    }).Count -gt 0
}

function Get-LocalJson([int]$Port, [string]$Path) {
    $request = [Net.WebRequest]::Create("http://127.0.0.1:$Port$Path")
    $request.Proxy = $null
    $request.Timeout = 1500
    $response = $request.GetResponse()
    try {
        $reader = [IO.StreamReader]::new($response.GetResponseStream())
        try { return ($reader.ReadToEnd() | ConvertFrom-Json) } finally { $reader.Dispose() }
    } finally { $response.Dispose() }
}

function Open-CdpSocket([string]$Url, [int]$Port) {
    $uri = [Uri]$Url
    if ($uri.Scheme -ne 'ws' -or $uri.Host -notin @('127.0.0.1', 'localhost') -or $uri.Port -ne $Port) {
        throw 'dialog.cookies_login_failed'
    }
    $socket = [Net.WebSockets.ClientWebSocket]::new()
    $socket.Options.Proxy = $null
    $deadline = [Threading.CancellationTokenSource]::new(5000)
    try {
        $socket.ConnectAsync($uri, $deadline.Token).GetAwaiter().GetResult() | Out-Null
        return $socket
    } catch { $socket.Dispose(); throw } finally { $deadline.Dispose() }
}

function Invoke-Cdp($Socket, [string]$Method, $Params = @{}) {
    $deadline = [Threading.CancellationTokenSource]::new(5000)
    try {
        $payload = [Text.Encoding]::UTF8.GetBytes((@{id=1;method=$Method;params=$Params} | ConvertTo-Json -Depth 12 -Compress))
        $Socket.SendAsync([ArraySegment[byte]]::new($payload), [Net.WebSockets.WebSocketMessageType]::Text,
            $true, $deadline.Token).GetAwaiter().GetResult() | Out-Null
        $buffer = [byte[]]::new(16384)
        while ($true) {
            $message = [IO.MemoryStream]::new()
            try {
                do {
                    $received = $Socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $deadline.Token).GetAwaiter().GetResult()
                    if ($received.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { throw 'dialog.cookies_login_closed' }
                    $message.Write($buffer, 0, $received.Count)
                    if ($message.Length -gt 1048576) { throw 'dialog.cookies_login_failed' }
                } until ($received.EndOfMessage)
                $reply = [Text.Encoding]::UTF8.GetString($message.ToArray()) | ConvertFrom-Json
                if ($reply.id -eq 1) {
                    if ($reply.error) { throw 'dialog.cookies_login_failed' }
                    return $reply.result
                }
            } finally { $message.Dispose() }
        }
    } finally { $deadline.Dispose() }
}

function Start-YouTubeLogin([string]$BrowserExe, [string]$Profile, [string]$OutputFile) {
    if (-not [IO.File]::Exists($BrowserExe)) { throw 'dialog.cookies_browser_required' }
    $directory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputFile))
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    # The session files are private to the Windows user running the app.
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner($sid)
    $acl.SetAccessRuleProtection($true, $false)
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, 'FullControl',
        'ContainerInherit, ObjectInherit', 'None', 'Allow'))
    [IO.Directory]::SetAccessControl($directory, $acl)
    $lock = [IO.File]::Open($OutputFile + '.lock', 'OpenOrCreate', 'ReadWrite', 'None')
    $browser = $null
    $socket = $null
    $browserSocket = $null
    $temporary = $OutputFile + '.tmp'
    try {
        $Profile = [IO.Path]::GetFullPath($Profile)
        if (-not $Profile.StartsWith($directory + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'dialog.cookies_login_failed'
        }
        [IO.Directory]::CreateDirectory($Profile) | Out-Null
        $portFile = Join-Path $Profile 'DevToolsActivePort'
        if ([IO.File]::Exists($portFile)) { [IO.File]::Delete($portFile) }
        # Google sign-in runs in a normal browser, with no debugging endpoint.
        $arguments = '--user-data-dir="' + $Profile +
            '" --no-first-run --no-default-browser-check --disable-background-mode --new-window https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fwww.youtube.com%2Fsignin%3Faction_handle_signin%3Dtrue%26app%3Ddesktop%26next%3D%252F'
        $browser = Start-Process -FilePath $BrowserExe -ArgumentList $arguments -WindowStyle Normal -PassThru
        $started = [DateTime]::UtcNow
        while (-not $browser.HasExited) {
            if (([DateTime]::UtcNow - $started).TotalMinutes -ge 10) { throw 'dialog.cookies_login_timeout' }
            Start-Sleep -Milliseconds 300
        }
        $browser.Dispose()
        $browser = $null
        # Only after the user closes the login window, read saved YouTube cookies
        # through the browser itself. No Google login page is automated.
        $arguments = '--user-data-dir="' + $Profile + '" --headless=new --remote-debugging-port=0' +
            ' --remote-debugging-address=127.0.0.1 --no-first-run --no-default-browser-check --disable-background-mode about:blank'
        $browser = Start-Process -FilePath $BrowserExe -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $started = [DateTime]::UtcNow
        $port = 0
        while ($port -eq 0) {
            if ($browser.HasExited) { throw 'dialog.cookies_login_closed' }
            if (([DateTime]::UtcNow - $started).TotalSeconds -gt 30) { throw 'dialog.cookies_login_failed' }
            if ([IO.File]::Exists($portFile)) {
                $portLines = [IO.File]::ReadAllLines($portFile)
                if ($portLines.Length -gt 0) { [int]::TryParse($portLines[0], [ref]$port) | Out-Null }
            }
            Start-Sleep -Milliseconds 200
        }
        $version = Get-LocalJson $port '/json/version'
        $browserSocket = Open-CdpSocket $version.webSocketDebuggerUrl $port
        while (([DateTime]::UtcNow - $started).TotalSeconds -lt 30) {
            if ($browser.HasExited) { throw 'dialog.cookies_login_closed' }
            if ($null -eq $socket) {
                $page = @(Get-LocalJson $port '/json/list') | Where-Object { $_.type -eq 'page' } | Select-Object -First 1
                if ($page) { $socket = Open-CdpSocket $page.webSocketDebuggerUrl $port }
            }
            if ($null -ne $socket) {
                try { $cookies = (Invoke-Cdp $socket 'Network.getCookies' @{urls=@('https://www.youtube.com/','https://youtube.com/')}).cookies }
                catch {
                    $socket.Dispose()
                    $socket = $null
                    if ($browser.HasExited) { throw 'dialog.cookies_login_closed' }
                    Start-Sleep -Milliseconds 500
                    continue
                }
                if (-not (Test-YouTubeLogin $cookies)) { throw 'dialog.cookies_login_closed' }
                [IO.File]::WriteAllText($temporary, (ConvertTo-YouTubeCookieFile $cookies), [Text.UTF8Encoding]::new($false))
                if ([IO.File]::Exists($OutputFile)) { [IO.File]::Replace($temporary, $OutputFile, $null) }
                else { [IO.File]::Move($temporary, $OutputFile) }
                return
            }
            Start-Sleep -Milliseconds 500
        }
        throw 'dialog.cookies_login_timeout'
    } finally {
        if ($socket) { $socket.Dispose() }
        if ($browserSocket) {
            try { Invoke-Cdp $browserSocket 'Browser.close' | Out-Null } catch {}
            $browserSocket.Dispose()
        }
        if ($browser) {
            if (-not $browser.HasExited) { $browser.CloseMainWindow() | Out-Null; $browser.WaitForExit(3000) | Out-Null }
            $browser.Dispose()
        }
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        $lock.Dispose()
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    try { Start-YouTubeLogin $BrowserExe $Profile $OutputFile }
    catch {
        $message = $_.Exception.Message
        if (-not $message.StartsWith('dialog.cookies_')) { $message = 'dialog.cookies_login_failed' }
        [Console]::Error.WriteLine($message)
        exit 1
    }
}
