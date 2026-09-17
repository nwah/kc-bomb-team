#ifndef BOMBS_H
#define BOMBS_H

#include <stdint.h>

#define BOMB_COUNT 48
#define MAX_WIRES  6

/*
 * One bomb: its wire count, each wire's colour (0..5, see below) and each
 * wire's cut order (0 = never cut this wire; 1..num_wires-1 = the order the
 * OTHER wires must be cut in).
 *
 * Colour index: 0 Red, 1 Red striped, 2 Blue, 3 Blue striped, 4 Green,
 * 5 Green striped. Odd index means striped; index>>1 gives 0 red, 1 blue,
 * 2 green.
 */
typedef struct {
    uint8_t num_wires;
    uint8_t colour[MAX_WIRES];
    uint8_t order[MAX_WIRES];
} Bomb;

extern const Bomb bombs[BOMB_COUNT];

#endif /* BOMBS_H */
