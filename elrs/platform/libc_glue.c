// Minimal newlib glue for the image: a heap inside the image RAM window,
// no files. ELRS allocates a few objects with new during setup().
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

extern char __heap_start[], __heap_end[];
static char *heap_ptr;

void *_sbrk(ptrdiff_t incr)
{
  if (!heap_ptr) heap_ptr = __heap_start;
  if (heap_ptr + incr > __heap_end) { errno = ENOMEM; return (void *)-1; }
  char *prev = heap_ptr;
  heap_ptr += incr;
  return prev;
}

void tango2_heap_reset(void) { heap_ptr = 0; }

int _write(int fd, const void *buf, size_t n) { (void)fd; (void)buf; return (int)n; }
int _read(int fd, void *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int code) { (void)code; for (;;) {} }
void abort(void) { for (;;) {} }
void *__dso_handle;
