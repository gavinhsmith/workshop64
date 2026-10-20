CC ?= cc
ROOT := ..
include core.mk

test: $(CORE_SRC) $(addprefix $(GEN)/,$(GEN_SRC)) $(CORE_HDR) test.c world.h
	$(CC) -std=c99 -Wall -Wextra -Werror $(CORE_INC) -o test_core $(CORE_SRC) $(addprefix $(GEN)/,$(GEN_SRC)) test.c -lm
	./test_core

.PHONY: test
