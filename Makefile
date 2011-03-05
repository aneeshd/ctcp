CXX := gcc
CFLAGS := -g -Wall
LDFLAGS :=

TARGETS := util \
	atoucli \
	atousrv \
	test \

HEADERS := scoreboard.h \

SRCS := $(TARGETS:%=%.c)

OBJS := $(TARGETS:%=%.o)

PRODUCTS := $(filter-out util, $(TARGETS))

# We start our mode with "mode" so we avoud the leading whitespace from the +=.
NEWMODE := mode

# This is the usual DEBUG mode trick. Add DEBUG=1 to the make command line to 
# build without optimizations and with assertions ON. 
ifeq ($(DEBUG),1)
	CFLAGS +=  -O0 -DDEBUG
	NEWMODE += debug
else
	CFLAGS += -DNDEBUG
	NEWMODE += nodebug
endif

# If the new mode does'n match the old mode, write the new mode to .buildmode.
# This forces a rebuild of all the objects files, because they depend on .buildmode.
OLDMODE := $(shell cat .buildmode 2> /dev/null)
ifneq ($(OLDMODE),$(NEWMODE))
  $(shell echo "$(NEWMODE)" > .buildmode)
endif

all: $(PRODUCTS)

# Rule for linking the .o binaries
$(PRODUCTS): $(OBJS) .buildmode
	$(CXX) -o $@ $< $@.o $(LDFLAGS)

# Rule for compiling c files.
$(OBJS) : %.o :  %.c $(HEADERS) .buildmode Makefile
	$(CXX) $(CFLAGS) -c $< -o $@

tags: $(SRCS)
	ctags -eR

clean:
	$(RM) $(TARGETS) $(OBJS) .buildmode TAGS \
	*.o *.d *.tmp


# Uncomment to debug the Makefile
#OLD_SHELL := $(SHELL)
#SHELL = $(warning [$@ ($^) ($?)]) $(OLD_SHELL)
