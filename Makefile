VPATH	=	src
BINDIR 	=	bin
LOGDIR	=	logs
HERE	=	$(shell pwd)
AT	=	@
DOLLAR  = 	$$


CD	=	$(AT)cd
CP	=	$(AT)cp
ECHO	=	@echo
CAT	=	$(AT)cat
IF	=	$(AT)if
LN	=	$(AT)ln
MKDIR	=	$(AT)mkdir
MV	=	$(AT)mv
SED	=	$(AT)sed
RM	=	$(AT)rm
TOUCH	=	$(AT)touch
CHMOD	=	$(AT)chmod
DATE    =       $(AT)date
PERL	=	$(AT)perl
MEX	=	$(AT)$(MATLAB)/bin/mex
THRIFT	=	/usr/local/bin/thrift
AR	=	$(AT)ar
ARFLAGS	=	rcs


CC		=	$(AT) gcc
INCLUDES	=	-I$(HERE) -I. -I$(SRCDIR)
CFLAGS	 	= 	-c -g -Wall $(INCLUDES)
LDFLAGS		=	-lm -lpthread


# We start our mode with "mode" so we avoud the leading whitespace from the +=.
NEWMODE := mode

# This is the usual DEBUG mode trick. Add DEBUG=1 to the make command line to
# build without optimizations and with assertions ON.
ifeq ($(DEBUG),1)
	CFLAGS  += -O0 -DDEBUG
	NEWMODE += debug
else
	CFLAGS  += -DNDEBUG
	NEWMODE += nodebug
endif

ifeq ($(PROF),1)
	CFLAGS  += -pg
	LDFLAGS += -pg
	NEWMODE += profile
endif

# If the new mode does'n match the old mode, write the new mode to .buildmode.
# This forces a rebuild of all the objects files, because they depend on .buildmode.
OLDMODE := $(shell cat .buildmode 2> /dev/null)
ifneq ($(OLDMODE),$(NEWMODE))
  $(shell echo "$(NEWMODE)" > .buildmode)
endif

.PHONY: all
all:

.PHONY: clean
clean:
	$(ECHO) Cleaning...
	$(RM) -rf $(BINDIR)

.PHONY: rmlogs
rmlogs:
	$(ECHO) Erasing the logs...
	$(RM) -rf $(LOGDIR)

.PHONY: remake
remake: clean all


$(BINDIR)/clictcp: $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCC\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CC) -o $@ $@.o $(BINDIR)/libUtil.a $(LDFLAGS)

$(BINDIR)/srvctcp: $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCC\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CC) -o $@ $@.o $(BINDIR)/libUtil.a $(LDFLAGS)

# Rule to make the libUtil library
$(BINDIR)/libUtil.a: $(BINDIR)/util.o $(BINDIR)/md5.o $(BINDIR)/qbuffer.o $(BINDIR)/thr_pool.o
	$(ECHO) "[\033[01;32mCC\033[22;37m] building  $@"
	$(MKDIR) -p $(dir $@)
	$(AR) $(ARFLAGS) $@ $(BINDIR)/util.o $(BINDIR)/md5.o $(BINDIR)/qbuffer.o $(BINDIR)/thr_pool.o


# Rule for compiling c files.
$(BINDIR)/%.o : %.c .buildmode Makefile
	$(ECHO) "[\033[01;34mCC\033[22;37m] compiling $<"
	$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<


all: $(BINDIR)/clictcp $(BINDIR)/srvctcp


# Uncomment to debug the Makefile
#OLD_SHELL := $(SHELL)
#SHELL = $(warning [$@ ($^) ($?)]) $(OLD_SHELL)
