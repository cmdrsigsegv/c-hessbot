/*
 * bots/default.c - A minimal chess bot with iterative-deepening
 * alpha-beta with captures-only quiescence, material + piece-square eval.
 * DEFAULT BOT IS IMPLEMENTED BY CLAUDE.
 */

#include <stdio.h>
#include <time.h>

#include "chess.h"

const char *BOT_NAME = "claude_bot";
Move        best_root;

static const int value[7] = { 0, 100, 320, 330, 500, 900, 0 };

// clang-format off
/* tables are written rank 8 (top) down to rank 1 (bottom) */
static const int pst_pawn[64] = {
	  0,   0,   0,   0,   0,   0,   0,   0,
	 50,  50,  50,  50,  50,  50,  50,  50,
	 10,  10,  20,  30,  30,  20,  10,  10,
	  5,   5,  10,  25,  25,  10,   5,   5,
	  0,   0,   0,  20,  20,   0,   0,   0,
	  5,  -5, -10,   0,   0, -10,  -5,   5,
	  5,  10,  10, -20, -20,  10,  10,   5,
	  0,   0,   0,   0,   0,   0,   0,   0
};

static const int pst_centre[64] = {
	-30, -20, -10, -10, -10, -10, -20, -30,
	-20, -10,   0,   0,   0,   0, -10, -20,
	-10,   0,  10,  15,  15,  10,   0, -10,
	-10,   5,  15,  20,  20,  15,   5, -10,
	-10,   0,  15,  20,  20,  15,   0, -10,
	-10,   5,  10,  15,  15,  10,   5, -10,
	-20, -10,   0,   5,   5,   0, -10, -20,
	-30, -20, -10, -10, -10, -10, -20, -30
};
// clang-format on

static int evaluate(void) {
	int sq, p, t, c, s, score = 0, idx;

	for (sq = 0; sq < 128; sq++) {
		if (OFFBOARD(sq)) {
			continue;
		}
		p = board[sq];
		if (p == EMPTY) {
			continue;
		}
		t   = PTYPE(p);
		c   = PCOL(p);
		idx = (c == WHITE) ? (7 - RANKOF(sq)) * 8 + FILEOF(sq) : RANKOF(sq) * 8 + FILEOF(sq);
		s   = value[t];
		if (t == PAWN) {
			s += pst_pawn[idx];
		} else if (t == KNIGHT || t == BISHOP) {
			s += pst_centre[idx];
		}
		score += (c == WHITE) ? s : -s;
	}
	return (side == WHITE) ? score : -score;
}

static struct timespec t_start;

static long   nodes;
static int    stop_search;
static double budget;
static int    root_ply;

static double elapsed(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t_start.tv_sec) + 1e-9 * (now.tv_nsec - t_start.tv_nsec);
}

/* Rough MVV-LVA: prefer capturing valuable pieces with cheap ones. */
static int move_score(Move m) {
	int victim = board[m.to];
	int s      = 0;
	if (victim != EMPTY) {
		s = 1000 + value[PTYPE(victim)] - value[PTYPE(board[m.from])] / 8;
	}
	if (m.promo) {
		s += 900;
	}
	return s;
}

static void sort_moves(Move *list, int n) {
	int  i, j, scores[MAX_MOVES];
	Move tmp;

	for (i = 0; i < n; i++) {
		scores[i] = move_score(list[i]);
	}
	for (i = 1; i < n; i++) { /* insertion sort, descending */
		int sc = scores[i];
		tmp    = list[i];
		for (j = i; j > 0 && scores[j - 1] < sc; j--) {
			scores[j] = scores[j - 1];
			list[j]   = list[j - 1];
		}
		scores[j] = sc;
		list[j]   = tmp;
	}
}

static int quiesce(int alpha, int beta) {
	Move list[MAX_MOVES];
	int  n, i, score;

	nodes++;
	score = evaluate();
	if (score >= beta) {
		return beta;
	}
	if (score > alpha) {
		alpha = score;
	}

	n = gen_moves(list);
	sort_moves(list, n);
	for (i = 0; i < n; i++) {
		if (board[list[i].to] == EMPTY && list[i].flag != F_EP) {
			continue; /* captures only */
		}
		if (!make_move(list[i])) {
			continue;
		}
		score = -quiesce(-beta, -alpha);
		unmake_move();
		if (score >= beta) {
			return beta;
		}
		if (score > alpha) {
			alpha = score;
		}
	}
	return alpha;
}

static int negamax(int depth, int alpha, int beta) {
	Move list[MAX_MOVES];
	int  n, i, score, legal = 0;

	if ((++nodes & 2047) == 0 && elapsed() > budget) {
		stop_search = 1;
	}
	if (stop_search) {
		return 0;
	}
	if (halfmove >= 100) {
		return 0;
	}

	if (depth <= 0) {
		return quiesce(alpha, beta);
	}

	n = gen_moves(list);
	sort_moves(list, n);
	for (i = 0; i < n; i++) {
		if (!make_move(list[i])) {
			continue;
		}
		legal++;
		score = -negamax(depth - 1, -beta, -alpha);
		unmake_move();
		if (stop_search) {
			return 0;
		}
		if (score >= beta) {
			return beta;
		}
		if (score > alpha) {
			alpha = score;
		}
	}
	if (legal == 0) {
		return in_check(side) ? -MATE + (ply - root_ply) : 0;
	}
	return alpha;
}

/* Searches the current position and stores the result in best_root. */
int search(double seconds, int max_depth) {
	Move list[MAX_MOVES];
	int  n, i, depth, score, best = 0, legal;
	Move best_this;
	char buf[8];

	budget      = seconds;
	stop_search = 0;
	nodes       = 0;
	root_ply    = ply;
	clock_gettime(CLOCK_MONOTONIC, &t_start);

	n = gen_moves(list);
	sort_moves(list, n);
	best_root.from = best_root.to = 0;
	best_root.promo = best_root.flag = 0;
	score                            = 0;

	for (depth = 1; depth <= max_depth; depth++) {
		best  = -INF;
		legal = 0;
		for (i = 0; i < n; i++) {
			if (!make_move(list[i])) {
				continue;
			}
			legal++;
			score = -negamax(depth - 1, -INF, -best);
			unmake_move();
			if (stop_search) {
				break;
			}
			if (score > best) {
				best      = score;
				best_this = list[i];
			}
		}
		if (stop_search || legal == 0) {
			break;
		}
		best_root = best_this;
		move_str(best_root, buf);
		if (best > MATE - MAX_PLY) {
			printf("info depth %d score mate %d nodes %ld pv %s\n", depth, +(MATE - best + 1) / 2,
				nodes, buf);
		} else if (best < -MATE + MAX_PLY) {
			printf("info depth %d score mate %d nodes %ld pv %s\n", depth, -(MATE + best + 1) / 2,
				nodes, buf);
		} else {
			printf("info depth %d score cp %d nodes %ld pv %s\n", depth, best, nodes, buf);
		}
		fflush(stdout);
		if (elapsed() > budget * 0.5) {
			break;
		}
	}
	return best;
}
