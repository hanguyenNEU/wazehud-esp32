# Reuse the simulator toolchain and dependencies, without its interactive main.
include sim/Makefile
UI_TEST_SRC := $(filter-out $(ROOT)/sim/main.c $(MAIN)/ui/ui_navigator.c,$(SRC))

.PHONY: ui-test
ui-test:
	@mkdir -p test/host/build
	$(CC) $(CFLAGS) test/host/ui_alerts_test.c $(UI_TEST_SRC) $(SDL_LIBS) -o test/host/build/ui_alerts_test
	test/host/build/ui_alerts_test

.PHONY: ui-input-test
ui-input-test:
	@mkdir -p test/host/build
	$(CC) $(CFLAGS) test/host/ui_input_test.c $(UI_TEST_SRC) $(SDL_LIBS) -o test/host/build/ui_input_test
	test/host/build/ui_input_test

.PHONY: ui-memory-test
ui-memory-test:
	@mkdir -p test/host/build
	$(CC) $(CFLAGS) test/host/memory_test.c $(UI_TEST_SRC) $(SDL_LIBS) -o test/host/build/ui_memory_test
	test/host/build/ui_memory_test

.PHONY: ui-all
ui-all: ui-test ui-input-test ui-memory-test
