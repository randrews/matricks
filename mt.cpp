// Matricks for DOS, by Ross Andrews
// This program is released under the GNU Public License v3
// The original Matricks game design was for a game for the TI-85
// by Dan Eble.
//
// How to play: try to make the board on the left match the board
// on the right. Every time you move the cursor to a different color
// of space, the destination space is changed to the _third_ color.
//
// This should build and run on Borland Turbo C++ with the
// "small" memory model. The declaration of `screen` may
// need to be changed if you use a larger memory model.

#include <conio.h>
#include <stdio.h>
#include <dos.h>
#include <mem.h>
#include <stdlib.h>
#include <time.h>

// conio is for getch, to pause after it runs
// stdio is for printf
// dos is for MK_FP. Also for the register structs
// mem is for _fmemset / memcmp
// stdlib for rand
// time for getting a random seed

typedef unsigned char byte;

void vgaMode(int mode);
inline int offset(int x, int y);
void px(int x, int y, byte c);
void hline(int x, int y, int len, byte c);
void vline(int x, int y, int len, byte c);

void drawSprite(int x, int y, const int *type, byte fg, byte bg);
void drawSquare(int x, int y, int side, byte c);
void randomizeBoard(byte *board);
void drawBoard(int x, int y, byte *board, int selected);
void drawCell(int x, int y, byte *board, int selected, int n);
void draw();
int handleKey(int key);
void drawMove(int from, int to);
int moveCursor(int *cursor, int delta);
int clamp(int l, int m, int h);
int boardsEqual(byte *a, byte *b);
void gameover();

// It doesn't support binary literals but octal is just as good
int piece1[] = {
                0, 0200, 0500, 01040, 02220, 04510, 011044, 022222,
                011044, 04510, 02220, 01040, 0500, 0200, 0
};

int piece2[] = {
                0, 025252, 012524, 025252, 012524, 025252, 012524,
                025252, 012524, 025252, 012524, 025252, 012524,
                025252, 0
};

int piece3[] = {
                0, 01740, 0200, 0200, 0200, 020202, 020202, 037776,
                020202, 020202, 0200, 0200, 0200, 01740, 0
}; // Yes I did these by hand

int BG = 20; // a nice neutral background color

// DOS is 16-bit but supports megs of memory. How? With a
// non-flat memory map. We want a pointer longer than 16
// bits, which DOS calls a "far pointer." MK_FP makes one.
byte far *screen = (byte far *)MK_FP(0xa000, 0);

// Tle left is what we manipulate, to make it match the right
byte leftboard[36], rightboard[36];

// The space currently highlighted
int cursor;

// The number of moves we've currently taken
int moves;

void main() {
    time_t t; srand((unsigned) time(&t)); // Seed the RNG
    randomizeBoard(leftboard); // Set up the two boards
    randomizeBoard(rightboard);
    cursor = rand() % 36; // And the cursor
    moves = 0; // And the score counter
    vgaMode(0x13); // Into mode 13
    hline(0, 0, 64000, BG); // clean the screen
    draw(); // Draw the boards (full)

    while(1) {
        int k = getch();
        if (k == 'q' || k == 0x1b) {
            vgaMode(3);
            break;
        }
        int delta = handleKey(k);
        int oldcursor = moveCursor(&cursor, delta);
        if (cursor != oldcursor) {
            moves++;
            drawMove(oldcursor, cursor);
        }
        if (boardsEqual(leftboard, rightboard)) {
            gameover();
            break;
        }
    }
}

// Set the VGA mode: put the new mode in AL and call int 10.
// Mode 0x13 is 320x200, 8-bit color.
void vgaMode(int mode){
    union REGS regs;
    regs.h.ah = 0;
    regs.h.al = mode & 0xff;
    int86(16, &regs, &regs);
}

// This is faster than multiplying by 320: two shifts and two adds.
inline int offset(int x, int y) {
    return x + (y << 8) + (y << 6);
}

// Linear memory for the screen, horizontal lines are just
// memset. But because it's in "far memory" it's _fmemset.
// Also note that we can quickly clear the screen with
// hline(0, 0, 64000, something);
void hline(int x, int y, int len, byte c) {
    _fmemset(screen + offset(x, y), c, len);
}

// We can't use memset for this one but we can make it a
// reasonably fast loop:
void vline(int x, int y, int len, byte c) {
    int off = offset(x, y);
    for(int n = 0; n < len; n++) {
        *(screen + off) = c;
        off += 320;
    }
}
// Setting a pixel.
void px(int x, int y, byte c) {
    *(screen + offset(x, y)) = c;
}

