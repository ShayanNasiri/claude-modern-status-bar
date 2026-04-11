# Changelog

All notable changes to claude-modern-status-bar are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/).

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
