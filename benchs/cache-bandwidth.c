#include <stdio.h>
#include <error.h>
#include <time.h>
#include <ccontrol.h>

#define ACCESS_TYPE int
#define ACCESS_SIZE (sizeof (ACCESS_TYPE))

volatile ACCESS_TYPE dummy = 0;

void bench_internal (void * mem, size_t size, size_t incr, size_t nb_repeat) {
	ACCESS_TYPE t = 0;
	struct timespec start, end;
	const long nsec_per_sec = 1000 * 1000 * 1000;

	clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &start);
	for (size_t iter = 0; iter < nb_repeat; ++iter) {
		char * it = mem;
		char * bound = &it[size];
		while (it < bound) {
			t += * (ACCESS_TYPE *) it;
			it += incr;
		}
	}
	clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &end);

	size_t nb_accesses = nb_repeat * (size / incr);
	struct timespec diff = {
		.tv_sec = end.tv_sec - start.tv_sec,
		.tv_nsec = end.tv_nsec - start.tv_nsec
	};
	if (diff.tv_nsec < 0) {
		diff.tv_nsec += nsec_per_sec;
		diff.tv_sec -= 1;
	}

	double time_in_sec = (double) diff.tv_sec + (double) diff.tv_nsec / (double) nsec_per_sec;
	double bytes_per_second = (double) (nb_accesses * ACCESS_SIZE) / time_in_sec;

	printf ("bandwidth = %lfB/s, %lfMB/s\n", bytes_per_second, bytes_per_second / (1 << 10 << 10));

	dummy += t;
}

int nb_page_base = 64;
int nb_it = 500;
int hit_per_page = 1;
struct cc_module_info mi;

void test (const char * blah, void * buf, size_t size) {
	printf ("%s ", blah);
	bench_internal (buf, size, 64 /*mi.block_size / hit_per_page*/, nb_it);
}

int main (void) {

	struct ccontrol_area * unused = ccontrol_create ();
	mi = unused->module_info;
	ccontrol_destroy (unused);

	{
		struct ccontrol_area * one_color_area = ccontrol_create ();
		int c = 0;
		struct cc_layout one_l = {
			.color_list = &c,
			.nb_colors = 1,
			.color_repeat = 1,
			.list_repeat = nb_page_base * mi.nb_colors
		};
		ccontrol_configure (one_color_area, &one_l);
		test ("prechauffage", one_color_area->start, one_color_area->size);
		ccontrol_destroy (one_color_area);
	}
	{
		size_t s = mi.block_size * nb_page_base * mi.nb_colors;
		void * a = malloc (s);
		test ("malloc", a, s);
		free (a);
	}
	{
		struct ccontrol_area * one_color_area = ccontrol_create ();
		mi = one_color_area->module_info;
		int c = 0;
		struct cc_layout one_l = {
			.color_list = &c,
			.nb_colors = 1,
			.color_repeat = 1,
			.list_repeat = nb_page_base * mi.nb_colors
		};
		ccontrol_configure (one_color_area, &one_l);
		test ("one-color", one_color_area->start, one_color_area->size);
		ccontrol_destroy (one_color_area);
	}
	{
		struct ccontrol_area * all_color_area = ccontrol_create ();
		struct cc_layout all_l = {
			.color_list = malloc (mi.nb_colors * sizeof (int)),
			.nb_colors = mi.nb_colors,
			.color_repeat = 1,
			.list_repeat = nb_page_base
		};
		for (int i = 0; i < mi.nb_colors; ++i)
			all_l.color_list[i] = i;
		ccontrol_configure (all_color_area, &all_l);
		test ("all-color", all_color_area->start, all_color_area->size);
		ccontrol_destroy (all_color_area);
	}
	return 0;
}

