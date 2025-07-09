CC=gcc
#CFLAGS = -Wall -Wextra -Werror
CFLAGS = -Wall -Wextra -Werror -Iinclude \
         -g -O0 -fno-inline -fno-omit-frame-pointer
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))
TARGET = sim

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o $(TARGET)
