#ifndef SCORES_H
#define SCORES_H

#include <stdint.h>

/* One row of the high-score table. An empty slot has name[0] == 0 and a
   score of 0; the table is kept sorted best-first, so the empty slots are
   always at the end. Lives in the BSS, so it starts empty and is forgotten
   when the game is restarted -- it is a per-session table. */
#define HS_ENTRIES  10
#define NAME_LEN    8       /* longest name, not counting the NUL */

typedef struct {
    char     name[NAME_LEN + 1];
    uint16_t score;
} HiScore;

#endif /* SCORES_H */
