/* Apeluri de sistem minime pentru newlib-nano (fără sistem de operare).
 * Doar _sbrk contează (heap pentru snprintf); restul există ca linkerul să nu se plângă.
 *
 * Fișierul e folosit DOAR la build-ul cu GCC (arm-none-eabi, Makefile), unde biblioteca C e newlib
 * și cere aceste funcții „de sistem”. Proiectul Keil (stm32-keil/) NU îl compilează: acolo biblioteca
 * e MicroLIB („Use MicroLIB” bifat), care nu are nevoie de ele, iar simbolurile _end/_estack/
 * _Min_Stack_Size vin din scriptul de link GCC (.ld) și nu există în Keil.
 *
 * Nimic din firmware nu citește/scrie fișiere: _write ignoră datele (jurnalul merge prin dbg_log
 * direct pe UART), _read nu întoarce nimic.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

extern uint8_t _end;              /* sfârșitul .bss, din scriptul de link */
extern uint8_t _estack;
extern uint32_t _Min_Stack_Size;

/* Mărește heap-ul cu incr octeți (malloc din newlib, folosit intern de vsnprintf pentru unele
 * formate). Heap-ul crește de la sfârșitul .bss (_end) în sus, până la limita
 * vârful stivei (_estack) minus stiva minimă rezervată (_Min_Stack_Size, din .ld). Întoarce
 * începutul zonei noi sau (void*)-1 cu errno = ENOMEM dacă s-ar suprapune peste stivă. */
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

/* Stub-uri: nu există fișiere. _fstat/_isatty spun că orice descriptor e un terminal (caracter),
 * ca stdio să nu încerce buffer mare; _write „reușește” fără să trimită nimic; _exit nu are unde
 * să se întoarcă, deci rămâne în buclă. */
int _close(int fd)                  { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)                 { (void)fd; return 1; }
int _lseek(int fd, int p, int d)    { (void)fd; (void)p; (void)d; return 0; }
int _read(int fd, char *b, int n)   { (void)fd; (void)b; (void)n; return 0; }
int _write(int fd, char *b, int n)  { (void)fd; (void)b; return n; }
int _getpid(void)                   { return 1; }
int _kill(int pid, int sig)         { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status)              { (void)status; while (1) { } }
