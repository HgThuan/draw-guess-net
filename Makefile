CC       := gcc
CFLAGS   := -std=c99 -O2 -Wall -Wextra -Werror
INCLUDES := -Icommon -Iserver/src -Iclient/libnetsketch

BUILD_DIR  := build
SERVER_BIN := $(BUILD_DIR)/netsketch-server
CLIENT_LIB := $(BUILD_DIR)/libnetsketch.so
UNIT_BIN   := $(BUILD_DIR)/unit-tests

COMMON_SRCS := $(wildcard common/*.c)
SERVER_SRCS := $(wildcard server/src/*.c)
SERVER_LOGIC_SRCS := $(filter-out server/src/main.c, $(SERVER_SRCS))
CLIENT_SRCS := $(wildcard client/libnetsketch/*.c)
TEST_SRCS   := $(wildcard tests/unit/*.c)

.PHONY: all test clean

all: $(SERVER_BIN) $(CLIENT_LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SERVER_BIN): $(COMMON_SRCS) $(SERVER_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(CLIENT_LIB): $(COMMON_SRCS) $(CLIENT_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -fPIC -shared $(INCLUDES) $^ -o $@

$(UNIT_BIN): $(COMMON_SRCS) $(SERVER_LOGIC_SRCS) $(CLIENT_SRCS) $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -lcmocka -o $@

test: $(UNIT_BIN)
	./$(UNIT_BIN)

clean:
	rm -rf $(BUILD_DIR)
