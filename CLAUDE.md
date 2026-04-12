# claude-modern-status-bar

Native C status line for Claude Code on Windows. Single ~430-line source file,
compiles to a ~160 KB self-contained `.exe`. Renders in ~16 ms per invocation
(measured with PowerShell `Measure-Command`) — roughly 90× faster than the
original bash + python + git + awk pipeline it replaced.

## What it renders

```
📍 <folder>  ┃  🔀 <branch>  ┃  🤖 <model>  ┃  🧠 [██████░░░░░░░░░░░░░░░░░░░░░░] 65%  ┃  ⏰ 5h 23% · ♻ 4h 26m  ┃  📅 wk 41%
```

- **Folder**: basename of `current_dir`, bold blue, 📍 U+1F4CD
- **Branch**: current git branch (or short SHA if detached), color 80, 🔀 U+1F500
- **Model**: `display_name` from input JSON, color 141, 🤖 U+1F916
- **Context bar**: 30 chars wide, **filled = FREE space**, bold pink (color 213), 🧠 U+1F9E0
- **5-hour usage**: `rate_limits.five_hour.used_percentage` from input JSON, plus a `resets_at` countdown formatted as `47m` / `3h` / `1h 47m` / `<1m`. Color thresholds apply to BOTH the percentage and the countdown text: white (37) < 70%, yellow (220) at 70–89%, red (196) at 90%+. ⏰ U+23F0 (with VS16) for the percentage, ♻ U+267B (with VS16) for the countdown, separated by a gray middle dot · U+00B7 in color 245. The whole segment is omitted if `five_hour` is missing; the countdown half is omitted independently if `resets_at` is missing, zero, or in the past.
- **Weekly usage**: `rate_limits.seven_day.used_percentage`, same color thresholds, no countdown. 📅 U+1F4C5. Omitted if `seven_day` is missing.
- **Separator**: U+2503 `┃` in color 245

The bar shows *free* space, not used — matches what `/context` reports as
"Free space". Math accounts for the autocompact buffer Claude Code reserves
before auto-compaction fires (default **16.5 %** of the context window —
33 k tokens on a 200 k window, 165 k tokens on a 1 M extended-context window):

```
used_tokens    = input_tokens + cache_creation_input_tokens + cache_read_input_tokens
used_pct       = used_tokens / context_window_size × 100
free_pct       = round(100 − used_pct − autocompact_pct)
```

`used_tokens` matches Claude Code's internal `used_percentage` formula —
**input tokens only, `output_tokens` deliberately excluded** (per the
statusline docs). The autocompact percentage is taken from
`CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` if the env var is set, otherwise the
16.5 % default. Expressing it as a percentage (not a fixed token count)
is load-bearing for extended-context models — the earlier 32768-token
constant was off by ~13 pp on a 1 M window.

## Files

| File                 | Purpose                                                   |
|----------------------|-----------------------------------------------------------|
| `statusline.c`       | Entire implementation. No deps beyond libc.               |
| `build.bat`          | Compile with MSVC 2022 (`cl /O2 /MT`). Output: `statusline.exe`. |
| `deploy.bat`         | Build + copy `statusline.exe` to `%USERPROFILE%\.claude\`.|
| `test/mock.json`     | Sample statusline JSON input for smoke testing.           |
| `test/run-test.bat`  | Pipe `mock.json` into the built binary and print output.  |

## Build

```
build.bat
```

Requires MSVC 2022 Community (`cl.exe`). The script calls `vcvars64.bat`
automatically, so you do not need a "Developer Command Prompt" — a plain
`cmd.exe` or Git Bash works.

## Deploy

```
deploy.bat
```

Builds, then copies `statusline.exe` into `%USERPROFILE%\.claude\statusline.exe`.
Claude Code's `settings.json` already points there:

```json
"statusLine": {
  "type": "command",
  "command": "C:/Users/<your-username>/.claude/statusline.exe"
}
```

No restart needed — Claude Code spawns the binary fresh on each render.

## Test

```
test\run-test.bat
```

Pipes `test/mock.json` into `statusline.exe` and prints the rendered line.
If the output looks right (colors, emojis, bar), the binary is healthy.

## Design notes (why it's fast)

1. **No fork to `git`.** `find_git_branch()` walks up the directory tree
   reading `.git/HEAD` directly. Also handles worktrees (where `.git` is a
   file containing `gitdir: <path>`). Saves ~280 ms per render.
2. **No JSON parser.** `json_find_value()` is a `strstr`-based key search;
   the `":` suffix on the needle is enough to avoid false matches against
   `"total_input_tokens":` when looking for `"input_tokens":`. The
   rate-limit lookups are scoped per-window first (`strstr(input,
   "\"five_hour\"")`, then look up `used_percentage` inside) so they can't
   collide with `context_window.used_percentage`.
