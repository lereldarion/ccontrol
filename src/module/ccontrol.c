/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Kernel Module to reserve physical address ranges for future mmap.
 *
 * Copyright (C) 2010 Swann Perarnau <swann.perarnau@imag.fr>
 * Copyright (C) 2015 Francois Gindraud <francois.gindraud@inria.fr>
 */

// module
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
// misc
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
// memory management
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <linux/highmem.h>
// devices
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include "ccontrol_ioctl.h"

/* ------------ Module params ---------------- */

MODULE_AUTHOR("Swann Perarnau <swann.perarnau@imag.fr>");
MODULE_AUTHOR("Francois Gindraud <francois.gindraud@inria.fr>");
MODULE_DESCRIPTION("Provides physical page coloring to userspace applications.");
MODULE_LICENSE("GPL");

static char *max_mem = "1k";
module_param(max_mem, charp, 0);
MODULE_PARM_DESC(max_mem, "maximum amount of memory the module can use");
static int nb_colors = 1;
module_param(nb_colors, int, 0);
MODULE_PARM_DESC(nb_colors, "number of colors");
static int color_list_size_max = 0;
module_param(color_list_size_max, int, 0);
MODULE_PARM_DESC(color_list_size_max, "maximum number of colors in config list");

/* -------------- Types --------------------- */

struct ccontrol_device {
	dev_t id; // device number
	struct cdev cdev; // chararcter device struct
	struct class *class; // sysfs class of sysfs device
};

struct page_storage {
	struct page **pages;
	size_t nb_pages;
};

struct ccontrol_memory {
	struct mutex mutex; // protects the module memory storage
	
	/* Kernel allocator for big buffers allocate slabs of 2^n pages (n is called order).
	 * We chose a fixed block order that will be used to allocate pages from the kernel.
	 * Its order is chosen so that is contains 1 or 2 pages of each color.
	 *
	 * Up to nb_allocated_blocks can be allocated,
	 * using alloc_pages(HIGHMEM) to get contiguous phy memory.
	 */
	int block_order;
	size_t nb_allocated_blocks;
	size_t max_allocated_blocks;
	struct page **allocated_blocks; // kvmalloc'ed

	/* Array to store struct page pointers to cached pages of a given color.
	 * This doesn't own the pages.
	 *
	 * pages_by_color and all pages_by_color[c]->pages buffers are kvmalloc'ed
	 * as one big buffer (can be quite big).
	 */
	struct page_storage *pages_by_color;
};

struct memory_area {
	struct rw_semaphore sem; // protect the memory area

	/* Contains metadata for a memory area.
	 * It is attached to a file descriptor structure, as each open of ccontrol device will create a new area.
	 *
	 * It is initially non configured, and cannot be mmaped.
	 * An ioctl config must be performed to configure its layout and allow mmap.
	 */
	struct cc_layout config; // contains color_list:kmalloc'ed
	struct page_storage store; // nb_pages==0 <=> pages==NULL ; contains pages:vmalloc'ed
	int is_configured;
	int vma_count;
};

static struct ccontrol_device cc_dev;
static struct ccontrol_memory cc_mem;

/* ---------------- Utils --------------------- */

// Set *suffix and returns trimed size to give a human readable memory size
static size_t pretty_size(char *suffix, size_t size)
{
	const size_t scale = 1 << 10;
	const char suffixes[] = "BkMGTP";
	int i;
	for (i = 0; suffixes[i + 1] != '\0' && size >= 8 * scale; ++i)
		size = DIV_ROUND_UP(size, scale);
	*suffix = suffixes[i];
	return size;
}

/* Select between vmalloc and kmalloc (use kvfree from kernel to free).
 * It allows module buffers (that can greatly vary in size) to use efficient
 * memory if they are actually small.
 *
 * The threshold is totally arbitrary (a few pages...)
 */
static void* cc_kvmalloc(size_t size)
{
	if (size > 16 * PAGE_SIZE)
		return vmalloc(size);
	else
		return kmalloc(size, GFP_KERNEL);
}