// Draws a 15x15 2-color sprite, packed into an array of 15 ints,
// at the given coordinates, in the given colors. Why not 16x16?
// Because it's nice to have odd sizes so there's a center line
// for symmetry.
void drawSprite(int x, int y, const int *type, byte fg, byte bg) {
    for(int r = 0; r < 15; r++)
        for(int c = 0; c < 15; c++)
            px(x + c, y + r, (type[r] & (1 << c) ? fg : bg));
}

// Fills a board with random pieces
void randomizeBoard(byte *board) {
    for(int n = 0; n < 36; n++) board[n] = rand() % 3;
}

void drawSquare(int x, int y, int side, byte c) {
    hline(x, y, side, c);
    vline(x, y, side, c);
    hline(x, y + side - 1, side, c);
    vline(x + side - 1, y, side, c);
}

// A board is a 6x6 grid of 19x19 spaces: each space has a 15x15
// sprite bordered with a 2px empty zone. The cursor, if it's
// shown, will be the outermost edge of this border. (x, y) is the
// coordinate of the top left of the outer border of the first
// space, so, (x+2, y+2) is where the sprite is actually drawn.
void drawBoard(int x, int y, byte *board, int selected) {
    for(int n = 0; n < 36; n++) {
        drawCell(x, y, board, selected, n);
    }
}

// Helper for drawBoard and drawMove, to draw a single cell
void drawCell(int x, int y, byte *board, int selected, int n) {
    int const* sprites[3] = { piece1, piece2, piece3 };
    const byte colors[3] = { 39, 9, 44 };
    int left = x + 19 * (n % 6);
    int top = y + 19 * (n / 6);
    drawSquare(left, top, 19, (selected == n ? 15 : BG));
    drawSquare(left + 1, top + 1, 17, BG);
    drawSprite(left + 2, top + 2, sprites[board[n]], colors[board[n]], BG);
}

void draw() {
    drawBoard(31, 25, leftboard, cursor); // Draw the boards
    drawBoard(31 + 114 + 30, 25, rightboard, -1); // Spaced evenly as we can
}

// Handle a keypress (maybe fetching the extended code) and return
// how far we should move the cursor based on it.
int handleKey(int key) {
    // If this is an extended key, like an arrow key,
    // get the extended code:
    if(key == 0 || key == 224) key = getch();

    int delta = 0;
    switch(key) {
    case 'w': case 72: delta = -6; break;
    case 's': case 80: delta = 6; break;
    case 'a': case 75: delta = -1; break;
    case 'd': case 77: delta = 1; break;
    }

    return delta;
}

// For speed, draw only the two cells that changed in a move:
void drawMove(int from, int to) {
    if (from == to) return;
    drawCell(31, 25, leftboard, cursor, from);
    drawCell(31, 25, leftboard, cursor, to);
}

// Handle the game logic of actually moving the cursor. Returns
// the original cursor value.
int moveCursor(int *cursor, int delta) {
    int oldcursor = *cursor;
    switch (delta) {
    case 1: if (*cursor % 6 < 5) (*cursor)++;
        break;
    case -1: if (*cursor % 6 > 0) (*cursor)--;
        break;
    default: *cursor = clamp(0, *cursor + delta, 36);
    }

    if (leftboard[*cursor] != leftboard[oldcursor]) {
        leftboard[*cursor] = 3 - leftboard[oldcursor] - leftboard[*cursor];
    }

    return oldcursor;
}

// Clamp m to the bounds of [l, h]
int clamp(int l, int m, int h) {
    if (m < l) m = l;
    else if (m > h) m = h;
    return m;
}

// Return whether the two boards are equal (in other words, if we've
// won the game)
int boardsEqual(byte *a, byte *b) {
    return !memcmp(a, b, 36);
}

// We want a nice game-over visual effect. Unfortunately we're lazy,
// so what we'll do instead is pulse the background color a bit:
void gameover() {
    BG = 63;
    while(!kbhit()) {
        BG++;
        draw();
        if (BG == 79) BG = 63;
    }
    vgaMode(3); // Back to text mode
    printf("\nYou won in %d moves! ", moves);
    if (moves < 80) printf("Fantastic!\n");
    else if(moves < 100) printf("Pretty good!\n");
    else if(moves < 120) printf("You'll get better with practice!\n");
    else printf("I bet it feels good to be done!\n");
}
