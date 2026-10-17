#include <stdlib.h>
#include "galaksija.h"

/* The playfield is 16 cells wide, each cell being a 2x2 character sprite. */
#define CELLS       16
#define CELL_X(p)   ((p) << 1)
#define MAZE_Y      8
#define POWER_TURNS 6

#define KEY_SPACE   32
#define KEY_DEL     67

static const unsigned char dot_sprite[4]    = {160, 144, 130, 129};
static const unsigned char pill_sprite[4]   = {184, 182, 175, 159};
static const unsigned char ghost_a[4]       = {190, 185, 159, 159};
static const unsigned char ghost_b[4]       = {190, 185, 175, 175};
static const unsigned char paku_left_a[4]   = {142, 185, 172, 159};
static const unsigned char paku_left_b[4]   = {174, 185, 175, 159};
static const unsigned char paku_right_a[4]  = {182, 141, 175, 156};
static const unsigned char paku_right_b[4]  = {182, 157, 175, 159};
static const unsigned char pill_act[4]  	= {184, 182, 175, 159};

static unsigned char dots[CELLS];
static int paku_pos, paku_dir, paku_phase;
static int ghost_pos, ghost_phase, ghost_accum;
static int ghost_active;
static int pill_pos, pill_active, pill_wait, pill_already_eaten;
static int frightened;
static unsigned int score, max_score;


static int random_between(int lo, int hi) {
    return lo + (rand() * (hi - lo)) / RAND_MAX;
}

static void put_sprite(int cell, const unsigned char *sprite) {
    int addr = SCREEN_ADDR + (MAZE_Y << 5) + CELL_X(cell);
    z80_bpoke(addr,     sprite[0]);
    z80_bpoke(addr + 1, sprite[1]);
    z80_bpoke(addr + 32, sprite[2]);
    z80_bpoke(addr + 33, sprite[3]);
}

static void clear_sprite(int cell) {
    int addr = SCREEN_ADDR + (MAZE_Y << 5) + CELL_X(cell);
    z80_bpoke(addr, 32); z80_bpoke(addr + 1, 32);
    z80_bpoke(addr + 32, 32); z80_bpoke(addr + 33, 32);
}

static void put_sprite_at(int x, int y, const unsigned char *sprite) {
    int addr = SCREEN_ADDR + (y << 5) + x;
    z80_bpoke(addr,      sprite[0]);
    z80_bpoke(addr + 1,  sprite[1]);
    z80_bpoke(addr + 32, sprite[2]);
    z80_bpoke(addr + 33, sprite[3]);
}

static void clear_sprite_at(int x, int y) {
    int addr = SCREEN_ADDR + (y << 5) + x;
    z80_bpoke(addr,      32); z80_bpoke(addr + 1,  32);
    z80_bpoke(addr + 32, 32); z80_bpoke(addr + 33, 32);
}

static void center_text(int y, char *text) {
    int x = 32;                   /* count without pulling in strlen */
    char *s = text;
    while (*s++) x--;
    gal_gotoxy((char)(x >> 1), (char)y);
    gal_puts(text);
}

/*
	Welcome frame
*/
void frame() {
	center_text(5, "PAKU PAKU GAME");
	center_text(6, "SPACE - CHANGE DIRECTION");
	center_text(8, "PRESS ENTER");	
	center_text(9, "BY IVAN ILYICHEV 2026"); 
	do {
	} while (getk() != 10);
}

static void draw_score(void) {
    char number[8];
    itoa(score, number, 10);
    gal_gotoxy(7, 0); gal_puts(number);
}

/* Draw all maze rows */
static void draw_maze(void) {
    int i;
	char max_number[8];

    for (i = 0; i < 32; i++) {
        z80_bpoke(SCREEN_ADDR + (7 << 5) + i, 140);
        z80_bpoke(SCREEN_ADDR + (10 << 5) + i, 140);
    }
    gal_gotoxy(0, 0); gal_puts("                                ");
    gal_gotoxy(0, 0); gal_puts("SCORE: ");
    gal_gotoxy(22, 0); gal_puts("HI: "); 
    itoa(max_score, max_number, 10);
    gal_gotoxy(26, 0); gal_puts(max_number);
}

/* Draw actors on top. */
static void draw_scene(void) {
    int i;
    const unsigned char *paku;

    for (i = 0; i < CELLS; i++) {
        if (dots[i] && !(pill_active && pill_pos == i)) clear_sprite(i);
        else if (!pill_active || pill_pos != i) put_sprite(i, dot_sprite);
    }
    if (pill_active) put_sprite(pill_pos, pill_sprite);
    if (ghost_active)
        put_sprite(ghost_pos, (ghost_phase == 0) ? ghost_a : ghost_b);

    if (paku_dir < 0)
        paku = (paku_phase == 0) ? paku_left_a : paku_left_b;
    else
        paku = (paku_phase == 0) ? paku_right_a : paku_right_b;
    put_sprite(paku_pos, paku);

    if (frightened > 0)
        put_sprite_at(15, 11, pill_act);
    else
        clear_sprite_at(15, 11);
    draw_score();
}

