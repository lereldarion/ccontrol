AM_CFLAGS = -I$(top_srcdir)/src/lib/ -I$(top_srcdir)/src/common/
LDADD = $(top_srcdir)/src/lib/libccontrol.la
bin_PROGRAMS = ccontrol

ccontrol_SOURCES = main.c
ccontrol_CFLAGS = $(AM_CFLAGS) 
ccontrol_LDADD = $(LDADD)
