pub main
  coginit(0, @entry, 0)
dat
	org	0
entry

_foo
	add	arg01, #4
	rdlong	result1, arg01
_foo_ret
	ret

__struct__bar_getx
	add	objptr, #4
	rdlong	result1, objptr
	sub	objptr, #4
__struct__bar_getx_ret
	ret

objptr
	long	@@@objmem
result1
	long	0
COG_BSS_START
	fit	496
objmem
	long	0[0]
	org	COG_BSS_START
arg01
	res	1
	fit	496
