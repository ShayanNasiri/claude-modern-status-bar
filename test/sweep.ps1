# Exhaustive sweep: for a range of (window_size, used_percentage) inputs,
# compare what statusline.exe prints to what /context would show.
#
# /context formula: free% = 100 - used% - (33000 / window_size * 100), clamped at 0.
# Binary should match within 1 pp (rounding).

$cases = @(
    # 200k window — primary concern for this test
    @{ size=200000;  used=0  },
    @{ size=200000;  used=5  },
    @{ size=200000;  used=10 },
    @{ size=200000;  used=18 },
    @{ size=200000;  used=25 },
    @{ size=200000;  used=40 },
    @{ size=200000;  used=50 },
    @{ size=200000;  used=60 },
    @{ size=200000;  used=70 },
    @{ size=200000;  used=75 },
    @{ size=200000;  used=80 },
    @{ size=200000;  used=83 },  # near autocompact trigger
    @{ size=200000;  used=84 },  # should show 0% free
    @{ size=200000;  used=90 },  # past trigger, clamped
    # 1M window — sanity check
    @{ size=1000000; used=0  },
    @{ size=1000000; used=7  },
    @{ size=1000000; used=25 },
    @{ size=1000000; used=50 },
    @{ size=1000000; used=90 },
    @{ size=1000000; used=96 },
    # Hypothetical 500k — proves dynamic detection, not just two hardcoded sizes
    @{ size=500000;  used=20 },
    @{ size=500000;  used=50 }
)

$results = @()
foreach ($c in $cases) {
    $size = $c.size
    $used = $c.used
    $rem  = 100 - $used

    # Expected free% per /context formula
    $autocompact = 33000.0 / $size * 100.0
    $expected = [math]::Max(0, [math]::Round(100 - $used - $autocompact))

    $json = '{"model":{"display_name":"Opus"},"workspace":{"current_dir":"C:\\x"},"context_window":{"context_window_size":' + $size + ',"used_percentage":' + $used + ',"remaining_percentage":' + $rem + '}}'
    $raw  = $json | & '.\statusline.exe'

    # Extract the "NN%" that follows the brain glyph
    $actual = $null
    if ($raw -match '\] (\d+)%') { $actual = [int]$matches[1] }

    $drift = if ($actual -ne $null) { [math]::Abs($actual - $expected) } else { 'N/A' }
    $pass  = if ($actual -ne $null -and $drift -le 1) { 'OK' } else { 'FAIL' }

    $results += [pscustomobject]@{
        Window   = '{0,-7}' -f ($size.ToString())
        Used     = '{0,3}%' -f $used
        Expected = '{0,3}%' -f $expected
        Binary   = if ($actual -ne $null) { '{0,3}%' -f $actual } else { 'N/A' }
        Drift    = '{0,3}pp' -f $drift
        Status   = $pass
    }
}

$results | Format-Table -AutoSize

$failures = @($results | Where-Object { $_.Status -eq 'FAIL' })
if ($failures.Count -gt 0) {
    Write-Output ""
    Write-Output "FAILURES: $($failures.Count) / $($results.Count)"
    exit 1
} else {
    Write-Output ""
    Write-Output "All $($results.Count) cases within 1 pp of /context."
}
