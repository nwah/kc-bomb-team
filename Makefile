# Bomb Squad for the KC 85/3, KC 85/4 and the Z9001 / KC 85/1.10.
#
#   make            build dist/bomb-squad       (KC 85/3 + 4)
#   make kc85       build dist/bomb-squad.KCC    (same, explicit)
#   make z9001      build dist/bomb-squad-z9001  (raw binary + .map)
#   make run        build and screenshot KC85 under MAME
#   make z9001-run  build and screenshot Z9001 under MAME
#   make clean

ZCC     ?= $(HOME)/z88dk/bin/zcc
HOMEINC := -I$(CURDIR)/src -I$(CURDIR)/common
Z9INC   := -I$(CURDIR)/z9001 -I$(CURDIR)/common

# KC 85 (KC 85/3 + KC 85/4) target: shared game + KC85 view/hw.
KC_SRC  := src/main.c common/game.c src/view.c common/bombs.c src/udg.c src/hw.asm
KC_HDRS := src/hw.h src/view.h common/game.h common/bombs.h src/udg.h
TARGET  := bomb-squad

# Z9001 (KC 85/1.10 / KC 85/1): shared game + Z9001 view/hw, 16K RAM layout.
Z9_SRC  := z9001/main.c common/game.c z9001/view.c common/bombs.c z9001/hw.asm z9001/hw.c
Z9_HDRS := z9001/hw.h z9001/view.h common/game.h common/bombs.h
Z9_TARGET := bomb-squad-z9001

all: dist/$(TARGET)

dist/$(TARGET): $(KC_SRC) $(KC_HDRS)
	@mkdir -p dist
	$(ZCC) +kc $(HOMEINC) -o dist/$(TARGET) -create-app -m $(KC_SRC)

kc85: dist/$(TARGET)

# Flat, injection-ready binary at $0300 with the stack at $4000.
dist/$(Z9_TARGET): $(Z9_SRC) $(Z9_HDRS)
	@mkdir -p dist
	$(ZCC) +z9001 -zorg=0x0300 -pragma-define:REGISTER_SP=0x4000 $(Z9INC) \
		-Onl -o dist/$(Z9_TARGET) -m $(Z9_SRC)

z9001: dist/$(Z9_TARGET) check-z9001

# A Z9001 fits in 16K ($0000-$3FFF) with the stack at $4000, so the linker's
# high-water mark (__tail) must clear $3E00. Fail the build otherwise.
check-z9001: dist/$(Z9_TARGET) dist/$(Z9_TARGET).map
	./tools/check-size.sh dist/$(Z9_TARGET)

# Also emit a KC-TAPE image for real hardware / JKCEMU.
dist/z9001.tap: $(Z9_SRC) $(Z9_HDRS)
	@mkdir -p dist
	$(ZCC) +z9001 -zorg=0x0300 -pragma-define:REGISTER_SP=0x4000 $(Z9INC) \
		-Onl -o dist/z9001.tap -create-app -m $(Z9_SRC)

z9001-tap: dist/z9001.tap

run: all
	./tools/run.sh -m kc85_3

z9001-run: z9001
	./tools/run.sh -m z9001

# The bomb definitions are generated, target-independent, and shared by both
# builds -- one source in common/, regenerated when the table changes.
common/bombs.c: tools/gen_bombs.py
	python3 tools/gen_bombs.py > $@

clean:
	rm -rf dist build

.PHONY: all kc85 z9001 z9001-tap run z9001-run clean