/* -------------- Memory --------------------- */

/* We assume that the cache use a simple modulo mapping from physical addresses to cache lines.
 * Thus the color of a physical page (pfn : phy page number) is a simple modulo.
 */
static int pfn_to_color(unsigned long pfn)
{
	return pfn % nb_colors;
}

static int cc_memory_init(size_t max_memory)
{
	int err = 0;
	int c;
	struct page **array_base;

	size_t sz_block;
	size_t sz_allocated_blocks;
	size_t sz_pages_by_color_array;
	size_t sz_colored_page_storage_by_array;
	size_t sz_colored_page_storage_total;

	// mutex for concurrent access
	mutex_init(&cc_mem.mutex);

	// block subsystem init
	cc_mem.block_order = get_order(nb_colors * PAGE_SIZE);
	sz_block = PAGE_SIZE << cc_mem.block_order;
	cc_mem.nb_allocated_blocks = 0;
	cc_mem.max_allocated_blocks = DIV_ROUND_UP(max_memory, sz_block);
	sz_allocated_blocks = cc_mem.max_allocated_blocks * sizeof(struct page *);
	cc_mem.allocated_blocks = cc_kvmalloc(sz_allocated_blocks);
	if (cc_mem.allocated_blocks == NULL) {
		err = -ENOMEM;
		goto err_block_list_alloc;
	}

	// init pages_by_color storage (uses one big vmalloc buffer cut into pieces)
	sz_pages_by_color_array = nb_colors * sizeof(struct page_storage);
	sz_colored_page_storage_by_array = 2 * cc_mem.max_allocated_blocks * sizeof(struct page *);
	sz_colored_page_storage_total = sz_pages_by_color_array + nb_colors * sz_colored_page_storage_by_array;

	cc_mem.pages_by_color = cc_kvmalloc(sz_colored_page_storage_total);
	if (cc_mem.pages_by_color == NULL) {
		err = -ENOMEM;
		goto err_pages_by_color_alloc;
	}
	array_base = (struct page **) &cc_mem.pages_by_color[nb_colors];
	for (c = 0; c < nb_colors; ++c) {
		struct page_storage *store = &cc_mem.pages_by_color[c];
		store->nb_pages = 0;
		store->pages = &array_base[c];
	}

	// print some structure size info
	{
		char sx;
		size_t sz;
		sz = pretty_size(&sx, sz_block);
		printk(KERN_DEBUG "ccontrol: memory: block={page_order=%d, size=%zu%c}\n", cc_mem.block_order, sz, sx);
		sz = pretty_size(&sx, sz_allocated_blocks);
		printk(KERN_DEBUG "ccontrol: memory: allocated_block_storage=%zu%c\n", sz, sx);
		sz = pretty_size(&sx, sz_colored_page_storage_total);
		printk(KERN_DEBUG "ccontrol: memory: colored_page_storage=%zu%c\n", sz, sx);
	}
	return 0;

err_pages_by_color_alloc:
	kvfree(cc_mem.allocated_blocks);
err_block_list_alloc:
	return err;
}

static void cc_memory_destroy(void)
{
	size_t i;
	{
		char sx;
		size_t sz;
		sz = pretty_size(&sx, cc_mem.nb_allocated_blocks * (PAGE_SIZE << cc_mem.block_order));
		printk(KERN_DEBUG "ccontrol: memory: used %zu blocks (total size=%zu%c)\n",
				cc_mem.nb_allocated_blocks, sz, sx);
	}

	// just delete colored page storage ; pages will be freed from the block list
	kvfree(cc_mem.pages_by_color);
	for (i = 0; i < cc_mem.nb_allocated_blocks; ++i)
		// free user pages from the block list (alloced with alloc_pages)
		__free_pages(cc_mem.allocated_blocks[i], cc_mem.block_order);
	kvfree(cc_mem.allocated_blocks);
}

