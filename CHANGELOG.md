# Changelog

All notable changes to claude-modern-status-bar are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- **5-hour and weekly rate-limit segments.** Reads `rate_limits.five_hour.used_percentage` and `rate_limits.seven_day.used_percentage` from Claude Code's input JSON and renders each as its own ┃-bracketed segment with a clock or calendar icon. Color thresholds apply to both: white below 70%, yellow at 70–89%, red at 90%+. Either segment is omitted if the corresponding window is missing from the JSON.
- **5-hour reset countdown.** Reads `rate_limits.five_hour.resets_at` (Unix epoch) and appends a human-readable countdown after a gray middle dot — formats as `47m` / `3h` / `1h 47m` / `<1m`. Inherits the same color threshold as the percentage. Omitted independently if `resets_at` is missing, zero, or already in the past.

### Changed

- **Brain context bar color is now bold pink (256-color 213)** instead of bold white, so it doesn't visually merge with the white-by-default rate-limit segments to its right.

## [1.0.0] — 2026-04-11

Initial public release.

### Features

- Native C status line for Claude Code on Windows.
- Renders folder, git branch, model, and free-context bar in a single line.
- ~21 ms render time (PowerShell `Measure-Command`), under the ~30 ms keystroke-lag ceiling.
- Single self-contained `.exe`, ~160 KB, no DLL dependencies (`/MT` static CRT).
- 30-character bar showing **free space remaining** — matches `/context`'s "Free space" row.
- Walks `.git/HEAD` directly instead of forking `git` (saves ~280 ms per render).
- Worktree-aware: handles `.git` files containing `gitdir: <path>`.
- Honors `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` to override the default 16.5% autocompact reserve, matching the env var Claude Code itself reads.
- Autocompact reserve expressed as a percentage (not a fixed token count), so the math is correct on both 200k and 1M extended-context windows.
