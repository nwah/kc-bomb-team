#include <stdint.h>
#include <stdlib.h>
#include "hw.h"
#include "bombs.h"
#include "view.h"
#include "game.h"

/*
 * The state machine and rules. Screen coordinates never appear here --
 * everything visible goes through view.c.
 */

typedef struct {
    uint16_t score;
    uint8_t  defused;
    uint8_t  min_bomb;
    uint8_t  page;
    uint8_t  bomb_num;
    uint8_t  selected;
    uint8_t  cut;
    uint8_t  to_cut;
    uint8_t  ticks_left;
    uint16_t tick_timer;
    uint8_t  cut_mask;   /* bit i set once wire i has been cut */
} GameState;

static GameState G;

/* 0 while a bomb is live, 1 once it has exploded, 2 once it has been
   defused. Read by play_bomb()'s loop and by game_run(). */
static uint8_t round_over;

/* clk_ticks() as tick() last saw it, so the fuse can be burnt in real time. */
static uint8_t last_clk;

static void boom(void);
static void defuse(void);
static void tick(void);
static void cut_wire(void);
static void pick_bomb(void);
static void reset_timer(void);
static void play_bomb(void);

/* Blocks until the player presses something. */
static void wait_key(void)
{
    while (key_get() == 0) view_wait(1);
}

/* ------------------------------------------------------------------- */

static void pick_bomb(void)
{
    G.bomb_num = (uint8_t)(G.min_bomb + (rand() % 8));
    G.cut = 0;
    G.to_cut = (uint8_t)(bombs[G.bomb_num].num_wires - 1);
    G.selected = 0;
    G.cut_mask = 0;
}

static void reset_timer(void)
{
    int t;
    G.ticks_left = 26;
    if (G.defused > 20) {
        t = 26 - (int)G.defused + 5;
        if (t < 15) t = 15;
        G.ticks_left = (uint8_t)t;
    }
    G.tick_timer = 0;   /* so the first tick fires immediately */
    last_clk = clk_ticks();
}

static void tick(void)
{
    uint8_t now, elapsed, i;

    /*
     * The fuse burns in real time, not in passes round the game loop. A pass
     * that redraws a page of the manual takes many times longer than an idle
     * one, so counting passes let a player who flips pages a lot slow the
     * countdown down; measuring the clock instead makes the fuse the same
     * length however busy the loop is.
     */
    now = clk_ticks();
    elapsed = (uint8_t)(last_clk - now);   /* the CTC counts down */
    last_clk = now;

    if (G.tick_timer > elapsed) {
        G.tick_timer = (uint16_t)(G.tick_timer - elapsed);
        return;
    }
    elapsed = (uint8_t)(elapsed - G.tick_timer);   /* how far we overran */

    view_light(LIGHT_RED);
    snd_tone(TICK_PITCH, 12);
    view_wait(5);
    snd_off();
    view_light(LIGHT_OFF);

    G.ticks_left--;

    /* The base sets the whole tempo; the doublings below scale off it, and
       the fuse comes to base * 81 fiftieths in total. 13 keeps it at the
       21 seconds the pass-counting version happened to run to. */
    G.tick_timer = 13;
    for (i = 11; i <= G.ticks_left; i += 5) {
        G.tick_timer = (uint16_t)(G.tick_timer + G.tick_timer);
    }
    /* Carry the overrun into the next interval rather than losing it, so a
       slow pass cannot buy the player extra time. The beep above is charged
       the same way: it lands in the next pass's elapsed. */
    G.tick_timer = (uint16_t)(G.tick_timer > elapsed ? G.tick_timer - elapsed
                                                     : 1);

    if (G.ticks_left == 0) boom();
}

static void cut_wire(void)
{
    uint8_t expected;
    const Bomb *b = &bombs[G.bomb_num];

    view_cut_anim(b, G.cut_mask, G.selected);

    expected = b->order[G.selected];
    G.cut++;
    if (expected > G.cut || expected == 0) {
        boom();
    } else if (expected == G.cut) {
        G.score = (uint16_t)(G.score + 10);
        G.cut_mask |= (uint8_t)(1 << G.selected);
        view_wire(b, G.selected, 1);
        /* view_wire repaints the whole wire, scissors included, so put them
           back on the severed one. */
        view_scissors(b, G.cut_mask, 0xFF, G.selected);
        view_status(G.score, G.defused);
        if (G.cut == G.to_cut) defuse();
    } else {
        G.cut--;   /* already cut; ignore */
        view_scissors(b, G.cut_mask, 0xFF, G.selected);
    }
}

static void defuse(void)
{
    if (G.min_bomb < 40) G.min_bomb = (uint8_t)(G.min_bomb + 2);
    G.defused++;
    G.score = (uint16_t)(G.score + 100);
    view_status(G.score, G.defused);
    view_defused();
    round_over = 2;
}

static void boom(void)
{
    view_boom(G.score);
    round_over = 1;
}

/* ------------------------------------------------------------------- */

static void play_bomb(void)
{
    uint8_t k, old;
    const Bomb *b;

    view_manual(G.page);
    pick_bomb();
    b = &bombs[G.bomb_num];
    view_bomb(b);
    reset_timer();
    view_scissors(b, G.cut_mask, 0xFF, G.selected);
    view_status(G.score, G.defused);

    round_over = 0;
    while (!round_over) {
        tick();
        if (round_over) break;

        k = key_get();
        if (k == 0) {
            view_wait(1);
            continue;
        }

        switch (k) {
            case 0x08: case 'a': case 'A': case ',':
                G.page = (uint8_t)((G.page + 48) % 49);
                view_manual(G.page);
                break;

            case 0x09: case 'd': case 'D': case '.':
                G.page = (uint8_t)((G.page + 1) % 49);
                view_manual(G.page);
                break;

            case 0x0B: case 'w': case 'W':
                old = G.selected;
                G.selected = (uint8_t)(G.selected == 0 ? b->num_wires - 1
                                                       : G.selected - 1);
                view_scissors(b, G.cut_mask, old, G.selected);
                break;

            case 0x0A: case 's': case 'S':
                old = G.selected;
                G.selected = (uint8_t)(G.selected + 1 >= b->num_wires ? 0
                                                       : G.selected + 1);
                view_scissors(b, G.cut_mask, old, G.selected);
                break;

            case ' ': case 0x0D:
                cut_wire();
                break;

            default:
                break;
        }
    }
}

void game_run(void)
{
    static uint8_t seeded = 0;

    view_init();
    for (;;) {
        view_title();
        wait_key();
        if (!seeded) {
            srand(clk_ticks());
            seeded = 1;
        }

        G.score = 0;
        G.defused = 0;
        G.min_bomb = 0;
        /* G.page is kept across bombs and games -- never reset here. */

        for (;;) {
            play_bomb();
            if (round_over == 1) {        /* exploded: back to the title */
                wait_key();
                break;
            }
            /* The green flashes say it is defused; say how to go on. */
            view_prompt("DEFUSED - PRESS SPACE FOR THE NEXT");
            wait_key();
            view_prompt(0);
        }
    }
}
