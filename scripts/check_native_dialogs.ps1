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
    public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, UIntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
}
'@
$directory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $directory) { throw 'Choose a new artifact directory.' }
New-Item -ItemType Directory -Path $directory | Out-Null
$env:QT_QPA_PLATFORM = 'windows'
foreach ($mode in @('open', 'save', 'cancel-open', 'cancel-save')) {
    $probe = Start-Process -FilePath $Executable -ArgumentList ('"' + $directory + '" ' + $mode) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$directory/$mode-stdout.log" -RedirectStandardError "$directory/$mode-stderr.log"
    $probeHandle = $probe.Handle # Keep the process handle so ExitCode survives UIA polling.
    try {
        $processCondition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $probe.Id)
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $dialog = $null
        while ($clock.ElapsedMilliseconds -lt 20000 -and -not $probe.HasExited) {
            $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $processCondition)
            foreach ($window in $windows) {
                if ($window.Current.Name.EndsWith("[probe:$mode]")) { $dialog = $window; break }
            }
            if ($dialog -and (Test-Path -LiteralPath "$directory/$mode.png")) { break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $dialog) { throw "Native dialog unavailable: $mode" }
        $buttonId = if ($mode.StartsWith('cancel')) { '2' } else { '1' }
        $idCondition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $buttonId)
        $button = $dialog.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $idCondition)
        if (-not $button) {
            $dialog.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object {
                "$($_.Current.AutomationId) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.Name) | $($_.GetCurrentPropertyValue([System.Windows.Automation.AutomationElement]::IsInvokePatternAvailableProperty))"
            } | Set-Content -LiteralPath "$directory/controls.txt" -Encoding UTF8
            throw "Native button unavailable: $mode"
        }
        # Qt 5 may expose native buttons as UIA panes without InvokePattern.
        # Use the exact native control handle, with no foreground/global input.
        $messageResult = [UIntPtr]::Zero
        $sent = [NativeDialogButton]::SendMessageTimeout([IntPtr]$button.Current.NativeWindowHandle, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero, 2, 2000, [ref]$messageResult)
        if ($sent -eq [IntPtr]::Zero) { throw "Native button timed out: $mode" }
        if (-not $probe.WaitForExit(10000)) { throw 'Native dialog probe timed out.' }
        Get-Content -LiteralPath "$directory/$mode-stdout.log" -Encoding UTF8
        if ($probe.ExitCode -ne 0) { throw "Native dialog probe failed: $($probe.ExitCode)" }
    } finally {
        if (-not $probe.HasExited) { $probe.Kill(); $probe.WaitForExit() }
    }
}
