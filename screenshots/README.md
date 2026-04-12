# Screenshots

This directory holds the screenshot referenced from the project README.

## What to capture

A screenshot of the rendered status bar in Windows Terminal, showing:

- Folder name
- Git branch (run inside any git repo)
- Model name
- The full context bar with a meaningful free percentage (around 60–80% free is ideal so both the filled and unfilled portions of the bar are visible)
- The 5-hour and weekly rate-limit segments (only render when Claude Code is actively passing `rate_limits` in the input JSON)

## Save as

`screenshots/statusbar.png`

The main `README.md` references this exact path. After capturing, the README image will render automatically on GitHub.

## Suggested capture method

Windows Snipping Tool (`Win + Shift + S`) → save as PNG. Trim to just the status bar line; no need to include the surrounding terminal chrome.
