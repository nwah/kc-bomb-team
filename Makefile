# Bomb Squad for the KC 85/3 and KC 85/4.
#
#   make            build dist/BOMB-SQUAD.KCC
#   make run        build and screenshot it under MAME (see tools/run.sh)
#   make clean

ZCC     ?= $(HOME)/z88dk/bin/zcc
TARGET  := bomb-squad
SRC     := src/main.c src/game.c src/view.c src/bombs.c src/udg.c src/hw.asm

all: dist/$(TARGET)

dist/$(TARGET): $(SRC) src/hw.h src/view.h src/game.h src/bombs.h src/udg.h
	@mkdir -p dist
	$(ZCC) +kc -o dist/$(TARGET) -create-app -m $(SRC)

run: all
	./tools/run.sh -m kc85_3

src/bombs.c: tools/gen_bombs.py
	python3 tools/gen_bombs.py > $@

clean:
	rm -rf dist build

.PHONY: all run clean
