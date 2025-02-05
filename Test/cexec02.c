#include <stdio.h>
#include <propeller.h>
#undef printf

void myexit(int n)
{
    _txraw(0xff);
    _txraw(0x0);
    _txraw(n);
    waitcnt(getcnt() + 40000000);
#ifdef __OUTPUT_BYTECODE__
    _cogstop(1);
    _cogstop(_cogid());
#else
    __asm {
        cogid n
        cogstop n
    }
#endif    
}

void main()
{
    long long x = 0x1122334455667788;
    long long y;
    y = x + x;
    printf("x = %llx y = %x : %x z = %llx\n", x, y, 0x9abcdef01234LL, 1);
    myexit(0);
}
