#-------------------------------------------------------------------------
#  Copyright 2016, Daan Leijen. 
#-------------------------------------------------------------------------

.PHONY : clean dist init tests staticlib main

include out/makefile.config

ifndef $(VARIANT)
VARIANT=debug
endif

CONFIGDIR  = out/$(CONFIG)
OUTDIR 		 = $(CONFIGDIR)/$(VARIANT)
INCLUDES   = -Iinc -I$(CONFIGDIR)

ifeq ($(VARIANT),release)
CCFLAGS    = $(CCFLAGSOPT) -DNDEBUG $(INCLUDES)
CXXFLAGS   = $(CXXFLAGSOPT) -DNDEBUG $(INCLUDES)
else ifeq ($(VARIANT),testopt)
CCFLAGS    = $(CCFLAGSOPT) $(INCLUDES)
CXXFLAGS   = $(CXXFLAGSOPT) $(INCLUDES)
else ifeq ($(VARIANT),debug)
CCFLAGS    = $(CCFLAGSDEBUG) $(INCLUDES)
CXXFLAGS   = $(CXXFLAGSDEBUG) $(INCLUDES)
else
VARIANTUNKNOWN=1
endif


# -------------------------------------
# Sources
# -------------------------------------

SRCFILES = libhandler.c

TESTFILES= main-tests.c tests.c \
					 test-exn.c test-state.c test-amb.c test-dynamic.c test-raise.c test-general.c \
					 test-tailops.c test-state-alloc.c

BENCHFILES=main-perf.c perf.c tests.c test-state.c \
					 perf-counter.c


SRCS     = $(patsubst %,src/%,$(SRCFILES)) $(patsubst %,src/%,$(ASMFILES))
OBJS  	 = $(patsubst %.c,$(OUTDIR)/%$(OBJ), $(SRCFILES)) $(patsubst %$(ASM),$(OUTDIR)/%$(OBJ),$(ASMFILES))
HLIB     = $(OUTDIR)/libhandler$(LIB)

TESTSRCS = $(patsubst %,test/%,$(TESTFILES)) 
TESTMAIN = $(OUTDIR)/libh-tests$(EXE)

BENCHSRCS= $(patsubst %,test/%,$(BENCHFILES))
BENCHMAIN= $(OUTDIR)/libh-bench$(EXE)



TESTFILESXX	=main-tests.cpp tests.c \
						 test-destructor.cpp test-try.cpp \
						 test-exn.c test-state.c test-amb.c test-dynamic.c test-raise.c test-general.c \
						 test-tailops.c test-state-alloc.c

OUTDIRXX = $(OUTDIR)xx
SRCSXX   = $(patsubst %,src/%,$(SRCFILES)) $(patsubst %,src/%,$(ASMFILES))
OBJSXX	 = $(patsubst %.c,$(OUTDIRXX)/%$(OBJ), $(SRCFILES)) $(patsubst %$(ASM),$(OUTDIRXX)/%$(OBJ),$(ASMFILES))
HLIBXX   =$(OUTDIRXX)/libhandler++$(LIB)

TESTSRCSXX = $(patsubst %,test/%,$(TESTFILESXX)) 
TESTMAINXX = $(OUTDIRXX)/libh-tests++$(EXE)

# -------------------------------------
# Main targets
# -------------------------------------

main: init staticlib

tests: init staticlib testmain
	  @echo ""
		@echo "run tests"
		$(TESTMAIN)

bench: init staticlib benchmain
	  @echo ""
		@echo "run benchmark"
		$(BENCHMAIN)



mainxx: initxx staticlibxx

testsxx: initxx staticlibxx testmainxx
	  @echo ""
		@echo "run tests++"
		$(TESTMAINXX)

# -------------------------------------
# build tests
# -------------------------------------

testmain: $(TESTMAIN)

$(TESTMAIN): $(TESTSRCS) $(HLIB)
	$(CC) $(CCFLAGS)  $(LINKFLAGOUT)$@ $(TESTSRCS) $(HLIB)


testmainxx: $(TESTMAINXX)

$(TESTMAINXX): $(TESTSRCSXX) $(HLIBXX)
	$(CXX) $(CXXFLAGS)  $(LINKFLAGOUT)$@ $(TESTSRCSXX) $(HLIBXX)


# -------------------------------------
# build benchmark
# -------------------------------------

benchmain: $(BENCHMAIN)

