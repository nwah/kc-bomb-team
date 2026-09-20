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

/* Runs the title screen's menu, keeping the logo lamp pulsing exactly as
   the old wait did -- half a second on, half a second off -- while it
   waits for the player to move the selection or choose it. Returns the
   chosen MENU_* item. */
static uint8_t title_menu(void)
{
    uint8_t sel = MENU_START;
    uint8_t on = 0, t = 0, k;

    for (;;) {
        while ((k = key_get()) == 0) {
            view_wait(1);
            if (++t >= 25) {
                t = 0;
                on ^= 1;
                view_logo_light(on);
            }
        }

        /* Up/down and left/right both move the selection: a menu drawn as a
           column answers to W/S, one drawn as a row to A/D and the cursor
           keys, and the shared code need not know which it has. */
        switch (k) {
            case 0x0B: case 'w': case 'W':
            case 0x08: case 'a': case 'A': case ',':
                /* sel is unsigned, so decrementing past 0 wraps to 0xFF
                   rather than -1, which is what this is actually testing
                   for. */
                sel--;
                if (sel > MENU_ITEMS) sel = MENU_ITEMS - 1;
                view_title_menu(sel);
                break;

            case 0x0A: case 's': case 'S':
            case 0x09: case 'd': case 'D': case '.':
                if (++sel >= MENU_ITEMS) sel = 0;
                view_title_menu(sel);
                break;

            case ' ': case 0x0D:
                return sel;
        }
    }
}

/* ------------------------------------------------------------------- */

/* The player's name, kept from game to game so that a returning player only
   has to press ENTER, and the session's high scores, best first. Both live
   in the BSS, so they start empty and are gone after a restart. */
static char player_name[NAME_LEN + 1];
static HiScore hs[HS_ENTRIES];

/* Asks for the player's name before a game, into player_name. The editing
   is done in a scratch buffer that view_name() redraws after every key, so
   ENTER is the only thing that commits it. */
static void ask_name(void)
{
    char buf[NAME_LEN + 1];
    uint8_t len, fresh, k;

    for (len = 0; player_name[len]; len++) buf[len] = player_name[len];
    buf[len] = 0;
    /* A remembered name starts out "fresh": the first key typed replaces it
       wholesale rather than being tacked on the end, so a different player
       need not backspace all eight letters away. Backspace edits it in
       place instead, and either one ends the freshness. */
    fresh = (uint8_t)(len != 0);

    for (;;) {
        view_name(buf);
        while ((k = key_get()) == 0) view_wait(1);

        if (k == 0x0D) {
            while (len != 0 && buf[len - 1] == ' ') len--;
            if (len == 0) continue;       /* a name is required */
            for (k = 0; k < len; k++) player_name[k] = buf[k];
            player_name[len] = 0;
            return;
        }

        /* DEL, backspace and cursor-left, whichever the machine delivers. */
        if (k == 0x08 || k == 0x7F || k == 0x1F) {
            if (len != 0) buf[--len] = 0;
            fresh = 0;
            continue;
        }

        /* Anything else that is not printable, the cursor keys included,
           is ignored. A space cannot begin a name, and a stray one must not
           wipe the remembered name either. */
        if (k < 0x20 || k > 0x7E) continue;
        if (k == ' ' && (len == 0 || fresh)) continue;
        if (fresh) {
            len = 0;
            fresh = 0;
        }
        if (len >= NAME_LEN) continue;
        if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 32);
        buf[len++] = (char)k;
        buf[len] = 0;
    }
}

/* Enters `score` under player_name and returns the 0-based rank it took, or
   0xFF if it did not make the table. Nothing scores nothing, and a tie goes
   to whoever got there first, so the new score has to beat an entry outright
   to move ahead of it. The empty slots at the end have a score of 0, so any
   score that gets this far beats them. */
static uint8_t hs_insert(uint16_t score)
{
    uint8_t rank, i;

    if (score == 0) return 0xFF;
    for (rank = 0; rank < HS_ENTRIES; rank++)
        if (score > hs[rank].score) break;
    if (rank == HS_ENTRIES) return 0xFF;

    for (i = HS_ENTRIES - 1; i > rank; i--) hs[i] = hs[i - 1];
    hs[rank].score = score;
    for (i = 0; i <= NAME_LEN; i++) hs[rank].name[i] = player_name[i];
    return rank;
}

/* ------------------------------------------------------------------- */

static void pick_bomb(void)
{
    uint8_t prev = G.bomb_num;
    uint8_t n;

    /* The same bomb twice running reads as the game not having moved on, so
       keep drawing until it comes up different. The pool is eight wide, so
       this rarely goes round even once, and it can never spin: there are
       always seven other bombs to land on. */
    do {
        n = (uint8_t)(G.min_bomb + (rand() % 8));
    } while (n == prev);

    G.bomb_num = n;
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
    view_timer(G.ticks_left);
    view_light(LIGHT_OFF);   /* the last bomb left the safe lamp lit */
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
    view_timer(G.ticks_left);

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

    /* The prompt line is the one thing on screen the manual does not cover,
       so the title's "PRESS SPACE TO START" has to be taken down here. */
    view_prompt(0);
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
    uint8_t rank;

    view_init();
    for (;;) {
        view_title();
        switch (title_menu()) {
            /* The title stays up: the loop draws it again, in the other
               language this time. */
            case MENU_LANG:
                view_lang_toggle();
                continue;

            /* exit() puts back the CAOS interrupt vectors the crt hooked
               at startup and leaves through FNLOOP, which returns control
               to CAOS without initialising memory -- so the game is still
               in the menu, ready to be started again. */
            case MENU_EXIT:
                view_exit();
                exit(0);

            default:            /* MENU_START: on with the game */
                break;
        }
        if (!seeded) {
            srand(clk_ticks());
            seeded = 1;
        }

        ask_name();

        G.score = 0;
        G.defused = 0;
        G.min_bomb = 0;
        view_book_rise();   /* the book slides up over the title's logo */
        /* G.page is kept from bomb to bomb, so the player does not lose
           their place; only an explosion closes the manual again. */

        for (;;) {
            play_bomb();
            if (round_over == 1) {        /* exploded: back to the title */
                /* boom() has put the banner and the final score up; the
                   table goes under them, with this game's entry picked out
                   if it made the cut. */
                rank = hs_insert(G.score);
                view_scores(hs, rank);
                wait_key();
                G.page = 0;               /* the next game starts closed */
                break;
            }
            /* The green lamp and the readout are already saying it is
               defused, so the bar only has to say how to go on. */
            view_continue();
            wait_key();
            view_prompt(0);
        }
    }
}
