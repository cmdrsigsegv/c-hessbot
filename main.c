/*
 * main.c - core chess engine: board representation, legality, move
 * generation, make/unmake, FEN/UCI plumbing.
 *
 * Evaluation and search live in the bots/ directory - see the Makefile.
 * This file never picks a move itself; it only calls into the bot's
 * search().
 *
 * Build:  make <botname>   (e.g. make toybot)
 *
 * Two ways to use the resulting binary:
 *   1. Plug it into any UCI GUI (Cute Chess, En Croissant, Arena, ...).
 *   2. Run it and type "play" for a bare-bones ASCII board you can move on.
 *
 * Board representation: 0x88 (16x8 array, off-board test is (sq & 0x88)).
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "chess.h"

/* -------------------------------STATE-------------------------------
 */

uint8_t  board[128];
uint8_t  side;
uint8_t  castle;
uint8_t  ep;              /* en-passant; target square, or -1 */
uint8_t  halfmove;
uint8_t  kingsq[2];
uint8_t  ply;             /* index into hist[] */
Undo hist[2048];

const int knight_dir[8] = { 33, 31, 18, 14, -33, -31, -18, -14 };
const int bishop_dir[4] = { 17, 15, -17, -15 };
const int rook_dir[4]   = { 16, 1, -16, -1 };
const int king_dir[8]   = { 17, 16, 15, 1, -17, -16, -15, -1 };

/* -------------------------------BOARD SETUP------------------------------- 
 */

static void clear_board(void) {
	int i;
	for (i = 0; i < 128; i++)
		board[i] = EMPTY;
	side = WHITE;
	castle = 0;
	ep = -1;
	halfmove = 0;
	ply = 0;
	kingsq[WHITE] = kingsq[BLACK] = -1;
}

static int piece_from_char(int c) {
	switch (c) {
		case 'P': return MK(WHITE, PAWN);
		case 'N': return MK(WHITE, KNIGHT);
		case 'B': return MK(WHITE, BISHOP);
		case 'R': return MK(WHITE, ROOK);
		case 'Q': return MK(WHITE, QUEEN);
		case 'K': return MK(WHITE, KING);
		case 'p': return MK(BLACK, PAWN);
		case 'n': return MK(BLACK, KNIGHT);
		case 'b': return MK(BLACK, BISHOP);
		case 'r': return MK(BLACK, ROOK);
		case 'q': return MK(BLACK, QUEEN);
		case 'k': return MK(BLACK, KING);
	}
	return EMPTY;
}

static int char_from_piece(int p) {
	static const char *w = ".PNBRQK";
	static const char *b = ".pnbrqk";
	if (p == EMPTY)
		return '.';
	return PCOL(p) == WHITE ? w[PTYPE(p)] : b[PTYPE(p)];
}

/* Parses the six standard FEN fields; extra fields are ignored. */
static void set_fen(const char *fen) {
	int rank = 7, file = 0, sq;
	const char *s = fen;

	clear_board();
	for (; *s && *s != ' '; s++) {
		if (*s == '/') {
			rank--;
			file = 0;
		} else if (*s >= '1' && *s <= '8') {
			file += *s - '0';
		} else {
			sq = SQ(file, rank);
			board[sq] = piece_from_char(*s);
			if (PTYPE(board[sq]) == KING)
				kingsq[PCOL(board[sq])] = sq;
			file++;
		}
	}
	while (*s == ' ')
		s++;
	side = (*s == 'b') ? BLACK : WHITE;
	while (*s && *s != ' ')
		s++;
	while (*s == ' ')
		s++;
	for (; *s && *s != ' '; s++) {
		if (*s == 'K') castle |= CW_K;
		if (*s == 'Q') castle |= CW_Q;
		if (*s == 'k') castle |= CB_K;
		if (*s == 'q') castle |= CB_Q;
	}
	while (*s == ' ')
		s++;
	if (*s && *s != '-')
		ep = SQ(s[0] - 'a', s[1] - '1');
	while (*s && *s != ' ')
		s++;
	while (*s == ' ')
		s++;
	if (*s)
		halfmove = atoi(s);
}

