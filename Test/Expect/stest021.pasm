PUB main
  coginit(0, @entry, 0)
DAT
	org	0
entry

_waitcycles
	mov	_var_end, CNT
	add	_var_end, arg1
L__0001
	cmps	CNT, _var_end wc,wz
 if_b	jmp	#L__0001
_waitcycles_ret
	ret

_var_end
	long	0
arg1
	long	0
arg2
	long	0
arg3
	long	0
arg4
	long	0
result1
	long	0
	fit	496
