CC=gcc
CFLAGS=`sdl2-config --cflags` -Wall -Wextra -std=c11
LDFLAGS=`sdl2-config --libs` -lSDL2_image -lSDL2_mixer -lSDL2_ttf -lm
TARGET=candycrush

all: $(TARGET)

$(TARGET): main.o
	$(CC) main.o -o $(TARGET) $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f $(TARGET) *.o
