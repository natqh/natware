param([Parameter(Mandatory=$true)][string]$BinaryPath)
$ErrorActionPreference = 'Stop'
if (-not ('NatwareEditorResourceReader' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class NatwareEditorResourceReader {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr FindResourceW(IntPtr module, string name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    public static byte[] Read(string path, string name) {
        // DATAFILE | IMAGE_RESOURCE maps resources without running any executable code.
        IntPtr module = LoadLibraryExW(path, IntPtr.Zero, 0x02 | 0x20);
        if (module == IntPtr.Zero) throw new Win32Exception();
        try {
            IntPtr resource = FindResourceW(module, name, new IntPtr(10));
            if (resource == IntPtr.Zero) throw new Win32Exception();
            uint size = SizeofResource(module, resource);
            IntPtr bytes = LockResource(LoadResource(module, resource));
            if (bytes == IntPtr.Zero) throw new Win32Exception();
            byte[] result = new byte[size]; Marshal.Copy(bytes, result, 0, result.Length); return result;
        } finally { FreeLibrary(module); }
    }
}
'@
}
$resolvedBinary = (Resolve-Path -LiteralPath $BinaryPath).Path
$assetRoot = Join-Path $PSScriptRoot '..\natware\src\features\editor\assets'
$resources = @{
    EDITOR_HTML='editor.html'; DIAGNOSTICS_JS='diagnostics.js'; WORKSPACE_JS='workspace.js';
    WORKSPACE_CSS='workspace.css'; DARK_VEIL_JS='dark-veil.js'; GLASS_JS='glass.js'; BRAND_TTF='brand.ttf'
}
foreach($resource in $resources.GetEnumerator()) {
    $embedded = [NatwareEditorResourceReader]::Read($resolvedBinary, $resource.Key)
    $embeddedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($embedded))
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $assetRoot $resource.Value)).Hash
    if ($sourceHash -ne $embeddedHash) { throw "Embedded $($resource.Value) differs from the current source." }
    Write-Output "PASS packaged $($resource.Value) matches source ($($embedded.Length) bytes)"
}
