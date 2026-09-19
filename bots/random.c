/*
 * bots/random.c - plays a uniformly random legal move. No eval, no search.
 * Useful as a sparring partner / sanity check for the core engine.
 */

#include <stdlib.h>

#include "chess.h"

const char *BOT_NAME = "random";
Move        best_root;

int search(double seconds, int max_depth) {
	Move list[MAX_MOVES], legal[MAX_MOVES];
	int  n, i, nlegal = 0;

	(void)seconds;
	(void)max_depth;

	n = gen_moves(list);
	for (i = 0; i < n; i++) {
		if (!make_move(list[i]))
			continue;
		unmake_move();
		legal[nlegal++] = list[i];
	}

	if (nlegal == 0) {
		best_root.from = best_root.to = 0;
		best_root.promo = best_root.flag = 0;
		return 0;
	}
	best_root = legal[rand() % nlegal];
	return 0;
}
