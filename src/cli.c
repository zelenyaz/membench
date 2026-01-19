#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include "cli.h"

void cli_init_defaults(cli_args_t *args)
{
	memset(args, 0, sizeof(*args));
	args->mode			  = MODE_SINGLE;
	args->stop_mode		  = STOP_TIME;
	args->buffer_size	  = 64 * 1024 * 1024; // 64 MB default
	args->threads		  = 4;
	args->seconds		  = 5.0;
	args->iters			  = 0;
	args->reuse			  = 0;
	args->region_bytes	  = 2 * 1024 * 1024; // 2 MB default
	args->reuse_iter	  = 50000;
	args->seed			  = 0x12345678DEADBEEFULL;
	args->pin			  = 0;
	args->report_interval = 1.0;
	args->bench_count	  = 0;
}

static size_t parse_size(const char *str)
{
	char  *end;
	double val = strtod(str, &end);

	switch (toupper((unsigned char)*end)) {
	case 'K':
		val *= 1024;
		break;
	case 'M':
		val *= 1024 * 1024;
		break;
	case 'G':
		val *= 1024 * 1024 * 1024;
		break;
	}

	return (size_t)val;
}

static int parse_bench_list(const char *str, cli_args_t *args)
{
	char buf[512];
	strncpy(buf, str, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *token		  = strtok(buf, ",");
	args->bench_count = 0;

	while (token && args->bench_count < MAX_BENCHES) {
		while (*token == ' ')
			token++;
		strncpy(args->bench_list[args->bench_count], token, MAX_BENCH_NAME - 1);
		args->bench_list[args->bench_count][MAX_BENCH_NAME - 1] = '\0';
		args->bench_count++;
		token = strtok(NULL, ",");
	}

	return args->bench_count > 0 ? 0 : -1;
}

void cli_usage(const char *prog)
{
	fprintf(
		stderr,
		"Usage: %s [OPTIONS]\n\n"
		"Execution Modes:\n"
		"  --mode <single|seq|concurrent>   Run mode (default: single)\n"
		"  --bench <name>                   Benchmark for single mode\n"
		"  --benches <list>                 Comma-separated benchmarks for seq/concurrent\n\n"
		"Stop Conditions:\n"
		"  --seconds <T>                    Run for T seconds (default: 5)\n"
		"  --iters <N>                      Run for N operations\n\n"
		"Buffer Settings:\n"
		"  --size <bytes>                   Buffer size per benchmark (default: 64M)\n"
		"  --threads <N>                    Threads per benchmark (default: 4)\n\n"
		"Reuse Mode:\n"
		"  --reuse <0|1>                    Enable reuse mode (default: 0)\n"
		"  --region-bytes <bytes>           Region size for reuse (default: 2M)\n"
		"  --reuse-iter <N>                 Iterations per region (default: 50000)\n\n"
		"Other:\n"
		"  --seed <N>                       PRNG seed\n"
		"  --pin <0|1>                      CPU pinning (default: 0)\n"
		"  --report-interval <sec>          Reporting interval (default: 1.0)\n"
		"  --help                           Show this help\n\n"
		"Available benchmarks:\n"
		"  seq_read, seq_write, seq_rw\n"
		"  rand_read, rand_write, rand_rw\n"
		"  ptr_chase\n\n"
		"Examples:\n"
		"  %s --mode single --bench seq_read --size 64M --threads 4 --seconds 5\n"
		"  %s --mode seq --benches seq_read,seq_write,rand_read --seconds 3\n"
		"  %s --mode concurrent --benches seq_read,rand_rw --threads 2 --seconds 5\n",
		prog, prog, prog, prog);
}

int cli_parse(int argc, char **argv, cli_args_t *args)
{
	static struct option long_options[] = {
		{ "mode",			  required_argument, 0, 'm' },
		{ "bench",		   required_argument, 0, 'b' },
		{ "benches",		 required_argument, 0, 'B' },
		{ "size",			  required_argument, 0, 's' },
		{ "threads",		 required_argument, 0, 't' },
		{ "seconds",		 required_argument, 0, 'T' },
		{ "iters",		   required_argument, 0, 'i' },
		{ "reuse",		   required_argument, 0, 'r' },
		{ "region-bytes",	  required_argument, 0, 'R' },
		{ "reuse-iter",		required_argument, 0, 'I' },
		{ "seed",			  required_argument, 0, 'S' },
		{ "pin",			 required_argument, 0, 'p' },
		{ "report-interval", required_argument, 0, 'P' },
		{ "help",			  no_argument,	   0, 'h' },
		{ 0,				 0,				 0, 0	 }
	};

	cli_init_defaults(args);
	int has_seconds = 0, has_iters = 0;

	int opt;
	int option_index = 0;
	while ((opt = getopt_long(argc, argv, "m:b:B:s:t:T:i:r:R:I:S:p:P:h",
							  long_options, &option_index)) != -1) {
		switch (opt) {
		case 'm':
			if (strcmp(optarg, "single") == 0) {
				args->mode = MODE_SINGLE;
			} else if (strcmp(optarg, "seq") == 0) {
				args->mode = MODE_SEQ;
			} else if (strcmp(optarg, "concurrent") == 0) {
				args->mode = MODE_CONCURRENT;
			} else {
				fprintf(stderr, "Unknown mode: %s\n", optarg);
				return -1;
			}
			break;
		case 'b':
			strncpy(args->bench_name, optarg, MAX_BENCH_NAME - 1);
			args->bench_name[MAX_BENCH_NAME - 1] = '\0';
			break;
		case 'B':
			if (parse_bench_list(optarg, args) < 0) {
				fprintf(stderr, "Failed to parse benchmark list: %s\n", optarg);
				return -1;
			}
			break;
		case 's':
			args->buffer_size = parse_size(optarg);
			break;
		case 't':
			args->threads = atoi(optarg);
			if (args->threads < 1)
				args->threads = 1;
			break;
		case 'T':
			args->seconds = atof(optarg);
			has_seconds	  = 1;
			break;
		case 'i':
			args->iters = (uint64_t)strtoull(optarg, NULL, 10);
			has_iters	= 1;
			break;
		case 'r':
			args->reuse = atoi(optarg);
			break;
		case 'R':
			args->region_bytes = parse_size(optarg);
			break;
		case 'I':
			args->reuse_iter = (uint64_t)strtoull(optarg, NULL, 10);
			break;
		case 'S':
			args->seed = (uint64_t)strtoull(optarg, NULL, 0);
			break;
		case 'p':
			args->pin = atoi(optarg);
			break;
		case 'P':
			args->report_interval = atof(optarg);
			break;
		case 'h':
			cli_usage(argv[0]);
			exit(0);
		default:
			return -1;
		}
	}

	// If iters specified, use iteration-based stop
	if (has_iters && !has_seconds) {
		args->stop_mode = STOP_ITERS;
	} else if (has_iters && has_seconds) {
		// Both specified: prefer time-based (documented behavior)
		args->stop_mode = STOP_TIME;
		fprintf(
			stderr,
			"Warning: both --seconds and --iters specified; using time-based stop\n");
	}

	// Validate
	if (args->mode == MODE_SINGLE && args->bench_name[0] == '\0') {
		fprintf(stderr, "Error: --bench required for single mode\n");
		return -1;
	}
	if ((args->mode == MODE_SEQ || args->mode == MODE_CONCURRENT) &&
		args->bench_count == 0) {
		fprintf(stderr, "Error: --benches required for seq/concurrent mode\n");
		return -1;
	}

	return 0;
}
