''
'' Nucode specific functions
''

''
'' divide (n, nlo) by d, producing qlo and rlo (used in FRAC operation)
''
pri _div64(n, nlo, dlo) : qlo, rlo
  __bytecode__("DIV64")

pri _waitx(tim)
  __bytecode__("WAITX")

pri _pinr(pin) : val
  __bytecode__("PINR")
  
pri _getcnthl : rl = +long, rh = +long
  __bytecode__("GETCTHL")

pri _getcnt : r = +long | rh
  r, rh := _getcnthl()
  return r

pri _cogid : r = long
  __bytecode__("COGID")

pri _cogstop(x)
  __bytecode__("COGSTOP")

pri _cogchk(id) : r
  __bytecode__("COGCHK")

pri _muldiv64(mult1, mult2, divisor) : r
  __bytecode__("MULDIV64")
  
pri _drvl(pin)
  __bytecode__("DRVL")
pri _drvh(pin)
  __bytecode__("DRVH")
pri _drvnot(pin)
  __bytecode__("DRVNOT")
pri _drvrnd(pin)
  __bytecode__("DRVRND")

pri _dirl(pin)
  __bytecode__("DIRL")
pri _dirh(pin)
  __bytecode__("DIRH")

pri _fltl(pin)
  __bytecode__("FLTL")

pri _wrpin(pin, val)
  __bytecode__("WRPIN")
pri _wxpin(pin, val)
  __bytecode__("WXPIN")
pri _wypin(pin, val)
  __bytecode__("WYPIN")
pri _rdpin(pin) : r
  __bytecode__("RDPIN")

pri _waitcnt(x)
  __bytecode__("WAITCNT")

pri waitcnt(x)
  __bytecode__("WAITCNT")
  
dat
    orgh
_rx_temp   long 0

con
 _rxpin = 63
 _txpin = 62

  _txmode       = %0000_0000_000_0000000000000_01_11110_0 'async tx mode, output enabled for smart output
  _rxmode       = %0000_0000_000_0000000000000_00_11111_0 'async rx mode, input  enabled for smart input

pri _setbaud(baudrate) | bitperiod, bit_mode
  bitperiod := (__clkfreq_var / baudrate)
  _fltl(_txpin)
  _fltl(_rxpin)
  long[$1c] := baudrate
  bit_mode := 7 + (bitperiod << 16)
  _wrpin(_txpin, _txmode)
  _wxpin(_txpin, bit_mode)
  _wrpin(_rxpin, _rxmode)
  _wxpin(_rxpin, bit_mode)
  _drvl(_txpin)
  _drvl(_rxpin)
  
pri _txraw(c) | z
  if long[$1c] == 0
    _setbaud(__default_baud__)  ' set up in common.c
  _wypin(_txpin, c)
  repeat
    z := _pinr(_txpin)
  while z == 0
  return 1

' timeout is approximately in milliseconds (actually in 1024ths of a second)
pri _rxraw(timeout = 0) : rxbyte = long | z, endtime, temp2, rxpin
  if long[$1c] == 0
    _setbaud(__default_baud__)
  if timeout
    endtime := _getcnt() + timeout * (__clkfreq_var >> 10)
  rxbyte := -1
  rxpin := _rxpin
  z := 0
  repeat
    z := _pinr(rxpin)
    if z
      rxbyte := _rdpin(rxpin)>>24
      quit
    if timeout
      if _getcnt() - endtime < 0
        quit

''
'' memset/memmove are here (in processor specific code)
'' because on P2 we can optimize them (long operations do
'' not have to be aligned)
''
pri __builtin_memset(ptr, val, count) : r | lval
  r := ptr
  lval := (val << 8) | val
  lval := (lval << 16) | lval
  repeat while (count > 3)
    long[ptr] := lval
    ptr += 4
    count -= 4
  repeat count
    byte[ptr] := val
    ptr += 1
    
