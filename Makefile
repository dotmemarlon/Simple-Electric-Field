C = gcc

all:
	$(C) main.c -Ofast  `sdl2-config --cflags --libs` -lm -o ElectricField
