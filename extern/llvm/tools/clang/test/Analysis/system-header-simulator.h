#pragma clang system_header

typedef struct _FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
// Include a variant of standard streams that occur in the pre-processed file.
extern FILE *__stdinp;
extern FILE *__stdoutp;
extern FILE *__stderrp;


int fscanf(FILE *restrict, const char *restrict, ...);

// Note, on some platforms errno macro gets replaced with a function call.
extern int errno;

unsigned long strlen(const char *);

char *strcpy(char *restrict, const char *restrict);

typedef unsigned long __darwin_pthread_key_t;
typedef __darwin_pthread_key_t pthread_key_t;
int pthread_setspecific(pthread_key_t, const void *);

typedef long long __int64_t;
typedef __int64_t __darwin_off_t;
typedef __darwin_off_t fpos_t;

void setbuf(FILE * restrict, char * restrict);
int setvbuf(FILE * restrict, char * restrict, int, size_t);

FILE *funopen(const void *,
                 int (*)(void *, char *, int),
                 int (*)(void *, const char *, int),
                 fpos_t (*)(void *, fpos_t, int),
                 int (*)(void *));

