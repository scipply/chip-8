CC = g++
FILES = main.cpp
EXEC = chip8.exe
FLAGS = -Wall -Wextra -Werror -lmingw32 -lSDL2main -lSDL2

all: build

build:
	$(CC) -I src/include -L src/lib -o $(EXEC) $(FILES) $(FLAGS)

debug:
	$(CC) -I src/include -L src/lib -o $(EXEC) -g $(FILES) $(FLAGS) -DDEBUG

clean: 
	rm -rf $(EXEC)
