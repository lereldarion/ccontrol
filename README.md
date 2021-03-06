CControl: a software cache partitionning tool
==============================================

CControl (Cache Control) is a Linux kernel module and accompanying libraries to control the amount of memory cache data structures inside an application can use.

TL;DR
-----

Because a complete explanation of CControl requires knowledge of OS and hardware cache internals, you should probably read what follows.
For the lazy bastards, here is one short explanation:

	CControl provides a software cache partitioning library for Linux by implementing page coloring in kernel.
	The standard ./configure; make; make install should work.
	Look at ccontrol.h for API doc.

You _must_ be root to install and load/unload ccontrol.
This requirement is lifted once ccontrol is loaded in kernel.

Background Knowledge
--------------------

During execution, a program makes a number of accesses to physical memory (RAM).
To increase global performance, small and fast memory banks called caches are placed on the path between CPU and RAM.
These caches store recently accessed memory locations.

To understand how ccontrol work and what can (and cannot) be done with it, some knowledge of cache internals are required.

* Virtual memory: on most architectures/OS a process only manipulates virtual memory.
Physical memory (RAM) is abstracted and shared by several processes at the same time, but each process can only touch its own memory.
The OS maintains the mapping between virtual and physical memory, with the help of dedicated hardware to speed things up.

* Pages: both physical and virtual memory are split into pages, contiguous blocks of memory.
This pages are traditionally of 4 KB in size.

* Indexing: a cache identifies the memory locations accessed by computing a hash function over their address (the _index_).
Since both a virtual address and a physical one can be used to refer to the same memory location, caches are either _virtually_ or _physically_ indexed.

* Lines: saving memory one byte or one word at a time would be completely inefficient for a cache.
Instead, it saves a set of contiguous bytes in memory under the same index.
This set is called a line, and on most architectures it is either 32 or 64 bytes wide.

* Associativity: each time the CPU accesses memory, the cache must find if the line in question is already known.
Three configurations for this search can be found nowadays:
	* _direct-mapped_, an address has only one designated line it can be saved to
	Very fast to search, but not very efficient regarding some memory access patterns.
	* _fully-associative_, an address can be saved in all the cache.
	Very slow to search but very efficient from a memory-optimization point of view.
	* _set-associative_, a mix of the two.
	An address in memory can be saved in a set of lines in cache, allowing more flexibility regarding the memory access patterns benefiting from the cache and costing less than a fully-associative one.

* LRU: since a cache is smaller than RAM, each time a new memory location is accessed (and thus a new line fetched) an old line must be evicted (of course it only applies to associative caches).
The most widely-known and efficient algorithm to do that is Least-Recently-Used.

Page Coloring
-------------

The goal of ccontrol is to split the cache in several parts, allowing the user to give some of its data structures more cache than others.
Since hardware cache partitioning is restricted to a few specific architectures, ccontrol does that in software.
The method used is called page coloring and is quite known is the OS community.

	For any cache that is _not_ fully-associative, a color is defined as the set of pages that occupy the same associative sets.

Put simply, in a set-associative cache, multiple lines map to the same associative set.
Since these lines are also part of pages, we can group these pages by the associative sets they map to.

In the case of physically indexed caches, the OS is sole responsible for the physical pages (and thus the colors) that a process touches, as it is in charge of the mapping between virtual and physical pages.

CControl inject code inside the Linux kernel (module), reserve a part of the RAM for itself, identify the colors of each page and redistribute them to applications.
The only caveat is that an application can specify the colors it wants to get back.

By limiting the colors available to the application, you limit the amount of cache available.
You can also split the cache in disjoint sets of colors and give some data structures their own partition to avoid cache pollution by bad access patterns.

Using CControl
---------------

First, as root, load the kernel module:

	ccontrol load --max_mem 1G

This will reserve 1 GB of RAM for ccontrol and initialize page coloring for the last level cache (LLC). You can look at `dmesg` for additional info.
You can also use options --colors to manually set the number of colors to either an integer value, or to "Ln" for using autoconfiguration with the n-th level cache.

If your application use the ccontrol library (linked with libccontrol), it can now access the module and create areas.

Once you're done with ccontrol, unload the module:

	ccontrol unload

If you want additional info on the cache characteristics detected by ccontrol, you can use:

	ccontrol info

Library
-------