static void reset_segment(void) {
    int i;
    for (i = 0; i < CELLS; i++) dots[i] = 0;
    pill_active = 0;
	pill_already_eaten = 0;
    pill_wait = random_between(2, 5);
}

/* Distance on the wrapping row of maze cells. */
static int maze_distance(int a, int b) {
    int distance = a - b;
    if (distance < 0) distance = -distance;
    if (distance > CELLS - distance) distance = CELLS - distance;
    return distance;
}

static void spawn_pill(void) {
    int p, i;

    /* Try random positions first, then use a deterministic fallback. */
    for (i = 0; i < CELLS * 2; i++) {
        p = random_between(0, CELLS - 1);
        if (maze_distance(p, paku_pos) >= 4) {
            pill_pos = p;
            pill_active = 1;
            return;
        }
    }
    for (p = 0; p < CELLS; p++) {
        if (maze_distance(p, paku_pos) >= 4) {
            pill_pos = p;
            pill_active = 1;
            return;
        }
    }
    pill_active = 0;
}

/* A ghost always enters from the corner behind Paku's movement direction. */
static void spawn_ghost(void) {
    ghost_active = 1;
    ghost_pos = (paku_dir < 0) ? CELLS - 1 : 0;
    ghost_phase = 0;
    ghost_accum = 0;
}

static void move_ghost_once(void) {
    if (ghost_pos > paku_pos) ghost_pos--;
    else if (ghost_pos < paku_pos) ghost_pos++;
    if (ghost_pos < 0) ghost_pos = 0;
    if (ghost_pos >= CELLS) ghost_pos = CELLS - 1;
    ghost_phase ^= 1;
}

static void update_ghost(void) {
    if (!ghost_active) return;
    /* 11/10 gives the requested 10% speed advantage over Paku. */
    ghost_accum += 11;
    while (ghost_accum >= 10) {
        ghost_accum -= 10;
        move_ghost_once();
    }
}

static int all_dots_eaten(void) {
    int i;
    for (i = 0; i < CELLS; i++) if (!dots[i]) return 0;
    return 1;
}

static int play_game(void) {
    char key;
	int dir_changed = 0;
	int ghost_spawned;
	volatile unsigned int n;
    score = 0;
    paku_pos = 7; 
	paku_dir = -1; 
	paku_phase = 0;
    frightened = 0;
	spawn_ghost();
    reset_segment();
	draw_maze();
    draw_scene();

    for (;;) {
		// Prevent fast change direction
		if (dir_changed > 0) {
			dir_changed--;
		}
		ghost_spawned = 0;
		for (n = 0; n < 80; n++) { 
			key = getk();
			if (key == KEY_DEL) return 0;
			if (key == KEY_SPACE && dir_changed == 0) {
				paku_dir = -paku_dir;
				dir_changed = 3;
			}
		}

        paku_pos += paku_dir;
        if (paku_pos < 0) paku_pos = CELLS - 1;
        if (paku_pos >= CELLS) paku_pos = 0;
        paku_phase ^= 1;

        if (!dots[paku_pos]) {
            dots[paku_pos] = 1;
            score++;
            if (score > max_score) max_score = score;
        }
        if (pill_active && paku_pos == pill_pos) {
            pill_active = 0;
			pill_already_eaten = 1;
            frightened = POWER_TURNS;
            pill_wait = random_between(2, 5);
            spawn_ghost();
			ghost_spawned = 1;
        }
        if (!pill_active && !pill_already_eaten) {
            if (pill_wait > 0) pill_wait--;
            if (pill_wait == 0) spawn_pill();
        }

        if (frightened > 0) frightened--;
        if (!ghost_spawned) update_ghost();

        if (ghost_active && ghost_pos == paku_pos) {
            if (frightened > 0) {
                score += 10;
                if (score > max_score) max_score = score;
                spawn_ghost();
            } else {
                draw_scene();
                return 1;
            }
        }
        if (all_dots_eaten()) reset_segment();
        draw_scene();
    }
}

static void draw_game_over(void) {
    char number[8], *s;
    int digits = 0;
    int x, j;

    itoa(score, number, 10);
    s = number;
    while (*s++) digits++;
    x = (32 - (7 + digits)) >> 1;

	gal_gotoxy(4, 2);
	for (j = 1; j <= 24; j++) {
		gal_putc(240);
	}
	gal_gotoxy(4, 5);
	for (j = 1; j <= 24; j++) {
		gal_putc(131);
	}	
	gal_gotoxy(4, 3); gal_putc(149);
	gal_gotoxy(27, 3); gal_putc(170);
	gal_gotoxy(4, 4); gal_putc(149);
	gal_gotoxy(27, 4); gal_putc(170);
    gal_gotoxy(x, 3);  gal_puts("SCORE: "); gal_puts(number);
    center_text(4, "PRESS ENTER");
}

int main(void) {
    int result;
    char key;
    max_score = 0;
    for (;;) {
	    srand(z80_wpeek(RND_ADDR));
        gal_cls();
        frame();
        gal_cls();
        center_text(0, "PAKU PAKU");
        result = play_game();
        if (!result) return 0;
        draw_game_over();
        for (;;) {
            key = getk();
            if (key == 10) break;
            if (key == KEY_DEL) return 0;
        }
    }
}