$(BENCHMAIN): $(BENCHSRCS) $(HLIB)
	$(CC) $(CCFLAGS) $(LINKFLAGOUT)$@  $(BENCHSRCS) $(HLIB) -lm


benchmainxx: $(BENCHMAINXX)

$(BENCHMAINXX): $(BENCHSRCSXX) $(HLIBXX)
	$(CXX) $(CXXFLAGS) $(LINKFLAGOUT)$@  $(BENCHSRCSXX) $(HLIBXX)


# -------------------------------------
# build the static library
# -------------------------------------

staticlib: initxx $(HLIB)

$(HLIB): $(OBJS)
	$(AR) $(ARFLAGS)  $(ARFLAGOUT)$@ $(OBJS) 

$(OUTDIR)/%$(OBJ): src/%.c
	$(CC) $(CCFLAGS) $(CCFLAG99) $(CCFLAGOUT)$@ -c $< 

$(OUTDIR)/%$(OBJ): src/%$(ASM)
	$(CC) $(ASMFLAGS)  $(ASMFLAGOUT)$@ -c $< 


staticlibxx: $(HLIBXX)

$(HLIBXX): $(OBJSXX)
	$(AR) $(ARFLAGS)  $(ARFLAGOUT)$@ $(OBJSXX) 

$(OUTDIRXX)/%$(OBJ): src/%.cpp
	$(CXX) $(CXXFLAGS) $(CCFLAGOUT)$@ -c $< 

$(OUTDIRXX)/%$(OBJ): src/%.c
	$(CXX) $(CXXFLAGS) $(CCFLAGOUT)$@ -c $< 

$(OUTDIRXX)/%$(OBJ): src/%$(ASM)
	$(CXX) $(ASMFLAGS)  $(ASMFLAGOUT)$@ -c $< 


# -------------------------------------
# other targets
# -------------------------------------

docs: 

clean:
	rm -rf $(CONFIGDIR)/*/*
	touch $(CONFIGDIR)/makefile.depend

init:
	@echo "use 'make help' for help"
	@echo "build variant: $(VARIANT), configuration: $(CONFIG)"
	@if test "$(VARIANTUNKNOWN)" = "1"; then echo ""; echo "Error: unknown build variant: $(VARIANT)"; echo "Use one of 'debug', 'release', or 'testopt'"; false; fi
	@if test -d "$(OUTDIR)/asm"; then :; else $(MKDIR) "$(OUTDIR)/asm"; fi

initxx: init	
	@if test -d "$(OUTDIRXX)/asm"; then :; else $(MKDIR) "$(OUTDIRXX)/asm"; fi

help:
	@echo "Usage: make <target>"
	@echo "Or   : make VARIANT=<variant> <target>"
	@echo ""
	@echo "Variants:"
	@echo "  debug       : Build a debug version (default)"
	@echo "  testopt     : Build an optimized version but with assertions"
	@echo "  release     : Build an optimized release version"
	@echo ""
	@echo "Targets:"
	@echo "  main        : Build a static library (default)"
	@echo "  tests       : Run tests"
	@echo "  bench       : Run benchmarks, use 'VARIANT=release'"	
	@echo "  clean       : Clean output directory"
	@echo "  depend      : Generate dependencies"
	@echo ""
	@echo "Configuration:"
	@echo "  output dir  : $(OUTDIR)"
	@echo "  c-compiler  : $(CC) $(CCFLAGS)"
	@echo ""

# dependencies
# [gcc -MM] generates the dependencies without the full
# directory name, ie.
#  evaluator.o: ...
# instead of
#  core/evaluator.o: ..
# we therefore use [sed] to append the directory name
depend: init
	$(CCDEPEND) $(INCLUDES) src/*.c > $(CONFIGDIR)/temp.depend
	sed -e "s|\(.*\.o\)|$(CONFIGDIR)/\$$(VARIANT)/\1|g" $(CONFIGDIR)/temp.depend > $(CONFIGDIR)/makefile.depend
	$(CCDEPEND) $(INCLUDES) test/*.c > $(CONFIGDIR)/temp.depend
	sed -e "s|\(.*\.o\)|$(CONFIGDIR)/\$$(VARIANT)/\1|g" $(CONFIGDIR)/temp.depend >> $(CONFIGDIR)/makefile.depend
	$(RM) $(CONFIGDIR)/temp.depend

include $(CONFIGDIR)/makefile.depend