/* push: put page into store
 * pop: get page from store (potentially allocate new ones if empty)
 * refill_storage: get  new page block from system and put it into store
 *
 * locks: needs cc_mem
 */
static void cc_memory_push_page(struct page *p);
static int cc_memory_pop_page(struct page **p, int color);
static int cc_memory_refill_storage(void);

static void cc_memory_push_page(struct page *p)
{
	struct page_storage *store = &cc_mem.pages_by_color[pfn_to_color(page_to_pfn(p))];
	store->pages[store->nb_pages++] = p;
}

static int cc_memory_pop_page(struct page **p, int color)
{
	struct page_storage *store = &cc_mem.pages_by_color[color];
	if (store->nb_pages == 0) {
		int err = cc_memory_refill_storage();
		if (err < 0)
			return err;
	}
	*p = store->pages[--store->nb_pages];
	return 0;
}

static int cc_memory_refill_storage(void)
{
	struct page *page;
	int i;
	if (cc_mem.nb_allocated_blocks == cc_mem.max_allocated_blocks) {
		printk(KERN_WARNING "ccontrol: memory: reached max_mem limit\n");
		return -ENOMEM;
	}

	/* GPF_HIGHUSER is for userspace memory, in a big space.
	 *	__GFP_COMP is to let the kernel consider this page block "as a unit".
	 *	More specifically, it is required to use this flag if using vm_insert_pages.
	 * See https://lkml.org/lkml/2006/3/16/170
	 */
	page = alloc_pages(GFP_HIGHUSER | __GFP_COMP, cc_mem.block_order);
	if (page == NULL)
		return -ENOMEM;

	// Store and split page block by color
	cc_mem.allocated_blocks[cc_mem.nb_allocated_blocks++] = page;
	for (i = 0; i < 1 << cc_mem.block_order; i++)
		cc_memory_push_page(nth_page(page, i));
	return 0;
}

/* -------------- Memory area ------------------- */

// locks: nothing
static int cc_memory_new_area(struct memory_area **area)
{
	struct memory_area *a;
	a = kmalloc(sizeof(struct memory_area), GFP_KERNEL);
	if (a == NULL) {
		return -ENOMEM;
	} else {
		init_rwsem(&a->sem);
		a->is_configured = 0;
		a->vma_count = 0;

		// What must be set in case of premature area destruction
		a->config.color_list = NULL;
		a->store.pages = NULL;
		a->store.nb_pages = 0;

		*area = a;
		return 0;
	}
}

// locks: uses cc_mem
static int cc_memory_destroy_area(struct memory_area *area)
{
	/* Do not protect area access with area lock because its memory will disappear.
	 * Assume the kernel will call release after every reference to file descriptor
	 * (and area) has been destroyed.
	 */

	// put colored pages back in storage (uses cc_mem lock)
	int i;
	if (mutex_lock_interruptible(&cc_mem.mutex))
		return -ERESTARTSYS;
	for (i = 0; i < area->store.nb_pages; ++i)
		cc_memory_push_page(area->store.pages[i]);
	mutex_unlock(&cc_mem.mutex);

	kfree(area->config.color_list);
	kvfree(area->store.pages);
	kfree(area);
	return 0;
}

