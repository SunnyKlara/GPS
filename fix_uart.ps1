$lines = Get-Content 'fw-AC63_BT_SDK/cpu/bd19/uart_dev.c'
Write-Host "Total lines: $($lines.Count)"

# Find the line that says "    return 0;" at the end of uart_config
# and delete all following lines until the next function comment block
$newLines = New-Object System.Collections.ArrayList
$i = 0
$foundEnd = $false

while ($i -lt $lines.Count) {
    $line = $lines[$i]
    
    if ($foundEnd -and $line -match '^\s*/\*\*$') {
        # Reached next function comment block
        [void]$newLines.Add($line)
        $foundEnd = $false
        $i++
        continue
    }
    
    if ($foundEnd) {
        # Skip this garbage line
        $i++
        continue
    }
    
    [void]$newLines.Add($line)
    
    # Check if this is the "    return 0;" line followed by "}" on next line
    if ($line -match '^\s+return 0;\s*$' -and $i + 1 -lt $lines.Count -and $lines[$i+1] -match '^\s*\}\s*$') {
        $foundEnd = $true
        Write-Host "Found end at line $($i+1), skipping garbage..."
    }
    
    $i++
}

Set-Content -Path 'fw-AC63_BT_SDK/cpu/bd19/uart_dev.c' -Value $newLines
Write-Host "Done. New line count: $($newLines.Count)"
