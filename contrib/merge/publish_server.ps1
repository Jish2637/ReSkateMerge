param([Parameter(Mandatory=$true)][ValidateSet('Prepare','Publish','Verify')][string]$Mode,
      [string]$Version='2.1.6', [string]$ReleaseTag='v0.1.11')
$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'
$repo='Jish2637/ReSkateMerge'; $root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$stage=Join-Path $root "artifacts/server-release-v$Version"
$meta=Get-Content (Join-Path $stage 'server/BUILD_INFO.json') -Raw | ConvertFrom-Json
$old=Get-Content (Join-Path $root 'contrib/merge/server-release-base.json') -Raw | ConvertFrom-Json
$sha=(& git -C $root rev-parse HEAD).Trim()
if($meta.source_commit -ne $sha -or (& git -C $root status --porcelain)){throw 'Package the final clean committed source first.'}
# Use the existing Git helper; credentials stay in memory and are never printed.
$env:GCM_INTERACTIVE='never'; $env:GIT_TERMINAL_PROMPT='0'
$lines="protocol=https`nhost=github.com`n`n" | & git -C $root credential fill 2>$null
if($LASTEXITCODE -ne 0){throw 'GitHub authentication is unavailable from the Git credential helper.'}
$cred=@{}; foreach($line in $lines){$p=$line.Split('=',2); if($p.Count -eq 2){$cred[$p[0]]=$p[1]}}
if(-not $cred.password){throw 'GitHub authentication is unavailable.'}
$headers=@{Authorization='Bearer '+$cred.password; Accept='application/vnd.github+json'; 'User-Agent'='MSC-server-release'}
$lines=$null; $cred=$null
function Api([string]$Path,[string]$Method='GET',$Body=$null){
    try{
        $p=@{Uri="https://api.github.com/repos/$repo$Path"; Method=$Method; Headers=$headers; MaximumRedirection=0; TimeoutSec=60}
        if($null -ne $Body){$p.Body=[Text.Encoding]::UTF8.GetBytes(($Body|ConvertTo-Json -Depth 60 -Compress)); $p.ContentType='application/json'}
        Invoke-RestMethod @p
    }catch{throw "GitHub $Method $Path failed: HTTP $([int]$_.Exception.Response.StatusCode)"}
}
function Hash([string]$File){(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Release{Api '/releases/latest'}
function Assert($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function ByName($Release,[string]$Name){@($Release.assets|Where-Object name -EQ $Name)|Select-Object -First 1}
function Upload($Release,[string]$Name,[string]$File){
    $digest='sha256:'+(Hash $File); $existing=ByName (Release) $Name
    if($existing){Assert ($existing.digest -eq $digest) "Existing $Name has different bytes."; return $existing}
    $uri="https://uploads.github.com/repos/$repo/releases/$($Release.id)/assets?name="+[Uri]::EscapeDataString($Name)
    try{$a=Invoke-RestMethod -Uri $uri -Method POST -Headers $headers -ContentType 'application/octet-stream' -InFile $File -MaximumRedirection 0 -TimeoutSec 120}
    catch{throw "Upload $Name failed: HTTP $([int]$_.Exception.Response.StatusCode)"}
    Assert ($a.digest -eq $digest -and $a.size -eq (Get-Item $File).Length) "Upload verification failed: $Name"
    Write-Host "Verified upload: $Name"; return $a
}
function RenameAsset([long]$Id,[string]$Name){
    $r=Release; $a=@($r.assets|Where-Object id -EQ $Id)|Select-Object -First 1
    Assert ($null -ne $a) "Asset $Id disappeared."; if($a.name -eq $Name){return}
    Assert ($null -eq (ByName $r $Name)) "Asset name $Name is taken."
    $result=Api "/releases/assets/$Id" 'PATCH' @{name=$Name}; Assert ($result.name -eq $Name) "Could not rename $Name"
}
function VerifyRelease{
    $r=Release; Assert ($r.tag_name -eq $ReleaseTag) 'The selected client release changed.'
    $a=ByName $r 'launcher.json'; $local=Join-Path $stage "launcher-server-v$Version.pending.json"
    Assert ($a.digest -eq 'sha256:'+(Hash $local)) 'Published manifest digest differs.'
    $download=Join-Path $stage 'verified-public-launcher.json'
    Invoke-WebRequest -Uri $a.browser_download_url -OutFile $download -TimeoutSec 60
    Assert ((Hash $download) -eq (Hash $local)) 'Public manifest bytes differ.'
    $m=Get-Content $download -Raw|ConvertFrom-Json; Assert ($m.server.version -eq $Version) 'Published server version differs.'
    foreach($key in @('launcher','runtime','game','depot_downloader')){
        Assert (($m.$key|ConvertTo-Json -Depth 20 -Compress) -eq ($old.$key|ConvertTo-Json -Depth 20 -Compress)) "Existing $key pins changed."
    }
    $zip=ByName $r "ReSkateServer-v$Version-Windows-x64.zip"; $exe=ByName $r 'ReSkateServer.exe'
    Assert ($zip.digest -eq 'sha256:'+$m.server.sha256 -and $zip.size -eq $m.server.size) 'Archive mismatch.'
    Assert ($exe.digest -eq 'sha256:'+$m.server.exe_sha256) 'Executable mismatch.'
    Assert ($null -ne (ByName $r "ReSkateServer-v$($old.server.version)-Windows-x64.zip")) 'Rollback archive disappeared.'
    $r|ConvertTo-Json -Depth 60|Set-Content (Join-Path $stage 'published-release.json') -Encoding utf8
    Write-Host "Verified public server $Version at $($r.html_url); client pins unchanged."
}
if($Mode -eq 'Prepare'){
    $r=Release; Assert ($r.tag_name -eq $ReleaseTag -and -not $r.draft -and -not $r.immutable) 'Unexpected or immutable release.'
    Assert ((Api '/commits/main').sha -eq $sha) 'Push the final source to main first.'
    $oldManifest=ByName $r 'launcher.json'; $oldExe=ByName $r 'ReSkateServer.exe'; $oldSums=ByName $r 'SHA256SUMS.txt'
    Assert ($oldManifest.digest -eq 'sha256:'+(Hash (Join-Path $root 'contrib/merge/server-release-base.json'))) 'Current manifest changed; refresh the baseline.'
    $uploads=Get-Content (Join-Path $stage 'UPLOADS.json') -Raw|ConvertFrom-Json; $assets=@{}
    foreach($e in $uploads.PSObject.Properties){$f=Join-Path $stage $e.Name; Assert ((Hash $f) -eq $e.Value.sha256) "Prepared $($e.Name) changed."; $assets[$e.Name]=Upload $r $e.Name $f}
    $sums=[ordered]@{}
    foreach($a in $r.assets){
        $name=$a.name
        if($name -eq 'launcher.json'){$name="launcher-server-v$($old.server.version).json"}
        elseif($name -eq 'ReSkateServer.exe'){$name="ReSkateServer-v$($old.server.version).exe"}
        elseif($name -eq 'SHA256SUMS.txt'){$name="SHA256SUMS-client-$ReleaseTag-server-v$($old.server.version).txt"}
        $sums[$name]=$a.digest.Substring(7)
    }
    foreach($e in $uploads.PSObject.Properties){
        $name=$e.Name
        if($name -eq "launcher-server-v$Version.pending.json"){$name='launcher.json'}
        elseif($name -eq "ReSkateServer-v$Version.exe"){$name='ReSkateServer.exe'}
        $sums[$name]=$e.Value.sha256
    }
    $sumsName="SHA256SUMS-server-v$Version.pending.txt"; $sumsFile=Join-Path $stage $sumsName
    ($sums.GetEnumerator()|Sort-Object Key|ForEach-Object{$_.Value+'  '+$_.Key})|Set-Content $sumsFile -Encoding ascii
    $assets[$sumsName]=Upload $r $sumsName $sumsFile
    @{release_id=$r.id; source_commit=$sha; assets=$assets; old_manifest=$oldManifest; old_exe=$oldExe; old_sums=$oldSums; old_body=$r.body}|
        ConvertTo-Json -Depth 60|Set-Content (Join-Path $stage 'prepared-release.json') -Encoding utf8
    Write-Host 'All new assets verified. Existing update manifest remains active.'
}elseif($Mode -eq 'Publish'){
    $p=Get-Content (Join-Path $stage 'prepared-release.json') -Raw|ConvertFrom-Json; $r=Release
    Assert ($r.id -eq $p.release_id -and $p.source_commit -eq $sha) 'Prepared release identity changed.'
    foreach($e in $p.assets.PSObject.Properties){$a=@($r.assets|Where-Object id -EQ $e.Value.id)|Select-Object -First 1; Assert ($a.digest -eq $e.Value.digest) "Remote $($e.Name) changed."}
    RenameAsset $p.old_exe.id "ReSkateServer-v$($old.server.version).exe"
    RenameAsset $p.assets."ReSkateServer-v$Version.exe".id 'ReSkateServer.exe'
    RenameAsset $p.old_sums.id "SHA256SUMS-client-$ReleaseTag-server-v$($old.server.version).txt"
    RenameAsset $p.assets."SHA256SUMS-server-v$Version.pending.txt".id 'SHA256SUMS.txt'
    RenameAsset $p.old_manifest.id "launcher-server-v$($old.server.version).json"
    try{RenameAsset $p.assets."launcher-server-v$Version.pending.json".id 'launcher.json'}
    catch{RenameAsset $p.old_manifest.id 'launcher.json'; throw 'Manifest publication failed; the previous manifest was restored.'}
    $body=(Get-Content (Join-Path $root 'contrib/merge/MSC_HUB_SERVER_RELEASE.md') -Raw)+"`n`n---`n`nPrevious client release notes (client files remain unchanged):`n`n"+$p.old_body
    $null=Api "/releases/$($r.id)" 'PATCH' @{name="MSC client v0.1.11 and server v$Version - MSC Hub plugin support"; body=$body}
    VerifyRelease
}else{VerifyRelease}
