CC = gcc
TARGET = sim
SRCS = $(wildcard *.c)

DEBUG_DIR = debug
PROFILE_DIR = profile
RELEASE_DIR = release

CFLAGS_DEBUG   = -Wall -Wextra -Werror -Iinclude -g3 -O0 -fno-inline -fno-omit-frame-pointer
CFLAGS_PROFILE = -Wall -Wextra -Werror -Iinclude -g  -O2 -fno-inline -fno-omit-frame-pointer -DNDEBUG
CFLAGS_RELEASE = -Wall -Wextra -Werror -Iinclude -O2 -DNDEBUG

.PHONY: all debug release clean

all: debug

debug: OUT_DIR = $(DEBUG_DIR)
debug: CFLAGS = $(CFLAGS_DEBUG)
debug: $(DEBUG_DIR)/$(TARGET)

release: OUT_DIR = $(RELEASE_DIR)
release: CFLAGS = $(CFLAGS_RELEASE)
release: $(RELEASE_DIR)/$(TARGET)

profile: OUT_DIR = $(PROFILE_DIR)
profile: CFLAGS = $(CFLAGS_PROFILE)
profile: $(PROFILE_DIR)/$(TARGET)

# Link the final binary
$(DEBUG_DIR)/$(TARGET): $(SRCS:%.c=$(DEBUG_DIR)/%.o)
	$(CC) $(CFLAGS) -o $@ $^

$(PROFILE_DIR)/$(TARGET): $(SRCS:%.c=$(PROFILE_DIR)/%.o)
	$(CC) $(CFLAGS) -o $@ $^

$(RELEASE_DIR)/$(TARGET): $(SRCS:%.c=$(RELEASE_DIR)/%.o)
	$(CC) $(CFLAGS) -o $@ $^

# Compile each .c into mode-specific .o
$(DEBUG_DIR)/%.o: %.c
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(PROFILE_DIR)/%.o: %.c
	@mkdir -p $(PROFILE_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RELEASE_DIR)/%.o: %.c
	@mkdir -p $(RELEASE_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(DEBUG_DIR) $(PROFILE_DIR) $(RELEASE_DIR)
