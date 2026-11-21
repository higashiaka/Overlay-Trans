# Open-source Sign in with ChatGPT. Credentials are protected by Windows DPAPI.
function Connect-ChatGpt {
    $ErrorActionPreference = 'Stop'
    Add-Type -AssemblyName System.Security
    $dir = Join-Path $env:LOCALAPPDATA 'OverlayTrans\ChatGPT'
    [IO.Directory]::CreateDirectory($dir) | Out-Null
    $hostFile = Join-Path $dir 'host.txt'
    if (!(Test-Path -LiteralPath $hostFile)) {
        [IO.File]::WriteAllText($hostFile, ('urn:uuid:' + [guid]::NewGuid()))
    }
    $credential = Join-Path $dir 'session.bin'
    $protection = [Security.Cryptography.DataProtectionScope]::CurrentUser
    $utf8 = [Text.Encoding]::UTF8
    function Invoke-WithSessionLock([scriptblock]$action) {
        $mutex = [Threading.Mutex]::new($false, 'Local\OverlayTrans.ChatGPT.Session')
        $held = $false
        try {
            try { $held = $mutex.WaitOne(30000) }
            catch [Threading.AbandonedMutexException] { $held = $true }
            if (!$held) { throw 'ChatGPT session is busy.' }
            & $action
        } finally {
            if ($held) { $mutex.ReleaseMutex() }
            $mutex.Dispose()
        }
    }
    $previous = $null
    if (Test-Path -LiteralPath $credential) {
        $previous = $utf8.GetString([Security.Cryptography.ProtectedData]::Unprotect(
            [IO.File]::ReadAllBytes($credential), $null, $protection)) | ConvertFrom-Json
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    if ($previous) {
        Write-Host ("Connected ChatGPT account: " + $previous.email)
        $action = Read-Host 'Enter R to reconnect, D to disconnect, or press Enter to cancel'
        if ($action -ieq 'D') {
            $revoked = Invoke-WithSessionLock {
                if (Test-Path -LiteralPath $credential) {
                    $current = $utf8.GetString([Security.Cryptography.ProtectedData]::Unprotect(
                        [IO.File]::ReadAllBytes($credential), $null, $protection)) | ConvertFrom-Json
                    if ($current.client_id -cne $previous.client_id) { throw 'The active ChatGPT account changed.' }
                    [IO.File]::Delete($credential)
                    $current
                }
            }
            if (!$revoked) { return }
            $metadata = Invoke-RestMethod -Uri 'https://auth.openai.com/.well-known/openid-configuration' -TimeoutSec 15
            $revoke = [uri]$metadata.revocation_endpoint
            if ($revoke.GetLeftPart('Authority') -cne 'https://auth.openai.com') { throw 'Unexpected revocation endpoint.' }
            Invoke-RestMethod -Uri $revoke -Method Post -TimeoutSec 15 -Body @{
                token = $revoked.refresh_token; token_type_hint = 'refresh_token'; client_id = $revoked.client_id
            } | Out-Null
            return
        }
        if ($action -ine 'R') { return }
    }
    function Encode-UrlBytes([byte[]]$bytes) {
        [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
    }
    function Decode-UrlBytes([string]$value) {
        $value = $value.Replace('-', '+').Replace('_', '/')
        [Convert]::FromBase64String($value.PadRight(($value.Length + 3) -band -4, '='))
    }
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    function New-Challenge {
        $bytes = New-Object byte[] 32
        $rng.GetBytes($bytes)
        Encode-UrlBytes $bytes
    }
    $state = New-Challenge
    $nonce = New-Challenge
    $verifier = New-Challenge
    $rng.Dispose()
    $sha = [Security.Cryptography.SHA256]::Create()
    $challenge = Encode-UrlBytes ($sha.ComputeHash($utf8.GetBytes($verifier)))
    $sha.Dispose()

    Add-Type -AssemblyName System.Web
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $base = "http://127.0.0.1:$port/"
    $redirect = $base + 'auth/callback'
    $issuer = 'https://auth.openai.com'
    $resource = 'https://api.openai.com/v1'
    $scope = 'openid profile email offline_access resource.invoke chatgpt.tokens.use.direct'
    $query = @{
        client_id = 'dynamic_agent_client'; agent_name_hint = 'OverlayTrans'
        ext_agent_host_id = [IO.File]::ReadAllText($hostFile)
        response_type = 'code'; redirect_uri = $redirect; scope = $scope; resource = $resource
        state = $state; nonce = $nonce; code_challenge_method = 'S256'; code_challenge = $challenge
    }
    if ($previous) {
        $query.client_id = $previous.client_id
        $query.Remove('agent_name_hint')
        $query.id_token_hint = $previous.id_token
    }
    $encoded = ($query.GetEnumerator() | ForEach-Object {
        $_.Key + '=' + [uri]::EscapeDataString($_.Value)
    }) -join '&'
    $valid = $false
    try {
        Start-Process ($issuer + '/api/accounts/authorize?' + $encoded)
        $deadline = [DateTime]::UtcNow.AddMinutes(3)
        do {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'ChatGPT sign-in timed out.' }
            if (!$listener.Pending()) { Start-Sleep -Milliseconds 100; continue }
            $socket = $listener.AcceptTcpClient()
            try {
                $stream = $socket.GetStream()
                $stream.ReadTimeout = 1000
                $line = [Text.StringBuilder]::new()
                while ($line.Length -lt 16384 -and [DateTime]::UtcNow -lt $deadline) {
                    $byte = $stream.ReadByte()
                    if ($byte -lt 0 -or $byte -eq 10) { break }
                    [void]$line.Append([char]$byte)
                }
                if ($line.ToString() -cmatch '^GET (/auth/callback\?[^ ]+) HTTP/1\.[01]\r$') {
                    $target = $Matches[1]
                    $response = [Web.HttpUtility]::ParseQueryString($target.Substring($target.IndexOf('?') + 1))
                    $valid = $response['state'] -ceq $state
                }
                $status = if ($valid) { '200 OK' } else { '404 Not Found' }
                $message = 'Return to OverlayTrans to finish ChatGPT setup.'
                $body = $utf8.GetBytes("HTTP/1.1 $status`r`nConnection: close`r`nCache-Control: no-store`r`nContent-Type: text/plain`r`nContent-Length: $($message.Length)`r`n`r`n$message")
                $stream.Write($body, 0, $body.Length)
            } catch [IO.IOException] {
                # Ignore an incomplete loopback request.
            } finally {
                $socket.Dispose()
            }
        } until ($valid)
    } finally {
        $listener.Stop()
    }
    if ($response['error'] -or !$response['code']) { throw 'ChatGPT authorization was declined.' }
    $client = $response['client_id']
    if ($previous) {
        if ($client -and $client -cne $previous.client_id) { throw 'Unexpected ChatGPT client.' }
        $client = $previous.client_id
    }
    if ($client -notmatch '^oaiapp_[a-zA-Z0-9_-]+$') { throw 'Missing issued ChatGPT client.' }
    $tokens = Invoke-RestMethod -Method Post -Uri "$issuer/api/accounts/oauth/token" -TimeoutSec 20 -Body @{
        grant_type = 'authorization_code'; client_id = $client; code = $response['code']
        code_verifier = $verifier; redirect_uri = $redirect; resource = $resource
    }

    $parts = $tokens.id_token -csplit '\.'
    if ($parts.Count -ne 3) { throw 'Invalid ChatGPT identity token.' }
    $header = $utf8.GetString((Decode-UrlBytes $parts[0])) | ConvertFrom-Json
    $claims = $utf8.GetString((Decode-UrlBytes $parts[1])) | ConvertFrom-Json
    $metadata = Invoke-RestMethod -Uri "$issuer/.well-known/openid-configuration" -TimeoutSec 15
    if ($metadata.issuer -cne $issuer -or ([uri]$metadata.jwks_uri).GetLeftPart('Authority') -cne $issuer) {
        throw 'Unexpected ChatGPT identity provider.'
    }
    $key = (Invoke-RestMethod -Uri $metadata.jwks_uri -TimeoutSec 15).keys |
        Where-Object { $_.kid -ceq $header.kid -and $_.kty -ceq 'RSA' } | Select-Object -First 1
    if ($header.alg -cne 'RS256' -or !$key) { throw 'Unsupported ChatGPT identity signature.' }
    $rsa = [Security.Cryptography.RSA]::Create()
    try {
        $parameters = New-Object Security.Cryptography.RSAParameters
        $parameters.Modulus = Decode-UrlBytes $key.n
        $parameters.Exponent = Decode-UrlBytes $key.e
        $rsa.ImportParameters($parameters)
        if (!$rsa.VerifyData($utf8.GetBytes($parts[0] + '.' + $parts[1]), (Decode-UrlBytes $parts[2]),
                [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
            throw 'Invalid ChatGPT identity signature.'
        }
    } finally { $rsa.Dispose() }
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    if ($claims.iss -cne $issuer -or @($claims.aud).Count -ne 1 -or $claims.aud -cne $client -or
        $claims.nonce -cne $nonce -or !$claims.sub -or $claims.exp -le $now -or
        ($previous -and $previous.subject -cne $claims.sub)) { throw 'Unexpected ChatGPT identity.' }
    if (($tokens.scope -split ' ') -cnotcontains 'chatgpt.tokens.use.direct' -or
        !$tokens.access_token -or !$tokens.refresh_token -or $tokens.expires_in -le 0 -or
        $tokens.token_type -ine 'Bearer') { throw 'ChatGPT plan permission was not granted.' }

    Write-Host ("Connected ChatGPT account: " + $claims.email)
    Write-Host 'Automatic subtitle translation sends recognized speech, recent translations, and enabled stream/chat context or DeepL drafts to OpenAI.'
    if ((Read-Host 'Type YES to allow automatic translation') -cne 'YES') { throw 'Automatic translation was not enabled.' }
    $record = @{
        client_id = $client; subject = $claims.sub; email = $claims.email; id_token = $tokens.id_token
        access_token = $tokens.access_token; refresh_token = $tokens.refresh_token
        scope = $tokens.scope; expires_at = $now + [long]$tokens.expires_in
    }
    $tmp = Join-Path $dir ([guid]::NewGuid().ToString() + '.tmp')
    try {
        $bytes = $utf8.GetBytes(($record | ConvertTo-Json -Compress))
        [IO.File]::WriteAllBytes($tmp, [Security.Cryptography.ProtectedData]::Protect($bytes, $null, $protection))
        Invoke-WithSessionLock {
            if ([IO.File]::Exists($credential)) { [IO.File]::Replace($tmp, $credential, $null) }
            else { [IO.File]::Move($tmp, $credential) }
        }
    } finally {
        if ([IO.File]::Exists($tmp)) { [IO.File]::Delete($tmp) }
    }
}