static void print_board(void) {
	int r, f;
	for (r = 7; r >= 0; r--) {
		printf("%d  ", r + 1);
		for (f = 0; f < 8; f++)
			printf("%c ", char_from_piece(board[SQ(f, r)]));
		printf("\n");
	}
	printf("\n   a b c d e f g h      %s to move\n\n",
	       side == WHITE ? "white" : "black");
}

/* -------------------------------ATTACK DETECTION-------------------------------
 */

/* Is square sq attacked by any piece of colour by? */
int attacked(int sq, int by) {
	int i, d, t, p;

	if (by == WHITE) {
		if (!OFFBOARD(sq - 15) && board[sq - 15] == MK(WHITE, PAWN)) return 1;
		if (!OFFBOARD(sq - 17) && board[sq - 17] == MK(WHITE, PAWN)) return 1;
	} else {
		if (!OFFBOARD(sq + 15) && board[sq + 15] == MK(BLACK, PAWN)) return 1;
		if (!OFFBOARD(sq + 17) && board[sq + 17] == MK(BLACK, PAWN)) return 1;
	}
	for (i = 0; i < 8; i++) {
		t = sq + knight_dir[i];
		if (!OFFBOARD(t) && board[t] == MK(by, KNIGHT)) return 1;
	}
	for (i = 0; i < 8; i++) {
		t = sq + king_dir[i];
		if (!OFFBOARD(t) && board[t] == MK(by, KING)) return 1;
	}
	for (i = 0; i < 4; i++) {
		d = bishop_dir[i];
		for (t = sq + d; !OFFBOARD(t); t += d) {
			p = board[t];
			if (p == EMPTY)
				continue;
			if (PCOL(p) == by && (PTYPE(p) == BISHOP || PTYPE(p) == QUEEN))
				return 1;
			break;
		}
	}
	for (i = 0; i < 4; i++) {
		d = rook_dir[i];
		for (t = sq + d; !OFFBOARD(t); t += d) {
			p = board[t];
			if (p == EMPTY)
				continue;
			if (PCOL(p) == by && (PTYPE(p) == ROOK || PTYPE(p) == QUEEN))
				return 1;
			break;
		}
	}
	return 0;
}

int in_check(int c) {
	return attacked(kingsq[c], c ^ 1);
}

/* -------------------------------MOVE GENERATION-------------------------------
 */

static void add(Move *list, int *n, int from, int to, int promo, int flag) {
	Move *m = &list[(*n)++];
	m->from = (unsigned char)from;
	m->to = (unsigned char)to;
	m->promo = (unsigned char)promo;
	m->flag = (unsigned char)flag;
}

static void add_pawn(Move *list, int *n, int from, int to, int last_rank, int flag) {
	if (RANKOF(to) == last_rank) {
		add(list, n, from, to, QUEEN, flag);
		add(list, n, from, to, ROOK, flag);
		add(list, n, from, to, BISHOP, flag);
		add(list, n, from, to, KNIGHT, flag);
	} else {
		add(list, n, from, to, 0, flag);
	}
}

