CXX := gcc
CFLAGS := -g -Wall
TARGETS := atoucli \
	atousrv

SRCS := $(TARGETS:%=%.c)

OBJS := $(TARGETS:%=%.o)

TEST_SRCS := \
#	util_test.cpp
# Add test source files here.

# Objects to link into test binary. Can't link main.
TEST_OBJS := $(TEST_SRCS:.c=.o) $(filter-out main.o,$(OBJS))
# Rename test objs to .test.o instead of .o so we can build non-cilk object
# files for testing.
TEST_OBJS := $(TEST_OBJS:.o=.test.o)

# Point to the root of Google Test, relative to where this file is
GTEST_DIR = gtest

# We start our mode with "mode" so we avoud the leading whitespace from the +=.
NEWMODE := mode

# This is the usual DEBUG mode trick. Add DEBUG=1 to the make command line to 
# build without optimizations and with assertions ON. 
ifeq ($(DEBUG),1)
	CFLAGS := -DDEBUG -O0 $(CFLAGS)
	NEWMODE += debug
else
	CFLAGS := -O3 -ffast-math -DNDEBUG $(CFLAGS)
	NEWMODE += nodebug
endif

# If the new mode does'n match the old mode, write the new mode to .buildmode.
# This forces a rebuild of all the objects files, because they depend on .buildmode.
OLDMODE := $(shell cat .buildmode 2> /dev/null)
ifneq ($(OLDMODE),$(NEWMODE))
  $(shell echo "$(NEWMODE)" > .buildmode)
endif

all: $(OBJS)

# Rule for compiling cpp files.
$(OBJS) : %.o:  %.c scoreboard.h .buildmode Makefile
	$(CXX) $(CFLAGS) $< scoreboard.h -o $@

clean:
	$(RM) $(TARGETS) $(OBJS) .buildmode \
	$gtest.a *.o *.d

