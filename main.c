#define _GNU_SOURCE

#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <argp.h>

#define BUF_SIZE (1024 * 1024)

struct io_def
{
	const char *file;
	int fd;
	size_t offset;
};

struct arguments
{
	struct io_def input;
	struct io_def output;
	size_t len;
};

enum ARG_FLAG
{
	ARG_INPUT_FILE,
	ARG_INPUT_OFFSET,
	ARG_OUTPUT_FILE,
	ARG_OUTPUT_OFFSET,
	ARG_LEN
};

struct argp_option options[] = {
	{
		.name = "input",
		.key = ARG_INPUT_FILE,
		.arg = "FILE",
		.doc = "input file name (required)",
		.group = 0
	},
	{
		.name = "input-offset",
		.key = ARG_INPUT_OFFSET,
		.arg = "OFFSET",
		.doc = "input offset in bytes (default: 0)",
		.group = 0
	},
	{
		.name = "output",
		.key = ARG_OUTPUT_FILE,
		.arg = "FILE",
		.doc = "output file name (required)",
		.group = 1
	},
	{
		.name = "output-offset",
		.key = ARG_OUTPUT_OFFSET,
		.arg = "OFFSET",
		.doc = "output offset in bytes (default: 0)",
		.group = 1
	},
	{
		.name = "length",
		.key = ARG_LEN,
		.arg = "LENGTH",
		.doc = "length in bytes (default: input file size)",
		.group = 2
	},
	{ }
};

error_t parse_arg(int key, char *arg, struct argp_state *state)
{
	char *endptr;
	struct arguments *arguments = state->input;
	switch (key)
	{
		case ARG_INPUT_FILE:
			arguments->input.file = arg;
			break;
		case ARG_INPUT_OFFSET:
			arguments->input.offset = strtol(arg, &endptr, 10);
			if (*endptr != 0) return EINVAL;
			break;
		case ARG_OUTPUT_FILE:
			arguments->output.file = arg;
			break;
		case ARG_OUTPUT_OFFSET:
			arguments->output.offset = strtol(arg, &endptr, 10);
			if (*endptr != 0) return EINVAL;
			break;
		case ARG_LEN:
			arguments->len = strtol(arg, &endptr, 10);
			if (*endptr != 0) return EINVAL;
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

struct argp argp = {
	.options = options,
	.parser = parse_arg,
	.doc = "Move data from an input file to an output file.\n\n"
	"Reads a specified number of bytes from the input file at a given offset and writes them to the output file at a specified offset. "
	"After writing each chunk of data, the storage space in the input file is immediately freed using fallocate with FALLOC_FL_PUNCH_HOLE. "
	"This allows for moving large sections of data without requiring significant storage overhead."
};

int main(int argc, char **argv)
{
	struct arguments arguments = { };
	size_t offset = 0;
	size_t len = 0, remaining_len;
	char buf[BUF_SIZE];

	arguments.len = SIZE_MAX;

	errno = argp_parse(&argp, argc, argv, 0, NULL, &arguments);
	if (errno)
	{
		perror("argp_parse");
		return errno;
	}

	if (!arguments.input.file)
	{
		errno = EINVAL;
		perror("input");
		argp_help(&argp, stderr, ARGP_HELP_USAGE, argv[0]);
		return EINVAL;
	}

	if (!arguments.output.file)
	{
		errno = EINVAL;
		perror("output");
		argp_help(&argp, stderr, ARGP_HELP_USAGE, argv[0]);
		return EINVAL;
	}

	arguments.input.fd = open(arguments.input.file, O_RDWR|O_CLOEXEC);
	if (arguments.input.fd == -1)
	{
		perror("open input");
		return errno;
	}

	arguments.output.fd = open(arguments.output.file, O_WRONLY|O_CREAT|O_CLOEXEC, 0644);
	if (arguments.output.fd == -1)
	{
		perror("open output");
		return errno;
	}

	struct stat stat;
	if (fstat(arguments.input.fd, &stat))
	{
		perror("fstat input");
		return errno;
	}

	len = stat.st_size - arguments.input.offset;
	if (len < arguments.len) arguments.len = len;

	if (fallocate(arguments.output.fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, arguments.output.offset, len))
	{
		perror("fallocate output");
		return errno;
	}

	remaining_len = arguments.len;
	while (remaining_len)
	{
		offset = lseek(arguments.input.fd, arguments.input.offset + offset, SEEK_DATA);
		if (offset == -1ul)
		{
			if (errno == ENXIO) break;
			perror("seek input");
			return errno;
		}

		offset -= arguments.input.offset;
		remaining_len = arguments.len - offset;

		len = (remaining_len < BUF_SIZE) ? remaining_len : BUF_SIZE;
		len = read(arguments.input.fd, &buf, len);
		if (len == -1ul)
		{
			perror("read input");
			return errno;
		}

		if (lseek(arguments.output.fd, arguments.output.offset + offset, SEEK_SET) == -1)
		{
			perror("seek output");
			return errno;
		}

		len = write(arguments.output.fd, &buf, len);
		if (len == -1ul)
		{
			perror("write output");
			return errno;
		}

		if (fallocate(arguments.input.fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, arguments.input.offset + offset, len))
		{
			perror("fallocate input");
			return errno;
		}

		offset += len;
		remaining_len -= len;
	}

	close(arguments.input.fd);
	close(arguments.output.fd);

	return 0;
}
