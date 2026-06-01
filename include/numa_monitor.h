#ifndef NUMA_MONITOR_H
#define NUMA_MONITOR_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <pthread.h>

#define NUMA_MON_MAX_BUFFERS 16

typedef struct {
	void	   *buffer;
	size_t		buffer_size;
	const char *bench_name;
} numa_mon_buffer_t;

typedef struct {
	numa_mon_buffer_t buffers[NUMA_MON_MAX_BUFFERS];
	int				  buffer_count;
	FILE			 *csv_file;
	atomic_int		 *stop_flag;
	pthread_t		  thread;
} numa_monitor_ctx_t;

// Start NUMA monitor thread.
// If write_header is non-zero, writes CSV header (use 1 for first open, 0 for
// append). Opens the file in "w" mode if write_header, "a" otherwise.
int numa_monitor_start(numa_monitor_ctx_t *ctx, const char *csv_path,
					   atomic_int *stop_flag, int write_header);

// Add a buffer to monitor. Call before start.
int numa_monitor_add_buffer(numa_monitor_ctx_t *ctx, void *buffer,
							size_t buffer_size, const char *bench_name);

// Stop and join the monitor thread. Closes the CSV file.
void numa_monitor_stop(numa_monitor_ctx_t *ctx);

#endif // NUMA_MONITOR_H
