# automake config
AM_COLOR_TESTS = yes

# flags for building lib check programs
AM_CFLAGS = -I../src/lib/ -I../src/commons/
LDADD = ../src/lib/libccontrol.la

if TEST_VALGRIND
TESTS_ENVIRONMENT= @LIBTOOL@ --mode=execute @VALGRIND@ --tool=memcheck -q --leak-check=full
endif

random_SOURCES = random.c
random_CFLAGS = $(AM_CFLAGS)
random_LDADD = $(LDADD)
check_PROGRAMS = random

