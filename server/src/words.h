#ifndef NETSKETCH_WORDS_H
#define NETSKETCH_WORDS_H

#include <stdbool.h>
#include "config.h"
#include "rng.h"

#define MAX_WORDS 1024

typedef struct {
    char words[MAX_WORDS][WORD_MAX];
    int  count;
} WordList;

void words_init(WordList *wl);
void words_init_default(WordList *wl);
int  words_load(WordList *wl, const char *path);
bool words_pick3(const WordList *wl, Rng *rng, char out[3][WORD_MAX]);
bool words_match(const char *guess, const char *word);
bool words_contains(const char *text, const char *word);
void words_make_hint(const char *word, char out[WORD_MAX]);

#endif
