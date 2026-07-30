/*
 * chess.h - shared interface between the core engine (main.c) and the
 * bots in the bots/ directory.
 *
 * main.c owns board representation, legality, move generation and
 * make/unmake. Each bot owns evaluation and search, and exposes BOT_NAME
 * + search() so main.c's UCI/console loops can drive it.
 */

#ifndef CHESS_H
#define CHESS_H

#include <inttypes.h>

#define MAX_MOVES  256
#define MAX_PLY    128
#define MATE       30000
#define INF        32000

enum { EMPTY = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { WHITE = 0, BLACK = 1 };
enum { F_NORMAL = 0, F_EP, F_CASTLE, F_DPUSH };

#define MK(c, t)     (((c) << 3) | (t))
#define PTYPE(p)     ((p) & 7)
#define PCOL(p)      (((p) >> 3) & 1)
#define SQ(f, r)     (((r) << 4) | (f))
#define FILEOF(s)    ((s) & 7)
#define RANKOF(s)    ((s) >> 4)
#define OFFBOARD(s)  (((s) < 0) || ((s) & 0x88))

/* castling-rights */
#define CW_K 1	// 0001
#define CW_Q 2	// 0010
#define CB_K 4  // 0100
#define CB_Q 8  // 1000

typedef struct {
	unsigned char from, to, promo, flag;
} Move;

typedef struct {
	Move m;
	int captured;
	int castle;
	int ep;
	int halfmove;
} Undo;

/* -------------------------------BOARD STATE-------------------------------
 * Owned and mutated by main.c. Bots read it directly, and mutate it
 * indirectly through make_move()/unmake_move() during search.
 */
extern uint8_t  board[128];
extern uint8_t  side;
extern uint8_t  castle;
extern uint8_t  ep;              /* en-passant; target square, or -1 */
extern uint8_t  halfmove;
extern uint8_t  kingsq[2];
extern uint8_t  ply;             /* index into hist[] */
extern Undo hist[2048];

extern const int knight_dir[8];
extern const int bishop_dir[4];
extern const int rook_dir[4];
extern const int king_dir[8];

/* -------------------------------CORE ENGINE API-------------------------------
 * Implemented in main.c: legality, move generation, make/unmake.
 */
int  attacked(int sq, int by);
int  in_check(int c);
int  gen_moves(Move *list);
int  make_move(Move m);
void unmake_move(void);
void move_str(Move m, char *out);

/* -------------------------------BOT API-------------------------------
 * Implemented by exactly one file under bots/, linked in per-binary.
 */
extern const char *BOT_NAME;
extern Move best_root;

/* Searches the current position for up to `seconds` (or `max_depth` plies,
 * whichever comes first) and stores the chosen move in best_root. */
int search(double seconds, int max_depth);

#endif
