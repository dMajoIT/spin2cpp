function left$(x as string, n as integer) as string
  dim p as ubyte pointer
  dim i, m as integer

  if (n <= 0) return ""
  m = __builtin_strlen(x)
  if (m <= n) return x
  p = new ubyte(n+1)
  if (p) then
    bytemove(p, x, n)
    p(n) = 0
  end if
  return p
end function

function right$(x as string, n as integer) as string
  dim p as ubyte pointer
  dim i, m as integer

  if (n <= 0) return ""
  m = __builtin_strlen(x)
  if (m <= n) return x
  p = new ubyte(n+1)
  if (p) then
    i = m - n
    bytemove(p, @x(i), n+1)
  end if
  return p
end function

function mid$(x as string, i=0, j=9999999) as string
  dim p as ubyte pointer
  dim m, n
  if (j <= 0) return ""
  i = i-1 ' convert from 1 based to 0 based
  m = __builtin_strlen(x)
  if (m < i) return ""

  ' calculate number of chars we will copy
  n = (m-i)
  if (n > j) then
    n = j
  endif
  p = new ubyte(n+1)
  if p then
    bytemove(p, @x(i), n)
    p(n) = 0
  end if
  return p
end function

function chr$(x as integer) as string
  dim p as ubyte pointer
  p = new ubyte(2)
  if (p) then
    p(0) = x
    p(1) = 0
  end if
  return p
end function

class __strs_cl
  dim p as ubyte pointer
  dim i as integer
  function pfunc(c as integer) as integer
    if (i < 16) then
      p(i) = c
      i = i+1
      return 1
    else
      return -1
    end if
  end function
end class

function str$(x as single) as string
  dim p as ubyte pointer
  dim i as integer
  dim g as __strs_cl pointer
  p = new ubyte(15)
  i = 0
  if p then
    g = __builtin_alloca(8)
    g(0).p = p
    g(0).i = i
    _fmtfloat(@g(0).pfunc, 0, x, ASC("g"))
  end if
  '' FIXME: should we check here that i is sensible?
  return p
end function
