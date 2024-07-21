#ifdef WIN32
#include "wingetopt.h"
#else
#include <getopt.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <assert.h>
#include "cmph.h"
#include "hash.h"

#ifdef WIN32
#define VERSION "0.8"
#else
#include "config.h"
#endif


void usage(const char *prg)
{
	fprintf(stderr, "usage: %s [-v] [-h] [-V] [-k nkeys] [-f hash_function] [-g [-c algorithm_dependent_value][-s seed] ] [-a algorithm] [-M memory_in_MB] [-b algorithm_dependent_value] [-t keys_per_bin] [-d tmp_dir] [-m file.mph]  keysfile\n", prg);
}
void usage_long(const char *prg)
{
	cmph_uint32 i;
	fprintf(stderr, "usage: %s [-v] [-h] [-V] [-k nkeys] [-f hash_function] [-g [-c algorithm_dependent_value][-s seed] ] [-a algorithm] [-M memory_in_MB] [-b algorithm_dependent_value] [-t keys_per_bin] [-d tmp_dir] [-m file.mph] keysfile\n", prg);
	fprintf(stderr, "Minimum perfect hashing tool\n\n");
	fprintf(stderr, "  -h\t print this help message\n");
	fprintf(stderr, "  -c\t c value determines:\n");
	fprintf(stderr, "    \t  * the number of vertices in the graph for the algorithms BMZ and CHM\n");
	fprintf(stderr, "    \t  * the number of bits per key required in the FCH algorithm\n");
	fprintf(stderr, "    \t  * the load factor in the CHD_PH algorithm\n");
	fprintf(stderr, "  -a\t algorithm - valid values are\n");
	for (i = 0; i < CMPH_COUNT; ++i) fprintf(stderr, "    \t  * %s\n", cmph_names[i]);
	fprintf(stderr, "  -f\t hash function (may be used multiple times) - valid values are\n");
	for (i = 0; i < CMPH_HASH_COUNT; ++i) fprintf(stderr, "    \t  * %s\n", cmph_hash_names[i]);
	fprintf(stderr, "  -V\t print version number and exit\n");
	fprintf(stderr, "  -v\t increase verbosity (may be used multiple times)\n");
	fprintf(stderr, "  -k\t number of keys\n");
	fprintf(stderr, "  -g\t generation mode\n");
	fprintf(stderr, "  -s\t random seed\n");
	fprintf(stderr, "  -m\t minimum perfect hash function file \n");
	fprintf(stderr, "  -M\t main memory availability (in MB) used in BRZ algorithm \n");
	fprintf(stderr, "  -d\t temporary directory used in BRZ algorithm \n");
	fprintf(stderr, "  -b\t the meaning of this parameter depends on the algorithm selected in the -a option:\n");
	fprintf(stderr, "    \t  * For BRZ it is used to make the maximal number of keys in a bucket lower than 256.\n");
	fprintf(stderr, "    \t    In this case its value should be an integer in the range [64,175]. Default is 128.\n\n");
	fprintf(stderr, "    \t  * For BDZ it is used to determine the size of some precomputed rank\n");
	fprintf(stderr, "    \t    information and its value should be an integer in the range [3,10]. Default\n");
	fprintf(stderr, "    \t    is 7. The larger is this value, the more compact are the resulting functions\n");
	fprintf(stderr, "    \t    and the slower are them at evaluation time.\n\n");
	fprintf(stderr, "    \t  * For CHD and CHD_PH it is used to set the average number of keys per bucket\n");
	fprintf(stderr, "    \t    and its value should be an integer in the range [1,32]. Default is 4. The\n");
	fprintf(stderr, "    \t    larger is this value, the slower is the construction of the functions.\n");
	fprintf(stderr, "    \t    This parameter has no effect for other algorithms.\n\n");
	fprintf(stderr, "  -t\t set the number of keys per bin for a t-perfect hashing function. A t-perfect\n");
	fprintf(stderr, "    \t hash function allows at most t collisions in a given bin. This parameter applies\n");
	fprintf(stderr, "    \t only to the CHD and CHD_PH algorithms. Its value should be an integer in the\n");
	fprintf(stderr, "    \t range [1,128]. Defaul is 1\n");
	fprintf(stderr, "  keysfile\t line separated file with keys\n");
}