3. **Static CRT (`/MT`).** No DLL load at startup — the binary is self
   contained. Removes ~50-80 ms on cold starts.
4. **Raw UTF-8 byte literals.** Emojis and box-drawing chars are written as
   `"\xf0\x9f\x93\x8d"`-style escapes so the source compiles as plain ASCII
   and the output is already valid UTF-8 for Windows Terminal.
5. **`_setmode(_fileno(stdout), _O_BINARY)`** prevents the CRT from
   translating `\n` → `\r\n` in the UTF-8 byte stream.

## Input JSON schema (what Claude Code pipes in on stdin)

Only the fields we actually read:

```json
{
  "model": { "display_name": "Opus 4.6" },
  "workspace": { "current_dir": "C:\\example\\demo" },
  "context_window": {
    "context_window_size": 200000,
    "used_percentage": 18,
    "current_usage": {
      "input_tokens": 5000,
      "cache_creation_input_tokens": 10000,
      "cache_read_input_tokens": 21000
    }
  },
  "rate_limits": {
    "five_hour": { "used_percentage": 23, "resets_at": 1744387200 },
    "seven_day": { "used_percentage": 41 }
  }
}
```

Nested structure is fine — the `strstr` scan finds keys anywhere in the blob.
The `rate_limits` block and either window inside it are optional; missing
fields just drop the corresponding segment from the rendered line.
`resets_at` is a Unix epoch in seconds.

## Performance history

| Stage                                        | Time per render |
|----------------------------------------------|-----------------|
| Original (bash wrapper + python + git + awk) | ~1500 ms        |
| All-in-one Python                            | ~420 ms         |
| `python -SE` (skip `site.py` + env scan)     | ~111 ms         |
| **Native C (this repo)**                     | **~16 ms**      |

Timings measured with PowerShell `Measure-Command` piping `test/mock.json`.
Bash benchmarks add ~80-130 ms of Git Bash fork overhead and are misleading —
Claude Code spawns the binary via Win32 directly, so PowerShell numbers are
the real cost.

## Changing the look

Edit `statusline.c` and rebuild. The interesting knobs:

- **Colors**: search for `\x1b[38;5;` — 256-color ANSI codes. 34=blue,
  141=purple, 80=cyan-green, 213=pink (brain bar), 245=gray, 37=white,
  220=yellow (rate-limit warn), 196=red (rate-limit crit).
- **Rate-limit thresholds**: the `if (pct >= 90) ... else if (pct >= 70)`
  ladders in each rate-limit render block. Edit both blocks (5-hour and
  weekly) if you want them to stay in sync.
- **Bar width**: `enum { BAR_W = 30 };` near the bottom of `main()`.
- **Bar glyphs**: `U+2588` (█ full) and `U+2591` (░ light shade).
- **Separator**: `static const char SEP[] = ...` — currently `┃`.
- **Emojis**: the raw UTF-8 byte sequences in the `fputs` calls at the
  bottom of `main()`. Comments name the codepoints.
- **Autocompact reserve**: `double autocompact_pct = 16.5;` — a
  percentage of `context_window_size`, honors the
  `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` env var. Do **not** switch back to
  a fixed token count: it breaks 1 M extended-context windows.

After any edit: `deploy.bat`, then open a new Claude Code session (or just
type anything — the next render will use the new binary).
