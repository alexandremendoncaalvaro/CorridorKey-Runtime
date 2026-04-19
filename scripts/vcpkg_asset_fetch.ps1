<#
.SYNOPSIS
    vcpkg asset-cache script: download a source tarball, rewriting
    known-blocked upstream URLs to documented mirrors.

.DESCRIPTION
    Invoked by vcpkg via `X_VCPKG_ASSET_SOURCES=x-script,...` during the
    ORT RTX source build. vcpkg passes the expected URL, SHA512, and
    destination path; this script is responsible for producing a file at
    $Dst whose SHA512 matches $Sha512. vcpkg validates the hash after
    return, so any mirror we use must ship byte-identical content.

    Currently handles:

      * gitlab.com/libeigen/eigen -> github.com/eigen-mirror/eigen
        The gitlab archive URL now sits behind a Cloudflare bot
        challenge (HTTP 403) for many non-browser clients. upstream
        onnxruntime migrated its own FetchContent path to the same
        GitHub mirror for this reason -- see
        https://github.com/microsoft/onnxruntime/issues/24861. The
        mirror is maintained by the Eigen team and serves the same git
        object under `eigen-mirror/eigen`, so the tarball bytes match.

    For every other URL the script passes through to the upstream via
    `Invoke-WebRequest` so vcpkg keeps its normal fetch behavior for
    packages that are not gitlab-blocked.

    Exit code 0 on success (file at $Dst, vcpkg verifies SHA).
    Non-zero exit tells vcpkg to fall back to its default download.

    Asset caching reference:
    https://learn.microsoft.com/en-us/vcpkg/users/assetcaching
#>

param(
    [Parameter(Mandatory=$true)][string]$Url,
    [Parameter(Mandatory=$true)][string]$Sha512,
    [Parameter(Mandatory=$true)][string]$Dst
)

$ErrorActionPreference = "Stop"

function Invoke-DownloadWithRetry {
    param(
        [string]$SourceUrl,
        [string]$DestinationPath,
        [int]$MaxAttempts = 3
    )

    $parent = Split-Path -Parent $DestinationPath
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $attempt = 0
    while ($true) {
        $attempt++
        try {
            Invoke-WebRequest -Uri $SourceUrl -OutFile $DestinationPath -UseBasicParsing -MaximumRedirection 10
            return
        } catch {
            if ($attempt -ge $MaxAttempts) {
                throw "Download failed after $MaxAttempts attempts: $SourceUrl ($($_.Exception.Message))"
            }
            Start-Sleep -Seconds ([Math]::Min(30, [Math]::Pow(2, $attempt)))
        }
    }
}

$gitlabEigenPattern = '^https://gitlab\.com/libeigen/eigen/-/archive/(?<sha>[0-9a-fA-F]{40})/eigen-\k<sha>\.tar\.gz$'
$match = [Regex]::Match($Url, $gitlabEigenPattern)

if ($match.Success) {
    $commitSha = $match.Groups['sha'].Value
    $mirrorUrl = "https://codeload.github.com/eigen-mirror/eigen/tar.gz/$commitSha"
    Write-Host "[vcpkg-asset] Rewriting gitlab.com/libeigen/eigen -> github.com/eigen-mirror/eigen for commit $commitSha"
    Invoke-DownloadWithRetry -SourceUrl $mirrorUrl -DestinationPath $Dst
    exit 0
}

Write-Host "[vcpkg-asset] Fetching (passthrough) $Url"
Invoke-DownloadWithRetry -SourceUrl $Url -DestinationPath $Dst
exit 0
