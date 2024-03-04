CC ?= gcc

# QOACONV
# Requires
# - https://github.com/mackron/dr_libs/blob/master/dr_mp3.h
# - https://github.com/mackron/dr_libs/blob/master/dr_flac.h
# Remove -D QOACONV_HAS_DRMP3 and -D QOACONV_HAS_DRFLAC to compile qoaconv
# without MP3/FLAC support
TARGET_CONV ?= qoaconv
CFLAGS_CONV ?= -std=c99 -O3 -D QOACONV_HAS_DRMP3 -D QOACONV_HAS_DRFLAC
LFLAGS_CONV ?= -lm


# QOAPLAY
# Requires
# - https://github.com/floooh/sokol/blob/master/sokol_audio.h
# FIXME: not yet tested on Windows/macOS
TARGET_PLAY ?= qoaplay
CFLAGS_PLAY ?= -std=gnu99 -O3

ifeq ($(OS),Windows_NT)
	LFLAGS_PLAY ?= # defined in #pragma() in sokol_audio.h
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
	$(RM) $(TARGET_PLAY) $(TARGET_CONV)
