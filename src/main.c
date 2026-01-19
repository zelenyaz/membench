#include <stdio.h>
#include <stdlib.h>
#include "cli.h"
#include "runner.h"
#include "bench.h"

int main(int argc, char **argv)
{
	cli_args_t args;

	if (cli_parse(argc, argv, &args) < 0) {
		cli_usage(argv[0]);
		return 1;
	}

	int ret = 0;

	switch (args.mode) {
	case MODE_SINGLE:
		ret = run_single(&args);
		break;
	case MODE_SEQ:
		ret = run_sequential(&args);
		break;
	case MODE_CONCURRENT:
		ret = run_concurrent(&args);
		break;
	default:
		fprintf(stderr, "Unknown mode\n");
		ret = 1;
	}

	return ret;
}
