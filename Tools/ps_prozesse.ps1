# Zeigt Unreal-/cmd-Prozesse UND alles, was nach Gate/Build/Python aussieht -
# um die push_gate-Kette zu identifizieren, die den Engine-Run-Lock haelt.
Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -like "*Unreal*" -or $_.Name -eq "cmd.exe" -or $_.Name -eq "powershell.exe" -or
        $_.Name -eq "dotnet.exe" -or $_.Name -eq "python.exe" -or $_.Name -eq "py.exe" -or
        ($_.CommandLine -and ($_.CommandLine -match "gate|push|RunUAT|build_release|UbaServer"))
    } |
    ForEach-Object {
        $cmd = if ($_.CommandLine) { $_.CommandLine } else { "(keine)" }
        "{0} | {1} | {2}" -f $_.ProcessId, $_.CreationDate, $cmd.Substring(0, [Math]::Min(200, $cmd.Length))
    }
"---JETZT: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')---"
