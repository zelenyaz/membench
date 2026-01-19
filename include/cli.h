#ifndef CLI_H
#define CLI_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    MODE_SINGLE,
    MODE_SEQ,
    MODE_CONCURRENT
} run_mode_t;

typedef enum {
    STOP_TIME,
    STOP_ITERS
} stop_mode_t;

#define MAX_BENCHES 16
#define MAX_BENCH_NAME 32

typedef struct {
    run_mode_t mode;
    stop_mode_t stop_mode;
    
    char bench_name[MAX_BENCH_NAME];           // for single mode
    char bench_list[MAX_BENCHES][MAX_BENCH_NAME]; // for seq/concurrent
    int bench_count;
    
    size_t buffer_size;     // total buffer per benchmark
    int threads;            // threads per benchmark
    
    double seconds;         // time-based stop
    uint64_t iters;         // iteration-based stop
    
    int reuse;              // reuse mode enabled
    size_t region_bytes;    // region size for reuse mode
    uint64_t reuse_iter;    // iterations per region
    
    uint64_t seed;          // PRNG seed
    int pin;                // CPU pinning
    double report_interval; // reporting interval in seconds
} cli_args_t;

// Parse command-line arguments
int cli_parse(int argc, char **argv, cli_args_t *args);

// Print usage
void cli_usage(const char *prog);

// Initialize defaults
void cli_init_defaults(cli_args_t *args);

#endif // CLI_H
