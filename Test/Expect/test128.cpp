#define __SPIN2CPP__
#include <propeller.h>
#include "test128.h"

typedef void (*Cogfunc__)(void *a, void *b, void *c, void *d);
static void Cogstub__(void *argp) {
  void **arg = (void **)argp;
  Cogfunc__ func = (Cogfunc__)(arg[0]);
  func(arg[1], arg[2], arg[3], arg[4]);
}
__asm__(".global _cogstart\n"); // force clone_cog to link if it is present
extern "C" void _clone_cog(void *tmp) __attribute__((weak));
extern "C" long _load_start_kernel[] __attribute__((weak));
static int32_t Coginit__(int cogid, void *stackbase, size_t stacksize, void *func, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4) {
    void *tmp = _load_start_kernel;
    unsigned int *sp = ((unsigned int *)stackbase) + stacksize/4;
    static int32_t cogargs__[5];
    int r;
    cogargs__[0] = (int32_t) func;
    cogargs__[1] = arg1;
    cogargs__[2] = arg2;
    cogargs__[3] = arg3;
    cogargs__[4] = arg4;
    if (_clone_cog) {
        tmp = __builtin_alloca(1984);
        _clone_cog(tmp);
    }
    *--sp = 0;
    *--sp = (unsigned int)cogargs__;
    *--sp = (unsigned int)Cogstub__;
    r = coginit(cogid, tmp, sp);
    return r;
}
void test128::Demo(void)
{
  int32_t _local__0001[2];
  _local__0001[0] = 2;
  Coginit__(30, (void *)Sqstack, 24, (void *)Square, (int32_t)(&_local__0001[0]), 0, 0, 0);
}

void test128::Square(int32_t Xaddr)
{
  // Square the value at XAddr
  while (1) {
    ((int32_t *)Xaddr)[0] = ((int32_t *)Xaddr)[0] * ((int32_t *)Xaddr)[0];
    waitcnt((80000000 + _CNT));
  }
}