static int cc_memory_config_area(struct memory_area *area, struct cc_layout *config)
{
	// TODO support reconfigure
	int err = 0;
	int i, b, c;
	size_t nb_pages = config->nb_colors * config->color_repeat * config->list_repeat;
	struct page_storage *store = &area->store;

	down_write(&area->sem);

	if (area->is_configured) {
		printk(KERN_WARNING "ccontrol: area: reconfigure is unsupported\n");
		err = -EPERM;
		goto err_already_configured;
	}

	store->nb_pages = 0;
	store->pages = cc_kvmalloc(nb_pages * sizeof(struct page *));
	if (store->pages == NULL) {
		err = -ENOMEM;
		goto err_kvmalloc_failed;
	}

	// obtain pages (block cyclic layout)
	mutex_lock(&cc_mem.mutex);
	for (i = 0; i < config->list_repeat; i++)
		for (c = 0; c < config->nb_colors; c++)
			for (b = 0; b < config->color_repeat; b++) {
				err = cc_memory_pop_page(&store->pages[store->nb_pages], config->color_list[c]);
				if (err)
					goto err_obtain_pages;
				store->nb_pages++;
			}
	mutex_unlock(&cc_mem.mutex);

	area->config = *config; // get ownership of color_list kmalloc'ed buffer
	area->is_configured = 1;
	up_write(&area->sem);

	{
		char sx;
		size_t sz;
		printk(KERN_DEBUG "ccontrol: area: configured with {nb_color=%d, color_repeat=%d, list_repeat=%d}, nb_pages=%zu\n",
				config->nb_colors, config->color_repeat, config->list_repeat, store->nb_pages);
		sz = pretty_size(&sx, nb_pages * sizeof(struct page *));
		printk(KERN_DEBUG "ccontrol: area: colored_pages_storage=%zu%c\n", sz, sx);
	}
	return 0;

err_obtain_pages:
	while (store->nb_pages > 0)
		cc_memory_push_page(store->pages[--store->nb_pages]);
	mutex_unlock(&cc_mem.mutex);
	kvfree(store->pages);
	store->pages = NULL;
err_kvmalloc_failed:
err_already_configured:
	up_write(&area->sem);
	return err;
}

/*static void cc_memory_move(void)
  {
  struct page *a = NULL;
  struct page *b = NULL;
  void *va, *vb;
  va = kmap_atomic(a);
  vb = kmap_atomic(b);
  memcpy(va, vb, PAGE_SIZE);
  kunmap_atomic(vb);
  kunmap_atomic(va);
  }*/

/* --------- Module device operations ------- */

// locks: nothing
static int cc_device_open(struct inode *inode, struct file *filp)
{
	if (inode->i_cdev != &cc_dev.cdev) {
		printk(KERN_ERR "ccontrol: device open: inode cdev is not device cdev\n");
		return -EPERM;
	}
	return cc_memory_new_area((struct memory_area **) &filp->private_data);
}

// locks: nothing
static int cc_device_release(struct inode *inode, struct file *filp)
{
	return cc_memory_destroy_area(filp->private_data);
}

// locks: nothing (only reads module data)
static void cc_ioctl_info (struct cc_module_info *info)
{
	info->nb_colors = nb_colors;
	info->block_size = PAGE_SIZE;
	info->color_list_size_max = color_list_size_max;
}

static int cc_ioctl_config (struct cc_layout *config, struct file *filp)
{
	return cc_memory_config_area(filp->private_data, config);
}

// locks: nothing (deferred to sub ioctl functions)
static long cc_device_ioctl(struct file *filp, unsigned int code, unsigned long val)
{
	void __user *arg = (void __user *) val;
	struct cc_module_info local_info;
	struct cc_layout local_config;
	int err = 0;

	if (_IOC_TYPE(code) != CCONTROL_IO_MAGIC) {
		printk(KERN_WARNING "ccontrol: received wrong ioctl type\n");
		return -ENOTTY;
	}

	switch (code) {
		case CCONTROL_IO_INFO:
			cc_ioctl_info (&local_info);
			err = copy_to_user(arg, &local_info, sizeof(struct cc_module_info));
			break;
		case CCONTROL_IO_CONFIG:
			{
				int *config_color_list;
				size_t bytes;

				// get config
				err = copy_from_user(&local_config, arg, sizeof(struct cc_layout));
				if (err)
					break;

				// check config arguments
				if (local_config.nb_colors < 1 || local_config.color_repeat < 1 || local_config.list_repeat < 1) {
					printk(KERN_WARNING "ccontrol: area: bad config {nb_color=%d, color_repeat=%d, list_repeat=%d}\n",
							local_config.nb_colors, local_config.color_repeat, local_config.list_repeat);
					err = -EINVAL;
					break;
				}	
				if (local_config.nb_colors > color_list_size_max) {
					printk(KERN_WARNING "ccontrol: color list exceeds max size (%d > %d)\n",
							local_config.nb_colors, color_list_size_max);
					err = -EPERM;
					break;
				}

				// get color list (check size before kmalloc)
				bytes = local_config.nb_colors * sizeof(int);
				config_color_list = kmalloc(bytes, GFP_KERNEL);
				if (config_color_list == NULL) {
					err = -ENOMEM;
					break;
				}
				err = copy_from_user(config_color_list, (int __user *) local_config.color_list, bytes);
				if (err)
					goto err_after_kmalloc;
				local_config.color_list = config_color_list;

				// ioctl (get ownership of kmalloced memory only on success)
				err = cc_ioctl_config(&local_config, filp);
				if (err)
					goto err_after_kmalloc;
				break;

err_after_kmalloc:
				kfree(config_color_list);
				break;
			}
		default:
			printk(KERN_WARNING "ccontrol: invalid ioctl opcode: %u\n", code);
			err = -ENOTTY;
	}
	return err;
}

