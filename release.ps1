# Publishes one natware component on its own: sets its version, builds it, and puts it on GitHub
# with its SHA-256. Every natware that's open or starts after this offers it.
#
#   .\release.ps1 natware 1.0.3 "Fixed the triggerbot"    tag v1.0.3        (natware.exe)
#   .\release.ps1 editor  1.0.1 "Faster tabs"             tag editor-v1.0.1 (editor.exe)
#   .\release.ps1 nexec   1.0.1 "Backend fixes"           tag nexec-v1.0.1  (nexec.exe)
#   .\release.ps1 1.0.3 "notes"                           the old form: natware
#
# natware keeps its plain "v" tags and is marked the repository's latest release, so natware
# versions from before the split (which only look at the latest release) still update into the
# launcher. The editor and nexec are never marked latest.
#
# nexec is built with cargo in its own project; this only publishes it. Pass -NexecExe when it
# isn't at ..\nexec\target\release\nexec.exe beside this folder.
#
# Needs the GitHub CLI (gh) logged in to an account that can write to the repository.
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Component,
    [Parameter(Position = 1)][string]$Version,
    [Parameter(Position = 2)][string]$Notes = "",
    [string]$Repo = "natqh/natware",
    [string]$NexecExe = ""
)
$ErrorActionPreference = "Stop"

# The old form: .\release.ps1 <version> "notes"
if ($Component -match '^\d') {
    $Notes = $Version
    $Version = $Component
    $Component = "natware"
}
if (-not $Notes) { $Notes = "" }
$Component = $Component.ToLower()
if ($Component -notin @("natware", "editor", "nexec")) { throw "Component must be natware, editor or nexec (got '$Component')." }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 1.2.3 (got '$Version')." }

$build = Join-Path $PSScriptRoot "natware\build"
switch ($Component) {
    "natware" {
        $tag = "v$Version"
        $project = Join-Path $PSScriptRoot "natware\natware.vcxproj"
        $versionFile = Join-Path $PSScriptRoot "natware\src\version.h"
        $macro = "NATWARE_VERSION"
        $exe = Join-Path $build "natware.exe"
    }
    "editor" {
        $tag = "editor-v$Version"
        $project = Join-Path $PSScriptRoot "editor\editor.vcxproj"
        $versionFile = Join-Path $PSScriptRoot "editor\version.h"
        $macro = "EDITOR_VERSION"
        $exe = Join-Path $build "editor.exe"
    }
    "nexec" {
        $tag = "nexec-v$Version"
        $exe = if ($NexecExe) { $NexecExe } else { Join-Path $PSScriptRoot "..\nexec\target\release\nexec.exe" }
    }
}

# Newer than every release of this component already out, or nobody would be offered it.
$prefix = $tag.Substring(0, $tag.Length - $Version.Length)
$released = gh release list --repo $Repo --limit 200 --json tagName --jq '.[].tagName'
if ($LASTEXITCODE -ne 0) { throw "gh couldn't list the releases of $Repo." }
foreach ($t in $released) {
    if ($t.StartsWith($prefix) -and $t.Substring($prefix.Length) -match '^\d+(\.\d+)*$') {
        if ([version]$Version -le [version]$t.Substring($prefix.Length)) { throw "$Component $Version isn't newer than the released $t." }
    }
}

if ($Component -ne "nexec") {
    # The version in the source moves with the release; a failed build puts it back.
    $source = Get-Content $versionFile -Raw
    $current = [regex]::Match($source, "#define $macro `"([^`"]+)`"").Groups[1].Value
    if ([version]$Version -le [version]$current) { throw "Version $Version isn't newer than $current in $versionFile." }
    $rc = ($Version.Split('.') + '0') -join ','
    # Written without a BOM: the resource compiler reads this header too.
    $setVersion = {
        param($v, $r)
        $text = (Get-Content $versionFile -Raw) -replace "#define $macro `"[^`"]+`"", "#define $macro `"$v`"" `
            -replace "#define ${macro}_RC [\d,]+", "#define ${macro}_RC $r"
        [IO.File]::WriteAllText($versionFile, $text, (New-Object System.Text.UTF8Encoding($false)))
    }
    $currentRc = [regex]::Match($source, "#define ${macro}_RC ([\d,]+)").Groups[1].Value

    $msbuild = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter MSBuild.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\amd64\\MSBuild\.exe$' } | Select-Object -First 1 -ExpandProperty FullName
    if (-not $msbuild) { throw "MSBuild wasn't found." }

    # A running exe can't be overwritten by the build, but it can be renamed.
    if (Test-Path $exe) {
        $aside = [IO.Path]::ChangeExtension($exe, ".prev.exe")
        Remove-Item $aside -ErrorAction SilentlyContinue
        Rename-Item $exe ([IO.Path]::GetFileName($aside))
    }

    & $setVersion $Version $rc
    # Each component builds alone (natware's reference to the editor is skipped).
    & $msbuild $project /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$(Join-Path $PSScriptRoot 'natware')\" /p:BuildProjectReferences=false /m /nologo /v:m
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
        & $setVersion $current $currentRc
        throw "The build failed; $Component is back to $current."
    }
}
elseif (-not (Test-Path $exe)) {
    throw "nexec.exe wasn't found at $exe. Build it with cargo build --release, or pass -NexecExe."
}

$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
$body = if ($Notes) { "$Notes`n`nsha256: $hash" } else { "sha256: $hash" }
$latest = if ($Component -eq "natware") { "--latest" } else { "--latest=false" }
# The asset is named after the file (natware.exe, editor.exe, nexec.exe): that's what natware looks for.
gh release create $tag $exe --repo $Repo --title "$Component $Version" --notes $body $latest
if ($LASTEXITCODE -ne 0) { throw "gh couldn't create the release." }
Write-Host "Released $Component $Version as $tag ($hash)."
