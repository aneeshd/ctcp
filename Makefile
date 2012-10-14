VPATH	=	src
BINDIR 	=	bin
LOGDIR	=	logs
FIGDIR	=	figs
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
CXX             =	$(AT) g++
FPIC		=	-fPIC
INCLUDES	=	-I$(HERE) -I. -I$(SRCDIR)
CFLAGS	 	= 	-c -g -Wall $(INCLUDES)
LDFLAGS		= -lnsl 

CWARN			=	-Wall -Wno-sign-compare -Wno-unused-variable
CXXWARN			=	$(CWARN) $(FPIC) -Wno-deprecated -Woverloaded-virtual

COMMON_CFLAGS		=	-c -g -std=gnu99 -D_GNU_SOURCE=1 \
					-D_REENTRANT  $(CWARN) $(FPIC)\

COMMON_CXXFLAGS		=	-c -g $(CXXWARN) -D__cplusplus


DBG_CFLAGS		=	$(COMMON_CFLAGS) -DDEBUG_MODE=1
DBG_CXXFLAGS		=	$(COMMON_CXXFLAGS) -DDEBUG_MODE=1
OPTIMIZATION_FLAGS	=	-O3
OPT_CFLAGS		=	$(COMMON_CFLAGS) -DNDEBUG \
				$(OPTIMIZATION_FLAGS) -fno-omit-frame-pointer
OPT_CXXFLAGS		=	$(COMMON_CXXFLAGS) -DNDEBUG \
				$(OPTIMIZATION_FLAGS) -fno-omit-frame-pointer

COMMON_LDFLAGS		=	-g $(FPIC) -Wl,--eh-frame-hdr -L$(ZLIBDIR) -lm -lpthread
DBG_LDFLAGS		=	$(COMMON_LDFLAGS)
OPT_LDFLAGS		=	$(COMMON_LDFLAGS) -O3 -fno-omit-frame-pointer

ifneq ($(strip $(OPT)),)
	CFLAGS		=	$(OPT_CFLAGS)
	CXXFLAGS	=	$(OPT_CXXFLAGS)
	LDFLAGS		=	$(OPT_LDFLAGS)
else
	CFLAGS		=	$(DBG_CFLAGS)
	CXXFLAGS	=	$(DBG_CXXFLAGS)
	LDFLAGS		=	$(DBG_LDFLAGS)
endif


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

# Rule for compiling c files.
$(BINDIR)/%.o : %.c .buildmode Makefile
	$(ECHO) "[\033[01;34mCC\033[22;37m] compiling $<"
	$(MKDIR) -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

# Rule for compiling c++ files.
$(BINDIR)/%.o: %.cpp
	$(ECHO) "[\033[01;34mCXX\033[22;37m] compiling $<"
	$(MKDIR) -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $<

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
	$(RM) -rf srvctcp clictcp
	$(RM) -rf demoServer demoClient
	$(RM) -rf proxy_local proxy_remote
	$(RM) -rf nftpServer nftpClient

.PHONY: remake
remake: clean proxy

clictcp: $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCXX\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CXX) -o $@ $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a $(LDFLAGS)

srvctcp: $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCXX\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CXX) -o $@ $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a $(LDFLAGS)

# Rule to make the libUtil library
$(BINDIR)/libUtil.a: $(BINDIR)/util.o $(BINDIR)/md5.o $(BINDIR)/qbuffer.o $(BINDIR)/thr_pool.o $(BINDIR)/fifo.o 
	$(ECHO) "[\033[01;32mCC\033[22;37m] building  $@"
	$(MKDIR) -p $(dir $@)
	$(AR) $(ARFLAGS) $@ $(BINDIR)/util.o $(BINDIR)/md5.o $(BINDIR)/qbuffer.o $(BINDIR)/thr_pool.o $(BINDIR)/fifo.o  

demoServer: $(BINDIR)/demoServer.o .buildmode Makefile
	$(ECHO) "[\033[01;33mCC\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CC) -o $@ $< $(LDFLAGS) 

demoClient: $(BINDIR)/demoClient.o .buildmode Makefile
	$(ECHO) "[\033[01;33mCC\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CC) -o $@ $< $(LDFLAGS) -lreadline

proxy_local: $(BINDIR)/proxy_local.o $(BINDIR)/child_local.o  $(BINDIR)/misc_local.o $(BINDIR)/up_proxy.o $(BINDIR)/error.o $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCXX\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CXX) -o $@ $(BINDIR)/proxy_local.o $(BINDIR)/child_local.o  $(BINDIR)/misc_local.o $(BINDIR)/up_proxy.o $(BINDIR)/error.o $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a $(LDFLAGS) -lrt

proxy_remote: $(BINDIR)/proxy_remote.o $(BINDIR)/child_remote.o  $(BINDIR)/misc_remote.o $(BINDIR)/up_proxy.o $(BINDIR)/error.o $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCXX\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CXX) -o $@ $(BINDIR)/proxy_remote.o $(BINDIR)/child_remote.o  $(BINDIR)/misc_remote.o $(BINDIR)/up_proxy.o $(BINDIR)/error.o $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a $(LDFLAGS)

nftpServer: $(BINDIR)/nftpServer.o $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCXX\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CXX) -o $@ $(BINDIR)/nftpServer.o $(BINDIR)/srvctcp.o $(BINDIR)/libUtil.a $(LDFLAGS)

nftpClient: $(BINDIR)/nftpClient.o $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a .buildmode Makefile
	$(ECHO) "[\033[01;33mCXX\033[22;37m] linking $@"
	$(MKDIR) -p $(dir $@)
	$(CXX) -o $@ $(BINDIR)/nftpClient.o $(BINDIR)/clictcp.o $(BINDIR)/libUtil.a $(LDFLAGS) -lrt

all: proxy

demo: demoClient demoServer

proxy: proxy_local proxy_remote

nftp: nftpServer nftpClient



# Uncomment to debug the Makefile
#OLD_SHELL := $(SHELL)
#SHELL = $(warning [$@ ($^) ($?)]) $(OLD_SHELL)
