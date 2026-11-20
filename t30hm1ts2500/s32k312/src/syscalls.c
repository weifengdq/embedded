#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

extern uint32_t _end;
extern uint32_t _heap_start;
extern uint32_t _heap_end;

static uint8_t *heap_ptr = 0;

void *_sbrk(ptrdiff_t incr)
{
    if (heap_ptr == 0) {
        heap_ptr = (uint8_t*)&_end;
    }
    uint8_t *prev = heap_ptr;
    if (heap_ptr + incr > (uint8_t*)&_heap_end) {
        errno = ENOMEM;
        return (void*)-1;
    }
    heap_ptr += incr;
    return (void*)prev;
}

int _close(int file) { (void)file; return -1; }
int _fstat(int file, struct stat *st) { (void)file; st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { (void)file; return 1; }
int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
void _exit(int status) { (void)status; while(1) {} }