/* Generates pseudo-legal moves for the side to move. */
int gen_moves(Move *list) {
	int n = 0, sq, i, d, t, p, us = side, them = side ^ 1;
	int push = (us == WHITE) ? 16 : -16;
	int home = (us == WHITE) ? 1 : 6;
	int last = (us == WHITE) ? 7 : 0;

	for (sq = 0; sq < 0x88; sq++) {
		if (OFFBOARD(sq))
			continue;
		p = board[sq];
		if (p == EMPTY || PCOL(p) != us)
			continue;

		switch (PTYPE(p)) {
		case PAWN:
			t = sq + push;
			if (!OFFBOARD(t) && board[t] == EMPTY) {
				add_pawn(list, &n, sq, t, last, F_NORMAL);
				if (RANKOF(sq) == home && board[t + push] == EMPTY)
					add(list, &n, sq, t + push, 0, F_DPUSH);
			}
			for (i = -1; i <= 1; i += 2) {
				t = sq + push + i;
				if (OFFBOARD(t))
					continue;
				if (board[t] != EMPTY && PCOL(board[t]) == them)
					add_pawn(list, &n, sq, t, last, F_NORMAL);
				else if (t == ep)
					add(list, &n, sq, t, 0, F_EP);
			}
			break;

		case KNIGHT:
			for (i = 0; i < 8; i++) {
				t = sq + knight_dir[i];
				if (OFFBOARD(t))
					continue;
				if (board[t] == EMPTY || PCOL(board[t]) == them)
					add(list, &n, sq, t, 0, F_NORMAL);
			}
			break;

		case KING:
			for (i = 0; i < 8; i++) {
				t = sq + king_dir[i];
				if (OFFBOARD(t))
					continue;
				if (board[t] == EMPTY || PCOL(board[t]) == them)
					add(list, &n, sq, t, 0, F_NORMAL);
			}
			break;

		default: {
			const int *dirs = (PTYPE(p) == BISHOP) ? bishop_dir :
			                  (PTYPE(p) == ROOK)   ? rook_dir : king_dir;
			int ndirs = (PTYPE(p) == QUEEN) ? 8 : 4;
			for (i = 0; i < ndirs; i++) {
				d = dirs[i];
				for (t = sq + d; !OFFBOARD(t); t += d) {
					if (board[t] == EMPTY) {
						add(list, &n, sq, t, 0, F_NORMAL);
						continue;
					}
					if (PCOL(board[t]) == them)
						add(list, &n, sq, t, 0, F_NORMAL);
					break;
				}
			}
			break;
		}
		}
	}

	/* castling: squares empty, king not passing through attack */
	if (us == WHITE) {
		if ((castle & CW_K) && board[5] == EMPTY && board[6] == EMPTY &&
		    !attacked(4, BLACK) && !attacked(5, BLACK) && !attacked(6, BLACK))
			add(list, &n, 4, 6, 0, F_CASTLE);
		if ((castle & CW_Q) && board[3] == EMPTY && board[2] == EMPTY &&
		    board[1] == EMPTY && !attacked(4, BLACK) && !attacked(3, BLACK) &&
		    !attacked(2, BLACK))
			add(list, &n, 4, 2, 0, F_CASTLE);
	} else {
		if ((castle & CB_K) && board[117] == EMPTY && board[118] == EMPTY &&
		    !attacked(116, WHITE) && !attacked(117, WHITE) && !attacked(118, WHITE))
			add(list, &n, 116, 118, 0, F_CASTLE);
		if ((castle & CB_Q) && board[115] == EMPTY && board[114] == EMPTY &&
		    board[113] == EMPTY && !attacked(116, WHITE) && !attacked(115, WHITE) &&
		    !attacked(114, WHITE))
			add(list, &n, 116, 114, 0, F_CASTLE);
	}
	return n;
}

/* -------------------------------MAKE/UNMAKE-------------------------------
 */

