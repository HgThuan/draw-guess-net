#include "words.h"
#include <stdio.h>
#include <string.h>

static char to_lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool is_ascii_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Trim leading/trailing spaces, collapse internal spaces to single ' ', optionally lowercase. */
static size_t normalize_str(const char *in, char *out, size_t cap, bool lowercase, bool ascii_only)
{
    if (!in || !out || cap == 0) {
        if (out && cap > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    while (*in && is_space_char(*in)) {
        in++;
    }

    size_t len = 0;
    bool prev_space = false;
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        char c = (char)*p;
        if (is_space_char(c)) {
            prev_space = true;
            continue;
        }
        if (*p < 0x20 || *p == 0x7F || (ascii_only && *p >= 0x7F)) {
            out[0] = '\0';
            return 0;
        }
        if (prev_space && len > 0) {
            if (len + 1 >= cap) {
                out[0] = '\0';
                return 0;
            }
            out[len++] = ' ';
            prev_space = false;
        }
        if (len + 1 >= cap) {
            out[0] = '\0';
            return 0;
        }
        out[len++] = lowercase ? to_lower_ascii(c) : c;
    }
    out[len] = '\0';
    return len;
}

void words_init(WordList *wl)
{
    if (!wl) {
        return;
    }
    memset(wl, 0, sizeof(*wl));
}

static bool words_add_unique(WordList *wl, const char *raw)
{
    if (!wl || wl->count >= MAX_WORDS) {
        return false;
    }
    char clean[WORD_MAX];
    size_t len = normalize_str(raw, clean, sizeof(clean), false, true);
    if (len == 0) {
        return false;
    }
    for (int i = 0; i < wl->count; i++) {
        if (words_match(wl->words[i], clean)) {
            return false;
        }
    }
    memcpy(wl->words[wl->count], clean, len + 1);
    wl->count++;
    return true;
}

void words_init_default(WordList *wl)
{
    if (!wl) {
        return;
    }
    words_init(wl);
    (void)words_add_unique(wl, "cat");
    (void)words_add_unique(wl, "house");
    (void)words_add_unique(wl, "tree");
}

int words_load(WordList *wl, const char *path)
{
    if (!wl || !path || path[0] == '\0') {
        return -1;
    }
    words_init(wl);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    char buf[64];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] != '\n' && !feof(fp)) {
            /* Line exceeds buffer; discard remainder of overlong line */
            int ch;
            while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
            }
            continue;
        }
        (void)words_add_unique(wl, buf);
    }

    fclose(fp);
    return wl->count;
}

bool words_pick3(const WordList *wl, Rng *rng, char out[3][WORD_MAX])
{
    if (!wl || !rng || !out || wl->count < 3) {
        return false;
    }

    int chosen[3] = { -1, -1, -1 };
    for (int i = 0; i < 3; i++) {
        int candidate = rng_range(rng, 0, wl->count - 1);
        for (int step = 0; step < wl->count; step++) {
            int idx = (candidate + step) % wl->count;
            bool dup = false;
            for (int j = 0; j < i; j++) {
                if (chosen[j] == idx || words_match(wl->words[chosen[j]], wl->words[idx])) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                chosen[i] = idx;
                break;
            }
        }
        if (chosen[i] < 0) {
            return false; /* GCOVR_EXCL_LINE */
        }
        size_t wlen = strlen(wl->words[chosen[i]]);
        memcpy(out[i], wl->words[chosen[i]], wlen + 1);
    }
    return true;
}

bool words_match(const char *guess, const char *word)
{
    if (!guess || !word) {
        return false;
    }

    while (*guess == ' ') {
        guess++;
    }
    size_t glen = strlen(guess);
    while (glen > 0 && guess[glen - 1] == ' ') {
        glen--;
    }

    while (*word == ' ') {
        word++;
    }
    size_t wlen = strlen(word);
    while (wlen > 0 && word[wlen - 1] == ' ') {
        wlen--;
    }

    if (glen == 0 || wlen == 0 || glen != wlen) {
        return false;
    }

    for (size_t i = 0; i < glen; i++) {
        unsigned char cg = (unsigned char)guess[i];
        unsigned char cw = (unsigned char)word[i];
        if (cg < 0x20 || cg >= 0x7F || cw < 0x20 || cw >= 0x7F) {
            return false;
        }
        if (to_lower_ascii((char)cg) != to_lower_ascii((char)cw)) {
            return false;
        }
    }
    return true;
}

/* Whole-word search: a match glued to other letters ("educate") does not count. */
static bool find_bounded(const char *nt, size_t lt, const char *nw, size_t lw)
{
    if (lw == 0 || lw > lt) {
        return false;
    }
    for (size_t pos = 0; pos + lw <= lt; pos++) {
        if (memcmp(nt + pos, nw, lw) != 0) {
            continue;
        }
        bool left_ok = (pos == 0) || !is_ascii_letter(nt[pos - 1]);
        bool right_ok = (pos + lw == lt) || !is_ascii_letter(nt[pos + lw]);
        if (left_ok && right_ok) {
            return true;
        }
    }
    return false;
}

bool words_contains(const char *text, const char *word)
{
    char nw[WORD_MAX];
    char nt[MAX_LINE];
    size_t lw = normalize_str(word, nw, sizeof(nw), true, true);
    size_t lt = normalize_str(text, nt, sizeof(nt), true, false);
    if (lw == 0 || lt == 0) {
        return false;
    }
    if (find_bounded(nt, lt, nw, lw)) {
        return true;
    }

    /* Multi-word answers typed without spaces ("icecream") must be caught too. */
    char joined[WORD_MAX];
    size_t lj = 0;
    for (size_t i = 0; i < lw; i++) {
        if (nw[i] != ' ') {
            joined[lj++] = nw[i];
        }
    }
    joined[lj] = '\0';
    return lj != lw && find_bounded(nt, lt, joined, lj);
}

void words_make_hint(const char *word, char out[WORD_MAX])
{
    if (!out) {
        return;
    }
    if (!word) {
        out[0] = '\0';
        return;
    }

    size_t i = 0;
    for (; word[i] != '\0' && i < WORD_MAX - 1; i++) {
        out[i] = (word[i] == ' ') ? ' ' : '_';
    }
    out[i] = '\0';
}
