#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "chess.h"

#define length(a) (int)(sizeof(a) / sizeof(*(a)))
#define max(a, b)                                                                                  \
	({                                                                                             \
		__typeof__(a) _a = (a);                                                                    \
		__typeof__(b) _b = (b);                                                                    \
		_a > _b ? _a : _b;                                                                         \
	})

/* Example of using compare:
 *   float f[] = {2, 1, 6, 4, 5, 3};
 *   int   i[] = {0, 1, 2, 3, 4, 5};
 *   qsort_r(i, length(i), sizeof(*i), cmp, f); <- f will be the array of scores
 *   printf("%g %g %g %g %g %g\n",
 *          f[i[0]], f[i[1]], f[i[2]], f[i[3]], f[i[4]], f[i[5]]);
 * > 1 2 3 4 5 6
 */

static int cmp(const void *pa, const void *pb, void *arg) {
	int  a = *(int *)pa;
	int  b = *(int *)pb;
	int *f = arg;
	return f[a] < f[b] ? -1 : f[a] > f[b] ? +1 : 0;
}

const char *BOT_NAME = "simple_bot";
Move        best_root;

int   position_eval(void);
int   negamax(int depth, int colour, int alpha, int beta);
Move *sort_moves(int n, Move *list, int *out_legal);
int   position_change(Move candidate);
int   material_change(Move candidate);
int   position_score(int sq, int type, int colour);

static const int piecevalues[7] = {
	000, // Empty
	100, // Pawn
	300, // Bishop/knight
	300, // Bishop/knight
	500, // Rook
	900, // Queen
	0    // King
};

/* Searches the current position and stores the result in best_root. */
int search(double seconds, int max_depth) {
	Move list[MAX_MOVES];
	int  n, nlegal, score = MATE, us = side;
	int  our_sign = (us == WHITE) ? 1 : -1;

	n           = gen_moves(list);
	Move *legal = sort_moves(n, list, &nlegal);

	(void)seconds;
	(void)max_depth;

	int max = -INF;
	if (nlegal > 0) {
		best_root = legal[0];
	} else {
		best_root.from = best_root.to = 0;
		best_root.promo = best_root.flag = 0;

		score = 0;
		goto end_search;
	}

	for (int i = 0; i < nlegal; i++) {
		make_move(legal[i]); // now legal by construction
		score = -negamax(3, -our_sign, -INF, INF);
		unmake_move();
		if (score > max) {
			max       = score;
			best_root = legal[i];
		}
	}

end_search:
	free(legal);
	return score;
}

int move_eval(Move candidate) {
	// Note: The function assumes that the moves are legal.
	int score_change = 0;
	score_change += material_change(candidate);
	score_change += position_change(candidate);
	return score_change;
}

int material_change(Move candidate) {
	int score_change = 0;
	if (candidate.flag == F_EP) {
		// En passant capture, needs to be handled because the target square is empty.
		int captured_sq = candidate.to + ((side == WHITE) ? -16 : 16);
		int captured_p  = board[captured_sq];
		score_change += piecevalues[PTYPE(captured_p)];
	} else {
		int captured_p = board[candidate.to];
		if (captured_p != EMPTY) {
			score_change += piecevalues[PTYPE(captured_p)];
		}
	}
	return score_change;
}

int position_change(Move candidate) {
	int score_change = 0;
	(void)candidate;
	// int from_sq      = candidate.from;
	// int to_sq        = candidate.to;
	// int piece        = board[from_sq];
	// int type         = PTYPE(piece);
	// int colour       = PCOL(piece);

	// Does nothing for now.

	return score_change;
}

int position_eval(void) { // Both material and position evaluation, white-relative.
	int score = 0;
	for (int sq = 0; sq < 0x88; sq++) {
		if (OFFBOARD(sq)) {
			continue;
		}
		int p = board[sq];
		if (p == EMPTY) {
			continue;
		}
		int type   = PTYPE(p);
		int colour = PCOL(p);
		int value  = piecevalues[type];
		score += (colour == WHITE) ? value : -value;
		// score += position_score(sq, type, colour);
	}

	return score;
}

int position_score(int sq, int type, int colour) {
	int score = 0;
	(void)sq;
	(void)type;
	(void)colour;
	// Does nothing for now.
	return colour * score;
}

int negamax(int depth, int colour, int alpha, int beta) {
	if (depth == 0) {
		return colour * position_eval();
	}
	Move  list[MAX_MOVES];
	int   nlegal = 0;
	int   n      = gen_moves(list);
	Move *legal  = sort_moves(n, list, &nlegal);
	int   value  = -INF;
	for (int i = 0; i < nlegal; i++) {
		make_move(legal[i]);
		value = max(value, -negamax(depth - 1, -colour, -beta, -alpha));
		unmake_move();
		alpha = max(alpha, value);
		if (alpha >= beta) {
			break;
		}
	}
	free(legal);
	return value;
}

Move *sort_moves(int n, Move *list, int *out_legal) {
	// First remove all illegal moves, scoring each move as we go
	Move legal[MAX_MOVES];
	int  scores[MAX_MOVES];
	int  indices[MAX_MOVES];

	int us       = side;
	int j        = 0;
	int our_sign = (us == WHITE) ? 1 : -1;

	for (int i = 0; i < n; i++) {
		if (!make_move(list[i])) {
			// Illegal move
			continue;
		}
		legal[j]   = list[i];
		scores[j]  = our_sign * position_eval();
		indices[j] = j;
		unmake_move();
		j++;
	}
	// if j==0, no legal moves. Game is already lost.

	// Sort the move list by the scores assigned above
	qsort_r(indices, j, sizeof(*indices), cmp, scores);

	// NOTE: sorted_list needs to be free'd by the caller
	Move *sorted_list = malloc(j * sizeof(*sorted_list));
	for (int i = 0; i < j; i++) {
		sorted_list[i] = legal[indices[i]];
	}
	*out_legal = j;
	return sorted_list;
}