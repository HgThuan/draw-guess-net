CC       := gcc
WARN     := -std=c99 -Wall -Wextra -Werror
CFLAGS   := $(WARN) -O2
COVFLAGS := $(WARN) -O0 -g --coverage
ASANFLAGS := $(WARN) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
INCLUDES := -Icommon -Iserver/src -Iclient/libnetsketch

BUILD_DIR    := build
COV_DIR      := $(BUILD_DIR)/cov
COV_HTML_DIR := $(BUILD_DIR)/coverage
ASAN_DIR     := $(BUILD_DIR)/asan

SERVER_BIN := $(BUILD_DIR)/netsketch-server
CLIENT_LIB := $(BUILD_DIR)/libnetsketch.so
UNIT_BIN   := $(BUILD_DIR)/unit-tests

COV_SERVER_BIN := $(COV_DIR)/netsketch-server
COV_UNIT_BIN   := $(COV_DIR)/unit-tests

ASAN_SERVER_BIN := $(ASAN_DIR)/netsketch-server
ASAN_UNIT_BIN   := $(ASAN_DIR)/unit-tests

COMMON_SRCS       := $(wildcard common/*.c)
SERVER_SRCS       := $(wildcard server/src/*.c)
SERVER_LOGIC_SRCS := $(filter-out server/src/main.c, $(SERVER_SRCS))
CLIENT_SRCS       := $(wildcard client/libnetsketch/*.c)
TEST_SRCS         := $(wildcard tests/unit/*.c)

.PHONY: all test coverage asan clean

all: $(SERVER_BIN) $(CLIENT_LIB)

$(BUILD_DIR) $(COV_DIR) $(COV_HTML_DIR) $(ASAN_DIR):
	mkdir -p $@

$(SERVER_BIN): $(COMMON_SRCS) $(SERVER_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(CLIENT_LIB): $(COMMON_SRCS) $(CLIENT_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fPIC -shared $(INCLUDES) $^ -o $@

$(UNIT_BIN): $(COMMON_SRCS) $(SERVER_LOGIC_SRCS) $(CLIENT_SRCS) $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lcmocka -o $@

test: $(UNIT_BIN)
	./$(UNIT_BIN)

$(COV_SERVER_BIN): $(COMMON_SRCS) $(SERVER_SRCS) | $(COV_DIR)
	$(CC) $(COVFLAGS) $(INCLUDES) $^ -o $@

$(COV_UNIT_BIN): $(COMMON_SRCS) $(SERVER_LOGIC_SRCS) $(CLIENT_SRCS) $(TEST_SRCS) | $(COV_DIR)
	$(CC) $(COVFLAGS) $(INCLUDES) $^ -lcmocka -o $@

coverage: | $(COV_DIR) $(COV_HTML_DIR)
	find $(COV_DIR) -name '*.gcda' -delete
	$(MAKE) $(COV_SERVER_BIN) $(COV_UNIT_BIN)
	./$(COV_UNIT_BIN)
	gcovr -r . \
		--exclude 'tests/.*' \
		--exclude 'tools/.*' \
		--exclude 'build/.*' \
		--exclude 'server/src/main\.c' \
		--print-summary \
		--html-details $(COV_HTML_DIR)/index.html \
		--xml $(BUILD_DIR)/coverage.xml

$(ASAN_SERVER_BIN): $(COMMON_SRCS) $(SERVER_SRCS) | $(ASAN_DIR)
	$(CC) $(ASANFLAGS) $(INCLUDES) $^ -o $@

$(ASAN_UNIT_BIN): $(COMMON_SRCS) $(SERVER_LOGIC_SRCS) $(CLIENT_SRCS) $(TEST_SRCS) | $(ASAN_DIR)
	$(CC) $(ASANFLAGS) $(INCLUDES) $^ -lcmocka -o $@

asan: $(ASAN_SERVER_BIN) $(ASAN_UNIT_BIN)
	./$(ASAN_UNIT_BIN)

clean:
	rm -rf $(BUILD_DIR)