pri __builtin_memmove(dst, src, count) : origdst
  origdst := dst
  if (dst < src)
    repeat while (count > 3)
      long[dst] := long[src]
      dst += 4
      src += 4
      count -= 4
    repeat count
      byte[dst] := byte[src]
      dst += 1
      src += 1
  else
    dst += count
    src += count
    repeat count
      dst -= 1
      src -= 1
      byte[dst] := byte[src]

'
' should these be builtins for NuCode as well?
'
pri longfill(ptr, val, count)
  repeat count
    long[ptr] := val
    ptr += 4
pri wordfill(ptr, val, count)
  repeat count
    word[ptr] := val
    ptr += 2
pri bytefill(ptr, val, count)
  __builtin_memset(ptr, val, count)

pri longmove(dst, src, count) : origdst
  origdst := dst
  if dst < src
    repeat count
      long[dst] := long[src]
      dst += 4
      src += 4
  else
    dst += 4*count
    src += 4*count
    repeat count
      dst -= 4
      src -= 4
      long[dst] := long[src]
      
pri wordmove(dst, src, count) : origdst
  origdst := dst
  if dst < src
    repeat count
      word[dst] := word[src]
      dst += 2
      src += 2
  else
    dst += 2*count
    src += 2*count
    repeat count
      dst -= 2
      src -= 2
      word[dst] := word[src]

pri bytemove(dst, src, count)
  return __builtin_memmove(dst, src, count)

pri __builtin_memcpy(dst, src, count)
  return __builtin_memmove(dst, src, count)

pri __builtin_strlen(str) : r=long
  r := 0
  repeat while byte[str] <> 0
    r++
    str++
pri __builtin_strcpy(dst, src) : r=@byte | c
  r := dst
  repeat
    c := byte[src++]
    byte[dst++] := c
  until c==0
pri strcomp(s1, s2) | c1, c2
  repeat
    c1 := byte[s1++]
    c2 := byte[s2++]
    if (c1 <> c2)
      return 0
  until (c1 == 0)
  return -1

pri _sqrt64(lo, hi) : r
  __bytecode__("SQRT64")

pri _sqrt(a) : r
  if (a =< 0)
    return 0
  return _sqrt64(a, 0)

pri _lockmem(addr)
  __bytecode__("LOCKMEM")

pri _unlockmem(addr) | oldlock
  long[addr] := 0

pri __topofstack(ptr) : r
  return @ptr

pri __get_heap_base() : r
  return long[$30]

pri _lookup(x, b, arr, n) | i
  i := x - b
  if (i => 0 and i < n)
    return long[arr][i]
  return 0
pri _lookdown(x, b, arr, n) | i
  repeat i from 0 to n-1
    if (long[arr] == x)
      return i+b
    arr += 4
  return 0

'
' random number generators
'
pri _lfsr_forward(x) : r
  __bytecode__("XORO")

pri _lfsr_backward(x) : r
  __bytecode__("XORO")

'
' time stuff
'
pri _getus() : freq = +long | lo, hi
  lo,hi := _getcnthl()
  freq := __clkfreq_us
  if freq == 0
    __clkfreq_us := freq := __clkfreq_var +/ 1000000
  hi := hi +// freq
  lo, hi := _div64(lo, hi, freq)
  return lo

pri _hubset(x)
  __bytecode__("HUBSET")
  
pri _clkset(mode, freq) | oldmode, xsel
  xsel := mode & 3
  if xsel == 0 and mode > 1
    xsel := 3
  oldmode := __clkmode_var & !3  ' remove low bits, if any
  __clkfreq_var := freq
  __clkmode_var := mode
  mode := mode & !3
  _hubset(oldmode)  ' go to RCFAST using known prior mode
  _hubset(mode)     ' setup for new mode, still RCFAST
  _waitx(20_000_000/100)
  mode |= xsel
  _hubset(mode)     ' activate new mode
  __clkfreq_ms := freq / 1000
  __clkfreq_us := freq / 1000000

pri _reboot
  _clkset(0, 0)
  _hubset(%0001 << 28)

pri _make_methodptr(o, func) | ptr
  ptr := _gc_alloc_managed(8)
  if (ptr)
    long[ptr] := o
    long[ptr+4] := func
  return ptr