/* Returns 0 and leaves the position untouched if the move is illegal. */
int make_move(Move m) {
	Undo *u = &hist[ply];
	int p = board[m.from];
	int us = PCOL(p);
	int capsq;

	u->m = m;
	u->castle = castle;
	u->ep = ep;
	u->halfmove = halfmove;
	u->captured = board[m.to];

	board[m.to] = m.promo ? MK(us, m.promo) : p;
	board[m.from] = EMPTY;

	if (m.flag == F_EP) {
		capsq = m.to + (us == WHITE ? -16 : 16);
		u->captured = board[capsq];
		board[capsq] = EMPTY;
	} else if (m.flag == F_CASTLE) {
		switch (m.to) {
			case 6:   board[5]   = board[7];   board[7]   = EMPTY; break;
			case 2:   board[3]   = board[0];   board[0]   = EMPTY; break;
			case 118: board[117] = board[119]; board[119] = EMPTY; break;
			case 114: board[115] = board[112]; board[112] = EMPTY; break;
		}
	}

	if (PTYPE(p) == KING)
		kingsq[us] = m.to;

	if (m.from == 4   || m.to == 4)   castle &= ~(CW_K | CW_Q);
	if (m.from == 7   || m.to == 7)   castle &= ~CW_K;
	if (m.from == 0   || m.to == 0)   castle &= ~CW_Q;
	if (m.from == 116 || m.to == 116) castle &= ~(CB_K | CB_Q);
	if (m.from == 119 || m.to == 119) castle &= ~CB_K;
	if (m.from == 112 || m.to == 112) castle &= ~CB_Q;

	ep = (m.flag == F_DPUSH) ? m.from + (us == WHITE ? 16 : -16) : -1;
	halfmove = (PTYPE(p) == PAWN || u->captured != EMPTY) ? 0 : halfmove + 1;

	side = us ^ 1;
	ply++;

    /* left own king hanging */
	if (in_check(us)) {
		unmake_move();
		return 0;
	}
	return 1;
}

void unmake_move(void) {
	Undo *u;
	Move m;
	int p, us, capsq;

	ply--;
	u = &hist[ply];
	m = u->m;
	p = board[m.to];
	us = PCOL(p);

	board[m.from] = m.promo ? MK(us, PAWN) : p;
	board[m.to] = EMPTY;

	if (m.flag == F_EP) {
		capsq = m.to + (us == WHITE ? -16 : 16);
		board[capsq] = u->captured;
	} else {
		board[m.to] = u->captured;
		if (m.flag == F_CASTLE) {
			switch (m.to) {
				case 6:   board[7]   = board[5];   board[5]   = EMPTY; break;
				case 2:   board[0]   = board[3];   board[3]   = EMPTY; break;
				case 118: board[119] = board[117]; board[117] = EMPTY; break;
				case 114: board[112] = board[115]; board[115] = EMPTY; break;
			}
		}
	}

	if (PTYPE(board[m.from]) == KING)
		kingsq[us] = m.from;

	castle = u->castle;
	ep = u->ep;
	halfmove = u->halfmove;
	side = us;
}

/* -------------------------------PERFT-------------------------------
 */

static long perft(int depth) {
	Move list[MAX_MOVES];
	int n, i;
	long total = 0;

	if (depth == 0)
		return 1;
	n = gen_moves(list);
	for (i = 0; i < n; i++) {
		if (!make_move(list[i]))
			continue;
		total += perft(depth - 1);
		unmake_move();
	}
	return total;
}

/* -------------------------------UCI I/O-------------------------------
 */

void move_str(Move m, char *out) {
	static const char *promos = " nbrq";
	out[0] = 'a' + FILEOF(m.from);
	out[1] = '1' + RANKOF(m.from);
	out[2] = 'a' + FILEOF(m.to);
	out[3] = '1' + RANKOF(m.to);
	out[4] = '\0';
	if (m.promo) {
		out[4] = promos[m.promo - 1];
		out[5] = '\0';
	}
}

static const char *START_FEN =
	"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

/* Applies a move given in coordinate notation, e.g. "e2e4", "e7e8q". */
static int play_str(const char *s) {
	Move list[MAX_MOVES];
	int n, i, from, to, promo = 0;

	if (strlen(s) < 4)
		return 0;
	from = SQ(s[0] - 'a', s[1] - '1');
	to = SQ(s[2] - 'a', s[3] - '1');
	switch (s[4]) {
		case 'n': promo = KNIGHT; break;
		case 'b': promo = BISHOP; break;
		case 'r': promo = ROOK;   break;
		case 'q': promo = QUEEN;  break;
	}

	n = gen_moves(list);
	for (i = 0; i < n; i++) {
		if (list[i].from != from || list[i].to != to)
			continue;
		if (promo && list[i].promo != promo)
			continue;
		if (make_move(list[i]))
			return 1;
	}
	return 0;
}

