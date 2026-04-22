/*
 * Claude Code status line — native C implementation.
 *
 * Compiled with MSVC. ~250 LOC. Single binary, no DLL deps when built /MT.
 * Cold start should be ~10-30ms vs Python's ~210ms.
 *
 * Reads statusline JSON from stdin. Hand-rolled JSON extraction (only the
 * fields we care about). Walks the filesystem looking for .git/HEAD instead
 * of forking git. Writes UTF-8 ANSI-colored output to stdout.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <time.h>

/* ----------- JSON helpers (good enough for our known schema) ----------- */

/* Find "key": and return pointer to first byte of value (after colon+ws). */
static const char *json_find_value(const char *src, const char *key) {
    char needle[128];
    int n = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return NULL;
    const char *p = strstr(src, needle);
    if (!p) return NULL;
    p += n;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Extract a string value, unescaping common JSON escapes. Returns 1 on hit. */
static int json_get_string(const char *src, const char *key,
                           char *dst, size_t dst_size) {
    const char *v = json_find_value(src, key);
    if (!v || *v != '"') return 0;
    v++;
    size_t i = 0;
    while (*v && *v != '"' && i + 1 < dst_size) {
        if (*v == '\\' && v[1]) {
            v++;
            char c;
            switch (*v) {
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                case '/':  c = '/';  break;
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case 'r':  c = '\r'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                default:   c = *v;   break;
            }
            dst[i++] = c;
            v++;
        } else {
            dst[i++] = *v++;
        }
    }
    dst[i] = 0;
    return 1;
}

/* Extract an integer value, returning default_val if missing/non-numeric. */
static long long json_get_int(const char *src, const char *key,
                              long long default_val) {
    const char *v = json_find_value(src, key);
    if (!v) return default_val;
    if (*v == 'n' || *v == 't' || *v == 'f' ||
        *v == '"' || *v == '{' || *v == '[') return default_val;
    char *end;
    long long n = strtoll(v, &end, 10);
    if (end == v) return default_val;
    return n;
}

/* Extract a floating-point value — used for fields that Claude Code may
 * serialize as a float (e.g. used_percentage = 16.7). strtoll would
 * truncate at the decimal point and lose sub-percent precision. */
static double json_get_double(const char *src, const char *key,
                              double default_val) {
    const char *v = json_find_value(src, key);
    if (!v) return default_val;
    if (*v == 'n' || *v == 't' || *v == 'f' ||
        *v == '"' || *v == '{' || *v == '[') return default_val;
    char *end;
    double d = strtod(v, &end);
    if (end == v) return default_val;
    return d;
}

/* ----------- stdin reader ----------- */

static char *read_all_stdin(void) {
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    _setmode(_fileno(stdin), _O_BINARY);
    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = 0;
    return buf;
}

/* ----------- Git branch (read .git/HEAD directly, no fork) ----------- */

static int read_first_line(const char *path, char *out, size_t out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fgets(out, (int)out_size, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    size_t l = strlen(out);
    while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = 0;
    return 1;
}

static void parse_head(const char *head_buf, char *branch, size_t branch_size) {
    static const char prefix[] = "ref: refs/heads/";
    size_t pl = sizeof(prefix) - 1;
    if (strncmp(head_buf, prefix, pl) == 0) {
        strncpy(branch, head_buf + pl, branch_size - 1);
        branch[branch_size - 1] = 0;
    } else {
        /* detached HEAD: short SHA (first 7 chars) */
        size_t copy_len = strlen(head_buf);
        if (copy_len > 7) copy_len = 7;
        if (copy_len >= branch_size) copy_len = branch_size - 1;
        memcpy(branch, head_buf, copy_len);
        branch[copy_len] = 0;
    }
}

static int find_git_branch(const char *cwd, char *branch, size_t branch_size) {
    if (!cwd || !*cwd) return 0;
    char dir[1024];
    strncpy(dir, cwd, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;

    /* Strip trailing slashes. */
    size_t l = strlen(dir);
    while (l > 1 && (dir[l-1] == '/' || dir[l-1] == '\\')) dir[--l] = 0;

    char head_path[1200];
    char gitpath[1100];
    char head_buf[512];

    int max_iter = 64; /* safety */
    while (max_iter-- > 0) {
        snprintf(head_path, sizeof(head_path), "%s/.git/HEAD", dir);
        if (read_first_line(head_path, head_buf, sizeof(head_buf))) {
            parse_head(head_buf, branch, branch_size);
            return 1;
        }

        /* Worktree case: .git is a file with "gitdir: <path>" */
        snprintf(gitpath, sizeof(gitpath), "%s/.git", dir);
        FILE *f = fopen(gitpath, "rb");
        if (f) {
            char line[1024];
            int got = fgets(line, sizeof(line), f) != NULL;
            fclose(f);
            if (got) {
                size_t ll = strlen(line);
                while (ll > 0 && (line[ll-1] == '\n' || line[ll-1] == '\r')) line[--ll] = 0;
                if (strncmp(line, "gitdir:", 7) == 0) {
                    const char *gd = line + 7;
                    while (*gd == ' ' || *gd == '\t') gd++;
                    char wt_head[1200];
                    snprintf(wt_head, sizeof(wt_head), "%s/HEAD", gd);
                    if (read_first_line(wt_head, head_buf, sizeof(head_buf))) {
                        parse_head(head_buf, branch, branch_size);
                        return 1;
                    }
                }
            }
            return 0;
        }

        /* Walk up one directory. */
        char *last = NULL;
        for (char *p = dir; *p; p++) {
            if (*p == '/' || *p == '\\') last = p;
        }
        if (!last) return 0;
        /* Stop at drive root (e.g. "C:/") */
        if (last - dir <= 2 && dir[1] == ':') return 0;
        *last = 0;
    }
    return 0;
}

/* ----------- Main ----------- */

int main(void) {
    _setmode(_fileno(stdout), _O_BINARY);

    char *input = read_all_stdin();
    if (!input) return 0;

    char cwd[1024] = {0};
    char model[256] = "unknown";
    if (!json_get_string(input, "current_dir", cwd, sizeof(cwd)))
        json_get_string(input, "cwd", cwd, sizeof(cwd));
    json_get_string(input, "display_name", model, sizeof(model));

    /* Scope context_window lookups to the object itself so we never collide
     * with identically-named fields elsewhere in the payload — notably
     * rate_limits.five_hour.used_percentage / rate_limits.seven_day.used_percentage. */
    const char *cw = strstr(input, "\"context_window\"");
    if (!cw) cw = input;

    long long size           = json_get_int(cw, "context_window_size", 200000);
    double    used_pct_field = json_get_double(cw, "used_percentage", -1.0);
    double    rem_pct_field  = json_get_double(cw, "remaining_percentage", -1.0);
    long long input_tokens   = json_get_int(cw, "input_tokens", 0);
    long long cache_creation = json_get_int(cw, "cache_creation_input_tokens", 0);
    long long cache_read     = json_get_int(cw, "cache_read_input_tokens", 0);
    long long used_tokens    = input_tokens + cache_creation + cache_read;

    /* Rate-limit usage — same numbers /usage shows. Scoped per-window so the
     * "used_percentage" lookup can't accidentally hit context_window's field
     * (or the wrong rate-limit window). Both windows are optional, and the
     * 5-hour reset countdown is independently optional — anything missing
     * is just omitted from the rendered line. */
    double    rl_5h_pct    = -1.0;
    double    rl_wk_pct    = -1.0;
    long long rl_5h_resets = 0;
    const char *rl = strstr(input, "\"rate_limits\"");
    if (rl) {
        const char *fh = strstr(rl, "\"five_hour\"");
        if (fh) {
            rl_5h_pct    = json_get_double(fh, "used_percentage", -1.0);
            rl_5h_resets = json_get_int(fh, "resets_at", 0);
        }
        const char *sd = strstr(rl, "\"seven_day\"");
        if (sd) rl_wk_pct = json_get_double(sd, "used_percentage", -1.0);
    }

    /* Format the 5-hour reset countdown into a compact human-readable string:
     *     <1m  |  47m  |  3h  |  1h 47m
     * Empty string if resets_at is missing, zero, or already in the past. */
    char reset_5h[16];
    reset_5h[0] = 0;
    if (rl_5h_resets > 0) {
        long long secs = rl_5h_resets - (long long)time(NULL);
        if (secs > 0) {
            if (secs < 60) {
                snprintf(reset_5h, sizeof(reset_5h), "<1m");
            } else {
                long long mins = (secs + 30) / 60;  /* round to nearest minute */
                long long h = mins / 60;
                long long m = mins % 60;
                if (h == 0)      snprintf(reset_5h, sizeof(reset_5h), "%lldm", m);
                else if (m == 0) snprintf(reset_5h, sizeof(reset_5h), "%lldh", h);
                else             snprintf(reset_5h, sizeof(reset_5h), "%lldh %lldm", h, m);
            }
        }
    }

    /* Autocompact reserve — the slice Claude Code withholds before auto-
     * compaction fires. It's a fixed ~33k tokens regardless of window size
     * (was 45k before a 2026-Q1 upstream reduction). Expressing it as a
     * percentage of the window is only coincidentally right on a 200k
     * window (33k/200k = 16.5%). On a 1M extended-context window the
     * buffer is still 33k — i.e. 3.3%, not 16.5% — so hardcoding 16.5%
     * made the free-space number ~13 pp lower than /context reported.
     *
     * Users can override via CLAUDE_AUTOCOMPACT_PCT_OVERRIDE — the env
     * var is interpreted as the buffer percentage directly. */
    double autocompact_pct = (size > 0) ? 33000.0 * 100.0 / (double)size : 16.5;
    const char *env_override = getenv("CLAUDE_AUTOCOMPACT_PCT_OVERRIDE");
    if (env_override && *env_override) {
        char *e;
        double v = strtod(env_override, &e);
        if (e != env_override && v >= 0.0 && v < 100.0) autocompact_pct = v;
    }

    /* Free-space percentage — matches what /context labels "Free space":
     *     free_pct = 100 − used_pct − autocompact_pct
     *
     * Source preference (important after /compact or /clear):
     *   1. remaining_percentage field (authoritative — Claude Code computes it
     *      from the CURRENT conversation state, not the last API response).
     *   2. used_percentage field (equivalent, same source).
     *   3. Raw sum of current_usage tokens (input + cache_creation + cache_read).
     *
     * Raw tokens are now the LAST resort rather than the first, because they
     * reflect only the most recent API call's `current_usage` — which, after
     * a /compact event, can include cached prefix tokens that are no longer
     * part of the conceptual conversation (Claude Code hasn't invalidated
     * the prompt cache yet). That caused the statusline to show ~14pp less
     * free space than /context in post-compact sessions. The pre-computed
     * percentage fields track the true post-compact state. */
    int free_pct = -1;
    if (rem_pct_field >= 0.0) {
        double free = rem_pct_field - autocompact_pct;
        if (free < 0.0) free = 0.0;
        free_pct = (int)(free + 0.5);
    } else if (used_pct_field >= 0.0) {
        double free = 100.0 - used_pct_field - autocompact_pct;
        if (free < 0.0) free = 0.0;
        free_pct = (int)(free + 0.5);
    } else if (used_tokens > 0 && size > 0) {
        double used_pct = (double)used_tokens * 100.0 / (double)size;
        double free = 100.0 - used_pct - autocompact_pct;
        if (free < 0.0) free = 0.0;
        free_pct = (int)(free + 0.5);
    }

    /* Folder = basename(cwd) */
    const char *folder = cwd;
    for (const char *p = cwd; *p; p++) {
        if (*p == '/' || *p == '\\') folder = p + 1;
    }
    if (!*folder) folder = cwd;

    /* Git branch */
    char branch[256] = {0};
    find_git_branch(cwd, branch, sizeof(branch));

    /* Bar (30 chars wide, filled = FREE space) */
    enum { BAR_W = 30 };
    char bar[BAR_W * 3 + 1];
    bar[0] = 0;
    if (free_pct >= 0) {
        int filled = (free_pct * BAR_W + 50) / 100;
        if (filled > BAR_W) filled = BAR_W;
        if (filled < 0) filled = 0;
        char *p = bar;
        for (int i = 0; i < filled; i++) {
            /* █ U+2588 */
            *p++ = (char)0xE2; *p++ = (char)0x96; *p++ = (char)0x88;
        }
        for (int i = 0; i < BAR_W - filled; i++) {
            /* ░ U+2591 */
            *p++ = (char)0xE2; *p++ = (char)0x96; *p++ = (char)0x91;
        }
        *p = 0;
    }

    /* Render. SEP = " \x1b[38;5;245m┃\x1b[0m " ; ┃ U+2503 */
    static const char SEP[] = " \x1b[38;5;245m\xe2\x94\x83\x1b[0m ";

    /* folder: 📍 U+1F4CD = F0 9F 93 8D */
    fputs("\x1b[1;34m\xf0\x9f\x93\x8d ", stdout);
    fputs(folder, stdout);
    fputs("\x1b[0m", stdout);

    if (branch[0]) {
        fputs(SEP, stdout);
        /* 🔀 U+1F500 = F0 9F 94 80 */
        fputs("\x1b[38;5;80m\xf0\x9f\x94\x80 ", stdout);
        fputs(branch, stdout);
        fputs("\x1b[0m", stdout);
    }

    fputs(SEP, stdout);
    /* 🤖 U+1F916 = F0 9F A4 96 */
    fputs("\x1b[38;5;141m\xf0\x9f\xa4\x96 ", stdout);
    fputs(model, stdout);
    fputs("\x1b[0m", stdout);

    if (free_pct >= 0 && bar[0]) {
        fputs(SEP, stdout);
        /* 🧠 U+1F9E0 = F0 9F A7 A0. Color 213 (bold pink) — distinct from
         * the white the rate-limit defaults render in, so the brain bar
         * doesn't visually merge with the segments to its right. */
        fputs("\x1b[1;38;5;213m\xf0\x9f\xa7\xa0 [", stdout);
        fputs(bar, stdout);
        printf("] %d%%", free_pct);
        fputs("\x1b[0m", stdout);
    }

    /* Rate-limit segments. The 5-hour and weekly windows now live in their
     * own ┃-bracketed chunks — once the 5h chunk gained a reset countdown
     * it had enough internal structure that bundling it with the weekly
     * percentage read as a tangle.
     *
     * Color thresholds apply to BOTH the percentage and the countdown text:
     *   < 70%   white   (color 37)
     *   70-89%  yellow  (color 220)
     *   >= 90%  red     (color 196)
     * The middle dot between the percentage and the countdown stays gray
     * (color 245) — it's structural punctuation, not data. Each piece is
     * independently optional: a missing percentage drops the whole chunk;
     * a missing/stale resets_at drops just the countdown half. */
    if (rl_5h_pct >= 0.0) {
        int pct = (int)(rl_5h_pct + 0.5);
        const char *col = "\x1b[37m";
        if (pct >= 90)      col = "\x1b[38;5;196m";
        else if (pct >= 70) col = "\x1b[38;5;220m";
        fputs(SEP, stdout);
        fputs(col, stdout);
        /* ⏰ U+23F0 + VS16 = E2 8F B0 EF B8 8F */
        fputs("\xe2\x8f\xb0\xef\xb8\x8f 5h ", stdout);
        printf("%d%%", pct);
        fputs("\x1b[0m", stdout);
        if (reset_5h[0]) {
            /* " · " — gray middle dot (U+00B7 = C2 B7) wrapped in color 245 */
            fputs(" \x1b[38;5;245m\xc2\xb7\x1b[0m ", stdout);
            fputs(col, stdout);
            /* ♻ U+267B + VS16 = E2 99 BB EF B8 8F */
            fputs("\xe2\x99\xbb\xef\xb8\x8f ", stdout);
            fputs(reset_5h, stdout);
            fputs("\x1b[0m", stdout);
        }
    }
    if (rl_wk_pct >= 0.0) {
        int pct = (int)(rl_wk_pct + 0.5);
        const char *col = "\x1b[37m";
        if (pct >= 90)      col = "\x1b[38;5;196m";
        else if (pct >= 70) col = "\x1b[38;5;220m";
        fputs(SEP, stdout);
        fputs(col, stdout);
        /* 📅 U+1F4C5 = F0 9F 93 85 */
        fputs("\xf0\x9f\x93\x85 wk ", stdout);
        printf("%d%%", pct);
        fputs("\x1b[0m", stdout);
    }

    free(input);
    return 0;
}
