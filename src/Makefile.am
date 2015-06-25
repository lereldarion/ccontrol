SUBDIRS = common lib utils

# module
EXTRA_DIST = module/Makefile module/ccontrol.c

module/ccontrol.ko: 
	test -d $(builddir)/module || $(MKDIR_P) $(builddir)/module
	test -f $(builddir)/module/Makefile || $(LN_S) $(abs_top_srcdir)/src/module/Makefile $(builddir)/module/
	test -f $(builddir)/module/ccontrol.c || $(LN_S) $(abs_top_srcdir)/src/module/ccontrol.c $(builddir)/module/
	$(MAKE) -C $(KERNEL_SRC_PATH) M=$(abs_top_builddir)/src/module EXTRA_CFLAGS=-I$(abs_top_srcdir)/src/common modules

all-local: module/ccontrol.ko

clean-local:
	$(MAKE) -C $(KERNEL_SRC_PATH) M=$(abs_top_builddir)/src/module clean

INSTALL_MOD_PATH ?= $(DESTDIR)
install-exec-local:
	$(MAKE) -C $(KERNEL_SRC_PATH) M=$(abs_top_builddir)/src/module INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) modules_install

