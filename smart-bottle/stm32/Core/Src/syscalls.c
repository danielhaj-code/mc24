/* Apeluri de sistem minime pentru newlib-nano (fără sistem de operare).
 * Doar _sbrk contează (heap pentru snprintf); restul există ca linkerul să nu se plângă. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

extern uint8_t _end;              /* sfârșitul .bss, din scriptul de link */
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

void *_sbrk(ptrdiff_t incr)
{
    static uint8_t *heap_end;
    const uint8_t *limit = &_estack - (uint32_t)&_Min_Stack_Size;
    if (!heap_end) heap_end = &_end;
    if (heap_end + incr > limit) { errno = ENOMEM; return (void *)-1; }
    uint8_t *prev = heap_end;
    heap_end += incr;
    return prev;
}

int _close(int fd)                  { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                 { (void)fd; return 1; }
int _lseek(int fd, int p, int d)    { (void)fd; (void)p; (void)d; return 0; }
int _read(int fd, char *b, int n)   { (void)fd; (void)b; (void)n; return 0; }
int _write(int fd, char *b, int n)  { (void)fd; (void)b; return n; }
int _getpid(void)                   { return 1; }
int _kill(int pid, int sig)         { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status)              { (void)status; while (1) { } }
