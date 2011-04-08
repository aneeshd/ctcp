CXX := gcc
CFLAGS := -g -Wall
LDFLAGS := -lm

# Put here the name of all the binaries
TARGETS := util \
	md5 \
	qbuffer \
	clictcp \
	srvctcp \

# Common libraries to be built and included to the products
UTILS := util \
	md5 \
	qbuffer \

SRCS := $(TARGETS:%=%.c)

OBJS := $(TARGETS:%=%.o)

PRODUCTS := $(filter-out $(UTILS) , $(TARGETS))

UTILS_PRODS := $(UTILS:%=%.o)

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
$(PRODUCTS): $(OBJS) .buildmode Makefile
	$(CXX) -o $@ $(UTILS_PRODS) $@.o $(LDFLAGS) 

# Rule for compiling c files.
%.o :  %.c .buildmode Makefile
	$(CXX) $(CFLAGS) -c $< -o $@

tags: $(SRCS)
	ctags -eR

tests: test.c util.o clictcp.o srvctcp.o md5.o .buildmode Makefile
	$(CXX) $(CFLAGS) $< util.o md5.o -o test $(LDFLAGS)

md5: md5driver.c md5.o
	$(CXX) $(CFLAGS) $< md5.o -o $@ $(LDFLAGS)

clean:
	$(RM) $(TARGETS) $(OBJS) .buildmode TAGS test\
	*.o *.d

clean_logs:
	$(RM) *.log

clean_rcv:
	$(RM) Rcv_*

# Uncomment to debug the Makefile
#OLD_SHELL := $(SHELL)
#SHELL = $(warning [$@ ($^) ($?)]) $(OLD_SHELL)
