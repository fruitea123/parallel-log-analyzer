CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -D_POSIX_C_SOURCE=200809L -Iinclude
TARGET = bin/log_analyzer
SRCS = src/main.c src/ipc.c src/log_stats.c src/worker.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | bin
	$(CC) $(OBJS) -o $@

bin:
	mkdir -p bin

src/%.o: src/%.c include/protocol.h include/ipc.h include/log_stats.h include/worker.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) sample_logs/app.log sample_logs/api.log sample_logs/db.log sample_logs/mixed.log

clean:
	rm -f $(OBJS) $(TARGET)