/* ------------ Mmap operation ------------- */

// locks: uses area_read
static int cc_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int err = 0;
	struct memory_area *area = vma->vm_private_data;
	// get the page offset in device
	size_t index = vma->vm_pgoff + vmf->pgoff;

	down_read(&area->sem);

	if (index < area->store.nb_pages) {
		vmf->page = area->store.pages[index];
		get_page(vmf->page); // increase page ref count
	} else {
		printk(KERN_WARNING "ccontrol: page fault outside of area (%zu > %zu)\n",
				index, area->store.nb_pages);
		err = VM_FAULT_ERROR;
	}

	up_read(&area->sem);
	return err;
}

// locks: uses area_write
static void cc_vma_open(struct vm_area_struct *vma)
{
	struct memory_area *area = vma->vm_private_data;
	down_write(&area->sem);
	area->vma_count++;
	up_write(&area->sem);
}

// locks: uses area_write
static void cc_vma_close(struct vm_area_struct *vma)
{
	struct memory_area *area = vma->vm_private_data;
	down_write(&area->sem);
	area->vma_count--;
	if (area->vma_count == 0)
		printk(KERN_DEBUG "ccontrol: area: vma destroyed\n");
	up_write(&area->sem);
}

static struct vm_operations_struct cc_vm_ops = {
	.fault = cc_vma_fault,
	.open = cc_vma_open,
	.close = cc_vma_close
};

// locks: uses area_write
static int cc_device_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int err = 0;
	struct memory_area * area = filp->private_data;
	size_t size = vma_pages(vma);

	down_write(&area->sem);

	if (!area->is_configured) {
		// Area must be configured before mapping
		printk(KERN_WARNING "ccontrol: area: cannot map if not configured\n");
		err = -ENODEV;
		goto err_bad_arg;
	}
	if (! (vma->vm_pgoff + size < area->store.nb_pages)) {
		// Reject out of bounds mmaps
		printk(KERN_WARNING "ccontrol: area: mmap [%zu, %zu[ out of bounds [0, %zu[\n",
				vma->vm_pgoff, vma->vm_pgoff + size, area->store.nb_pages);
		err = -EINVAL;
		goto err_bad_arg;
	}
	if(!(vma->vm_flags & VM_SHARED)) {
		// Only shared mapping are supported right now...
		printk(KERN_WARNING "ccontrol: area: only shared mappings are supported\n");
		err = -EPERM;
		goto err_bad_arg;
	}

	vma->vm_ops = &cc_vm_ops;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
	vma->vm_flags |= VM_RESERVED | VM_CAN_NONLINEAR; // flags from old implementation
#else
	vma->vm_flags |= VM_IO; // prevents mlock, merge, swap, .... (may break things)
