.PHONY: all clean

# release or debug
MODE = debug

CC = $(CROSS_COMPILE)gcc -c
LD = $(CROSS_COMPILE)gcc

CFLAGS.release = -s -O2 -Wall
CFLAGS.debug = -g -Wall

LDFLAGS.release = -s
LDFLAGS.debug = -g

CFLAGS.local = $(CFLAGS.$(MODE)) -Icfg_parse
LDFLAGS.local = $(LDFLAGS.$(MODE))

OUT = out/$(CROSS_COMPILE)$(MODE)/

all: $(OUT)afrd

clean:
	rm -rf $(OUT)

$(OUT)%.o: %.c $(OUT).stamp.dir
	$(CC) $(CFLAGS.local) $(CFLAGS) -o $@ $<

$(OUT)%.o: cfg_parse/%.c $(OUT).stamp.dir
	$(CC) $(CFLAGS.local) $(CFLAGS) -o $@ $<

$(OUT).stamp.dir:
	mkdir -p $(@D)
	touch $@

AFRD_SRC = main.c afrd.c sysfs.c cfg_parse.c cfg.c modes.c mstime.c uevent_filter.c colorspace.c

$(OUT)afrd: $(addprefix $(OUT),$(AFRD_SRC:.c=.o))
	$(LD) $(LDFLAGS.local) $(LDFLAGS) -o $@ $^
