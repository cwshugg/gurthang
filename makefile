# A simple makefile to build my project.
#
#   Connor Shugg

CC=clang

# Include variables (CHANGE THESE IF/WHEN NEEDED)
AFLPP_INCLUDE=~/fuzzing/afl/AFLplusplus/include
MEMCHECK_INCLUDE=~/workspace/fuzzing/memcheck_include

# output file names
MUTATOR_BINARY=gurthang-mutator.so
PRELOAD_BINARY=gurthang-preload.so
COMUX_BINARY=comux-tool

# test variables
TEST_BINARY=mtest
TEST=you_need_to_specify_a_C_source_test_file

# compilation variables
CFLAGS=-g -Wall
PRELOAD_CFLAGS=-g -Wall -fPIC -shared
PRELOAD_LDLIBS=-ldl

# other variables
C_NONE="\\033[0m"
C_ACCENT1="\\033[33m"
C_ACCENT2="\\033[35m"

# source files
SRC_DIR=./src
UTILS_DIR=$(SRC_DIR)/utils
COMUX_DIR=$(SRC_DIR)/comux

default: all

# Build both libraries: the mutator and the preload library
all: mutator preload comux-toolkit

# Build the mutator shared library
mutator:
	@ echo -e "$(C_ACCENT1)Building mutator library$(C_NONE)"
	$(CC) $(CFLAGS) -D_FORTIFY_SOURCE=2 -O3 -fPIC -shared -g \
		-I $(AFLPP_INCLUDE) \
		$(SRC_DIR)/mutator.c $(UTILS_DIR)/*.c $(COMUX_DIR)/comux.c \
		-o $(MUTATOR_BINARY)

mutator-memcheck:
	@ echo -e "$(C_ACCENT1)Building mutator library.$(C_NONE) $(C_ACCENT2)(for memcheck)$(C_NONE)"
	$(CC) $(CFLAGS) -D_FORTIFY_SOURCE=2 -O3 -fPIC -shared -g \
		-I $(AFLPP_INCLUDE) -I $(MEMCHECK_INCLUDE) \
		$(SRC_DIR)/mutator.c $(UTILS_DIR)/*.c $(COMUX_DIR)/comux.c \
		-o $(MUTATOR_BINARY)


# Build the LD_PRELOAD shared library
preload:
	@ echo -e "$(C_ACCENT1)Building preload library.$(C_NONE)"
	$(CC) $(PRELOAD_CFLAGS) \
		$(SRC_DIR)/preload.c $(UTILS_DIR)/*.c $(COMUX_DIR)/comux.c \
		-o $(PRELOAD_BINARY) \
		$(PRELOAD_LDLIBS)

comux-toolkit:
	@ echo -e "$(C_ACCENT1)Building comux toolkit.$(C_NONE)"
	$(CC) -g $(CFLAGS) \
		$(UTILS_DIR)/*.c $(COMUX_DIR)/*.c \
		-o $(COMUX_BINARY)

# Build a test
test:
	$(CC) $(CFLAGS) -g -o $(TEST_BINARY) $(TEST) \
		$(UTILS_DIR)/*.c $(COMUX_DIR)/comux.c

# Clean up extra junk
clean:
	rm -f $(MUTATOR_BINARY)
	rm -f $(PRELOAD_BINARY)
	rm -f $(COMUX_BINARY)
	rm -f $(TEST_BINARY)
	rm -f *.txt
