CC ?= gcc

# QOACONV

TARGET_CONV ?= qoaconv
CFLAGS_CONV ?= -std=c99 -O3
LFLAGS_CONV ?= -lm

# Call `make HAS_DRLIBS=true` to compile with mp3/flac support
# Requires header files from https://github.com/mackron/dr_libs
# curl https://raw.githubusercontent.com/mackron/dr_libs/refs/heads/master/dr_mp3.h -o dr_mp3.h
# curl https://raw.githubusercontent.com/mackron/dr_libs/refs/heads/master/dr_flac.h -o dr_flac.h
ifeq ($(HAS_DRLIBS), true)
	CFLAGS_CONV := $(CFLAGS_CONV) -D QOACONV_HAS_DRMP3 -D QOACONV_HAS_DRFLAC
endif

# QOAPLAY
# Requires
# - https://github.com/floooh/sokol/blob/master/sokol_audio.h
# FIXME: not yet tested on Windows/macOS
TARGET_PLAY ?= qoaplay
CFLAGS_PLAY ?= -std=gnu99 -O3

ifeq ($(OS),Windows_NT)
	LFLAGS_PLAY ?= # defined in #pragma() in sokol_audio.h
	EXTS        ?= .exe
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		LFLAGS_PLAY ?= -pthread -framework AudioToolbox
	else
		LFLAGS_PLAY ?= -pthread -lasound
	endif
endif

all: $(TARGET_PLAY) $(TARGET_CONV)

play: $(TARGET_PLAY)
$(TARGET_PLAY):$(TARGET_PLAY).c qoa.h
	$(CC) $(CFLAGS_PLAY) $(TARGET_PLAY).c -o $(TARGET_PLAY) $(LFLAGS_PLAY)

conv: $(TARGET_CONV)
$(TARGET_CONV):$(TARGET_CONV).c qoa.h
	$(CC) $(CFLAGS_CONV) $(TARGET_CONV).c -o $(TARGET_CONV) $(LFLAGS_CONV)

.PHONY: clean
clean:
	$(RM) $(TARGET_PLAY)$(EXTS) $(TARGET_CONV)$(EXTS)
