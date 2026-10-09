param([Parameter(Mandatory=$true)][string]$Executable,
      [Parameter(Mandatory=$true)][string]$OutputDirectory)
# Run with Windows PowerShell: UI Automation uses desktop .NET assemblies.
# The driver targets only its own child PID and never sends global key presses.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NativeDialogButton {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool PostMessage(IntPtr hwnd, uint msg, UIntPtr wp, IntPtr lp);
}
'@
$directory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $directory) { throw 'Choose a new artifact directory.' }
New-Item -ItemType Directory -Path $directory | Out-Null
$env:QT_QPA_PLATFORM = 'windows'
foreach ($mode in @('open', 'save', 'cancel-open', 'cancel-save', 'dll-open', 'cancel-dll')) {
    $probe = Start-Process -FilePath $Executable -ArgumentList ('"' + $directory + '" ' + $mode) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$directory/$mode-stdout.log" -RedirectStandardError "$directory/$mode-stderr.log"
    $probeHandle = $probe.Handle # Keep the process handle so ExitCode survives UIA polling.
    try {
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $dialog = $null
        while ($clock.ElapsedMilliseconds -lt 20000 -and -not $probe.HasExited) {
            $handleFile = "$directory/$mode.hwnd"
            if ((Test-Path -LiteralPath $handleFile) -and (Test-Path -LiteralPath "$directory/$mode.buttons")) {
                $nativeHandle = [IntPtr]::new([int64](Get-Content -LiteralPath $handleFile -Raw))
                $ownedDialog = [System.Windows.Automation.AutomationElement]::FromHandle($nativeHandle)
                if ($ownedDialog.Current.ProcessId -eq $probe.Id) { $dialog = $ownedDialog; break }
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $dialog) { throw "Native dialog unavailable: $mode" }
        # Qt 5 may expose native buttons as UIA panes without InvokePattern.
        # Use the exact native control handle, with no foreground/global input.
        $buttons = Get-Content -LiteralPath "$directory/$mode.buttons"
        $buttonHandle = [IntPtr]::new([int64]$buttons[$(if ($mode.StartsWith('cancel')) { 1 } else { 0 })])
        if (-not [NativeDialogButton]::PostMessage($buttonHandle, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero)) { throw "Native button unavailable: $mode" }
        if (-not $probe.WaitForExit(10000)) { throw 'Native dialog probe timed out.' }
        Get-Content -LiteralPath "$directory/$mode-stdout.log" -Encoding UTF8
        if ($probe.ExitCode -ne 0) { throw "Native dialog probe failed: $($probe.ExitCode)" }
    } finally {
        if (-not $probe.HasExited) { $probe.Kill(); $probe.WaitForExit() }
    }
}
