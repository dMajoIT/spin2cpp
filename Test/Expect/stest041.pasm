DAT
	org	0

_count1
	mov	_count1_i, #5
L_039_
	xor	OUTA, #2
	djnz	_count1_i, #L_039_
_count1_ret
	ret

_count1_i
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
