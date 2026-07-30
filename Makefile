# Build one binary per bot: `make <name>` builds bots/<name>.c + main.c
# into a binary called <name>. `make all` builds every bot.

CC     = gcc
CFLAGS = -O2 -Wall -Wextra -Werror -I.

BOTS_DIR := bots
BOTS     := $(basename $(notdir $(wildcard $(BOTS_DIR)/*.c)))

.PHONY: all clean $(BOTS)

all: $(BOTS)

$(BOTS): %: main.c $(BOTS_DIR)/%.c chess.h
	$(CC) $(CFLAGS) -o $@ main.c $(BOTS_DIR)/$@.c

clean:
	rm -f $(BOTS)