The `libccontrol` provides a easy interface to the cache control facilities.
It works as a custom memory allocation library: you create an _area_ by asking the kernel module for a set of colored pages.

	ccontrol.h

	/* allocates an area */
	struct ccontrol_area * ccontrol_create (void);

	/* configure an area and make its memory available */
	int ccontrol_configure (struct ccontrol_area * area, struct cc_layout * layout);

	/* accessing the memory */
	char * buf = area->start;
	size_t size_in_bytes = area->size;

	/* destroy an area */
	void ccontrol_destroy (struct ccontrol_area * area);

Layout is a block cyclic layout:

	ccontrol_types.h
	
	struct cc_layout {
		int *color_list; // list of color to repeat
		int nb_colors; // size of color list
		int color_repeat; // size of each color block
		int list_repeat; // number of list repetition
	};

After creation, an area contains some useful information (from the module) to help create the layout:

	struct cc_module_info * info = &area->module_info;
	info->nb_colors; // number of colors in the module instance
	info->block_size; // size of each color block in bytes (usally a page)
	info->color_list_size_max; // maximum size of color list (can be changed in module parameters)

Installing
---------

The classical `./autogen.sh; ./configure; make; make install;` should work, except if you have outdated autotools.
With a distribution (pre-generated ./configure), there should be no problem even with autotools.
This project has no dependencies except for the Linux kernel headers necessary to compile the kernel module and the autotools (autoconf,automake,libtool). 

Notes:
* `./configure --prefix` can be used to relocate files (default = /usr/local), however it will not affect the kernel module and udev rules file positions that are automatically detected.
udev rules path can be overriden using variables given to configure; see `./configure -h`.

* `make install` can use the DESTDIR variable as an install prefix for every file. If defined, INSTALL\_MOD\_PATH is a specific DESTDIR override for the kernel module (e.g. on Archlinux it is /usr).

* A newly installed kernel module may require running `depmod` before use.

* `make uninstall` will not remove the kernel module due to build system limitation.

* `make dist` generates a working distribution; however `make distcheck` fails due to autodetected paths having priority on the prefix used in tests.

Bugs and Limitations
--------------------

This tool assume that physical lines of memory are distributed by round robin across all associate-sets.
Given the cache size C, the associativity A and the page size P, ccontrol computes the number of colors as C/AP.
Then, the module consider that page 0 is of color 0, page 1 of colors 1 and so on (a simple modulo gives us the color of each page).

Some architectures do not fit that description.
Lines are not distributed in round robin, or the number of colors is more complicated than that.
If you use ccontrol on such architecture, the real colors of pages given by ccontrol to a process will be wrong.
Unfortunately, Intel Sandy Bridge and newer cores are among such architectures, and their cache layout is undocumented.
 
The number of pages of a given color is determined by the amount of RAM you give to the module.
Since ccontrol does not support swapping, this number of pages also determines the maximal size of an allocation in a colored zone.
Be careful not asking too much memory with too few pages available.
 
FAQ
---

* modprobe error: Module ccontrol not found.

	* The two most common reasons are a missing depmod after the first make install, and a system erasing the install path after reboot.
	While the Makefile coming from the kernel should do a module dependency update after install, sometimes it is not taken into account.
	Launching `depmod` as root after `make install` can solve this issue.

	* If your system is configured so that the standard module install path is recreated after each reboot, the module will not survive a system shutdown (this issue is present on recent Ubuntu for exemple).
	Until ccontrol includes a dkms configuration and install rules, you must reinstall the module each time. Sorry...

References
----------

This tool was the subject of a publication in the International Conference on Supercomputing (ICS) in 2011.
You can find the paper [here](http://moais.imag.fr/membres/swann.perarnau/pdfs/cacheics11.pdf).
This paper describe several possible uses of ccontrol to measure and optimize the cache performance of HPC applications.

If you want to cite this paper:

	Swann Perarnau, Marc Tchiboukdjian, and Guillaume Huard.
	Controlling cache utilization of hpc applications.
	In International Conference on Supercomputing (ICS), 2011.

You can also use this Bibtex:

	@inproceedings{ics11,
		title = {Controlling Cache Utilization of HPC Applications},
		author = {Swann Perarnau and Marc Tchiboukdjian and Guillaume Huard},
		booktitle = {International Conference on Supercomputing (ICS)},
		year = {2011}
	}