#endif
	vma->vm_flags |= VM_DONTEXPAND; // prevent mremap (fixed size phy area)
	vma->vm_private_data = area;

	area->vma_count++;

err_bad_arg:
	up_write(&area->sem);
	return err;
}

// use vm_insert_page
// zap_page_range
// asm/pgtable.h : pfn_pte

/* --------- Module device management ------- */

static struct file_operations cc_device_fops = {
	.owner = THIS_MODULE,
	.open = cc_device_open,
	.release = cc_device_release, // close()
	.unlocked_ioctl = cc_device_ioctl,
	.mmap = cc_device_mmap,
};

// locks: nothing
static int cc_device_create(void)
{
	int err;
	struct device * d;

	// alloc one device id
	err = alloc_chrdev_region(&cc_dev.id, 0, 1, "ccontrol");
	if (err < 0) {
		printk(KERN_ERR "ccontrol: device: cannot alloc id\n");
		goto err_alloc_id;
	}

	// create device
	cdev_init(&cc_dev.cdev, &cc_device_fops);
	cc_dev.cdev.owner = THIS_MODULE;
	err = cdev_add(&cc_dev.cdev, cc_dev.id, 1);
	if (err < 0) {
		printk(KERN_ERR "ccontrol: device: cannot create\n");
		goto err_create_dev;
	}

	// add device to sysfs
	cc_dev.class = class_create(THIS_MODULE, "ccontrol");
	if (IS_ERR(cc_dev.class)) {
		err = PTR_ERR(cc_dev.class);
		printk(KERN_ERR "ccontrol: device: cannot allocate sysfs class\n");
		goto err_create_class;
	}
	d = device_create(cc_dev.class, NULL, cc_dev.id, NULL, "ccontrol");
	if (IS_ERR(d)) {
		err = PTR_ERR(d);
		printk(KERN_ERR "ccontrol: device: cannot create sysfs device\n");
		goto err_create_sys_dev;
	}

	printk(KERN_DEBUG "ccontrol: device: created with number %d:%d\n", MAJOR(cc_dev.id), MINOR(cc_dev.id));
	return 0;	

err_create_sys_dev:
	class_destroy(cc_dev.class);
err_create_class:
	cc_dev.class = NULL;
	cdev_del(&cc_dev.cdev);
err_create_dev:
	unregister_chrdev_region(cc_dev.id, 1);
err_alloc_id:
	return err;
}

// locks: nothing
static void cc_device_destroy(void)
{
	device_destroy(cc_dev.class, cc_dev.id);
	class_destroy(cc_dev.class);
	cc_dev.class = NULL;
	cdev_del(&cc_dev.cdev);
	unregister_chrdev_region(cc_dev.id, 1);
}

/* -------------- Entry points --------------- */

static int __init ccontrol_init(void)
{
	int err;
	size_t max_memory;

	// module arguments
	if (nb_colors <= 0) {
		printk(KERN_ERR "ccontrol: non-positive color number (%d)\n", nb_colors);
		return -EINVAL;
	}
	max_memory = (size_t) memparse(max_mem, NULL);
	if (max_memory == 0) {
		printk(KERN_ERR "ccontrol: invalid max memory argument: \"%s\"\n", max_mem);
		return -EINVAL;
	}
	if (color_list_size_max <= 0) {
		// if color_list_size_max is undefined, default to the number of colors
		color_list_size_max = nb_colors;
	}

	printk(KERN_DEBUG "ccontrol: init max_mem=%s nb_colors=%d\n", max_mem, nb_colors);

	err = cc_device_create();
	if (err)
		goto err_device_create;
	err = cc_memory_init(max_memory);
	if (err)
		goto err_mem_init;
	return 0;

err_mem_init:
	cc_device_destroy();
err_device_create:
	return err;
}

static void __exit ccontrol_exit(void)
{
	cc_memory_destroy();
	cc_device_destroy();
	printk(KERN_DEBUG "ccontrol: exit\n");
}

module_init(ccontrol_init);
module_exit(ccontrol_exit);