static void cmd_position(char *line) {
	char *moves = strstr(line, "moves");
	char *tok;

	if (strstr(line, "startpos")) {
		set_fen(START_FEN);
	} else {
		char *fen = strstr(line, "fen");
		if (!fen)
			return;
		fen += 4;
		if (moves)
			*moves = '\0';
		set_fen(fen);
		if (moves)
			*moves = 'm';
	}
	if (!moves)
		return;
	tok = strtok(moves + 5, " \t\n");
	while (tok) {
		play_str(tok);
		tok = strtok(NULL, " \t\n");
	}
	ply = 0;                    /* history before the search root is not needed */
}

static void cmd_go(char *line) {
	char *p;
	double seconds = 2.0;
	int depth = 64, ms;

	if ((p = strstr(line, "movetime"))) {
		seconds = atoi(p + 8) / 1000.0;
	} else if ((p = strstr(line, side == WHITE ? "wtime" : "btime"))) {
		ms = atoi(p + 5);
		seconds = ms / 30000.0;
	}
	if ((p = strstr(line, "depth")))
		depth = atoi(p + 5);
	if (seconds < 0.02)
		seconds = 0.02;

	search(seconds, depth);
	if (best_root.from == best_root.to) {
		printf("bestmove 0000\n");
	} else {
		char buf[8];
		move_str(best_root, buf);
		printf("bestmove %s\n", buf);
	}
	fflush(stdout);
}

/* Tiny console mode so the engine is playable without any GUI. */
static void console_loop(void) {
	char line[256];
	char buf[8];

	printf("Type moves like e2e4 (or 'quit'). Engine plays the other side.\n\n");
	print_board();
	for (;;) {
		printf("> ");
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin))
			return;
		line[strcspn(line, "\r\n")] = '\0';
		if (!strcmp(line, "quit"))
			return;
		if (!play_str(line)) {
			printf("illegal move\n");
			continue;
		}
		print_board();
		search(1.0, 64);
		if (best_root.from == best_root.to) {
			printf("game over (%s)\n", in_check(side) ? "mate" : "stalemate");
			return;
		}
		move_str(best_root, buf);
		printf("engine plays %s\n", buf);
		make_move(best_root);
		print_board();
	}
}

int main(void) {
	char line[8192];

	setvbuf(stdout, NULL, _IOLBF, 0);
	srand((unsigned)time(NULL));
	set_fen(START_FEN);

	while (fgets(line, sizeof(line), stdin)) {
		if (!strncmp(line, "uci", 3) && line[3] != 'n') {
			printf("id name %s\n", BOT_NAME);
			printf("id author you\n");
			printf("uciok\n");
		} else if (!strncmp(line, "isready", 7)) {
			printf("readyok\n");
		} else if (!strncmp(line, "ucinewgame", 10)) {
			set_fen(START_FEN);
		} else if (!strncmp(line, "position", 8)) {
			cmd_position(line);
		} else if (!strncmp(line, "go", 2)) {
			cmd_go(line);
		} else if (!strncmp(line, "d", 1) && line[1] <= ' ') {
			print_board();
		} else if (!strncmp(line, "perft", 5)) {
			int d = atoi(line + 5);
			printf("perft(%d) = %ld\n", d, perft(d));
		} else if (!strncmp(line, "play", 4)) {
			console_loop();
			return 0;
		} else if (!strncmp(line, "quit", 4)) {
			break;
		}
		fflush(stdout);
	}
	return 0;
}
