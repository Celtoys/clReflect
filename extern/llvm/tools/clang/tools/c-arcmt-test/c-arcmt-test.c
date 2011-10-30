/* c-arcmt-test.c */

#include "clang-c/Index.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

static int print_remappings(const char *path) {
  CXRemapping remap;
  unsigned i, N;
  CXString origFname;
  CXString transFname;

  remap = clang_getRemappings(path);
  if (!remap)
    return 1;

  N = clang_remap_getNumFiles(remap);
  for (i = 0; i != N; ++i) {
    clang_remap_getFilenames(remap, i, &origFname, &transFname);

    fprintf(stdout, "%s\n", clang_getCString(origFname));
    fprintf(stdout, "%s\n", clang_getCString(transFname));

    clang_disposeString(origFname);
    clang_disposeString(transFname);
  }

  clang_remap_dispose(remap);
  return 0;
}

/******************************************************************************/
/* Command line processing.                                                   */
/******************************************************************************/

static void print_usage(void) {
  fprintf(stderr,
    "usage: c-arcmt-test -arcmt-migrate-directory <path>\n\n\n");
}

/***/

int carcmttest_main(int argc, const char **argv) {
  clang_enableStackTraces();
  if (argc == 3 && strncmp(argv[1], "-arcmt-migrate-directory", 24) == 0)
    return print_remappings(argv[2]);

  print_usage();
  return 1;
}

/***/

/* We intentionally run in a separate thread to ensure we at least minimal
 * testing of a multithreaded environment (for example, having a reduced stack
 * size). */

typedef struct thread_info {
  int argc;
  const char **argv;
  int result;
} thread_info;
void thread_runner(void *client_data_v) {
  thread_info *client_data = client_data_v;
  client_data->result = carcmttest_main(client_data->argc, client_data->argv);
}

int main(int argc, const char **argv) {
  thread_info client_data;

#if defined(_WIN32)
  if (getenv("LIBCLANG_LOGGING") == NULL)
    putenv("LIBCLANG_LOGGING=1");
  _setmode( _fileno(stdout), _O_BINARY );
#else
  setenv("LIBCLANG_LOGGING", "1", /*overwrite=*/0);
#endif

  if (getenv("CINDEXTEST_NOTHREADS"))
    return carcmttest_main(argc, argv);

  client_data.argc = argc;
  client_data.argv = argv;
  clang_executeOnThread(thread_runner, &client_data, 0);
  return client_data.result;
}
