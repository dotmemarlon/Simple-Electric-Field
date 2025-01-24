CC = gcc
EMCC = emcc
SRC_NAME = main.c
PROGRAM_NAME = SimpleElectricField
CC_FLAGS = -Ofast `sdl2-config --libs --cflags` -lm
EMCC_FLAGS = -O3 -ffast-math  -s WASM=1 -s USE_SDL=2

all:
	${CC} ${SRC_NAME} ${CC_FLAGS} -o ${PROGRAM_NAME}

emcc:
	${EMCC} ${SRC_NAME} ${EMCC_FLAGS} -o index.js
