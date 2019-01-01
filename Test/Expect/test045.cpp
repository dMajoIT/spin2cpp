#include <propeller.h>
#include "test045.h"

void test045::Fun(int32_t X, int32_t Y)
{
  switch(X) {
  case 0:
    switch(Y) {
    case 0:
      _OUTA ^= 0x1;
      break;
    case 1:
      _OUTA ^= 0x2;
      break;
    }
    break;
  case 20:
    _OUTA ^= 0x4;
    break;
  default:
    _OUTA ^= 0x8;
    break;
  }
}

