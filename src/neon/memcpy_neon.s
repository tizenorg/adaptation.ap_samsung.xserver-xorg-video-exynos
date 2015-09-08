/*
 *	Memory copy functions implemented using NEON
 *
 *	NOTICE:
 *		pld instruction caused no changes in performance.
 *	  	Does our processor actually deal with this??
 *		Performance was dropped when src and dst pointers are close
 *		to each other. (I don't know why, this was same for pixman_blt)
 *
 *	TODO:
 *	 	Benchmark small block copy and make it better.
 *	 	SW pipelining, proper prefetching.
 */
 
.fpu	neon
.text

.func memcpy_forward_32_neon
.global	memcpy_forward_32_neon

memcpy_forward_32_neon:
	push	{r4}

	mov		r3, r0

	cmp		r2, #16
	blt		2f

0:
	tst		r3, #15
	ldrne	r4, [r1], #4
	subne	r2, r2, #1
	strne	r4, [r3], #4
	bne		0b

1:
	vld1.8	{d0, d1, d2, d3}, [r1]!
	sub		r2, r2, #8
	cmp		r2, #8
	vst1.32 {d0, d1, d2, d3}, [r3, :128]!
	bge		1b

2:
	cmp		r2, #0
	ble		3f
	ldr		r4, [r1], #4
	sub		r2, r2, #1
	str		r4, [r3], #4
	b 		2b

3:
	pop		{r4}
	bx		lr
.endfunc


.func memcpy_backward_32_neon
.global	memcpy_backward_32_neon

memcpy_backward_32_neon:
	push	{r4}

	mov		r3, r0

	add		r3, r3, r2, asl #2
	add		r1,	r1, r2, asl #2

	cmp		r2, #16
	blt		2f

0:
	tst		r3, #15
	ldrne	r4, [r1, #-4]!
	subne	r2, r2, #1
	strne	r4, [r3, #-4]!
	bne		0b

1:
	cmp		r2, #8
	blt		2f
	sub		r1, r1, #32
	vld1.8	{d0, d1, d2, d3}, [r1]
	sub		r3, r3, #32
	sub		r2, r2, #8
	vst1.32	{d0, d1, d2, d3}, [r3, :128]
	b		1b

2:
	cmp		r2, #0
	ble		3f
	ldr		r4, [r1, #-4]!
	sub		r2, r2, #1
	str		r4, [r3, #-4]!
	b		2b

3:
	pop		{r4}
	bx		lr
.endfunc

.func memcpy_forward_16_neon
.global	memcpy_forward_16_neon

memcpy_forward_16_neon:
	push	{r4}

	mov		r3, r0

	cmp		r2, #16
	blt		2f

0:
	tst		r3, #15
	ldrneh	r4, [r1], #2
	subne	r2, r2, #1
	strneh	r4, [r3], #2
	bne		0b

1:
	cmp		r2, #8
	blt		2f
	vld1.8	{d0, d1}, [r1]!
	sub		r2, r2, #8
	vst1.32 {d0, d1}, [r3, :128]!
	b		1b

2:
	cmp		r2, #0
	ble		3f
	ldrh	r4, [r1], #2
	sub		r2, r2, #1
	strh	r4, [r3], #2
	b 		2b

3:
	pop		{r4}
	bx		lr
.endfunc


.func memcpy_backward_16_neon
.global	memcpy_backward_16_neon

memcpy_backward_16_neon:
	push	{r4}

	mov		r3, r0

	add		r3, r3, r2, asl #1
	add		r1,	r1, r2, asl #1

	cmp		r2, #16
	blt		2f

0:
	tst		r3, #15
	ldrneh	r4, [r1, #-2]!
	subne	r2, r2, #1
	strneh	r4, [r3, #-2]!
	bne		0b

1:
	cmp		r2, #8
	blt		2f
	sub		r1, r1, #16
	vld1.8	{d0, d1}, [r1]
	sub		r3, r3, #16
	sub		r2, r2, #8
	vst1.32	{d0, d1}, [r3, :128]
	b		1b

2:
	cmp		r2, #0
	ble		3f
	ldrh	r4, [r1, #-2]!
	sub		r2, r2, #1
	strh	r4, [r3, #-2]!
	b		2b

3:
	pop		{r4}
	bx		lr
.endfunc

/*
 *	Memory copy functions implemented using NEON
 */
		.text
		.fpu	neon
		.align	4

memcpy_neon:
        MOV      ip,r0

        CMP      r2,#0x41
        BCC      COPY_UNDER_64B

		CMP		 r2,#256
		BHI		 DST_ALIGN

		SUB	 	r2,r2,#0x40
COPY_BY_64B_s:
        VLD1.8   {d0,d1,d2,d3},[r1]!
		PLD		[r1,#0x60]
        SUBS     r2,r2,#0x40
        VST1.8   {d0,d1,d2,d3},[ip]!
        VLD1.8   {d4,d5,d6,d7},[r1]!
        VST1.8   {d4,d5,d6,d7},[ip]!
        BGE      COPY_BY_64B_s
		ANDS	r2,r2,#0x3f
		BXEQ	lr
		B 		COPY_UNDER_64B		

@		SUBS	r2,r2,#0x20
@COPY_BY_32B_s
@        VLD1.8   {d0,d1,d2,d3},[r1]!
@		PLD		[r1,#0x40]
@        SUBS     r2,r2,#0x20
@        VST1.8   {d0,d1,d2,d3},[ip]!
@        BGE      COPY_BY_32B_s
@		ANDS	r2,r2,#0x1f
@		B 		COPY_UNDER_64B		


DST_ALIGN:
		@ dst memory align to 16-byte
		TST      r0,#0xf
		BEQ      BLOCK_COPY
ALIGN_TO_1B:
		TST      r0,#1
		LDRNEB   r3,[r1],#1
		STRNEB   r3,[ip],#1
@		BEQ		 ALIGN_TO_2B
@		VLD1.8   {d7[0]},[r1]!
@		VST1.8   {d7[0]},[ip]!
		SUBNE    r2,r2,#1
ALIGN_TO_2B:	
		TST      ip,#2
		LDRNEH   r3,[r1],#2
		STRNEH   r3,[ip],#2
@		BEQ		 ALIGN_TO_4B
@		VLD2.8   {d5[0],d6[0]},[r1]!
@		VST2.8   {d5[0],d6[0]},[ip@16]!
		SUBNE    r2,r2,#2
ALIGN_TO_4B:
		TST      ip,#4
		LDRNE	 r3,[r1],#4
		STRNE	 r3,[ip],#4
@		BEQ      ALIGN_TO_16B@{pc} + 0x10  @ 0x48
@		VLD4.8   {d0[0],d1[0],d2[0],d3[0]},[r1]!
@		VST4.8   {d0[0],d1[0],d2[0],d3[0]},[ip,:32]!
		SUBNE    r2,r2,#4
ALIGN_TO_16B:
		TST      ip,#8
		LDRNE	 r3,[r1],#4
		STRNE	 r3,[ip],#4
		LDRNE	 r3,[r1],#4
		STRNE	 r3,[ip],#4
@		BEQ      BLOCK_COPY_CMP
@		VLD1.8   {d0},[r1]!
@		VST1.8   {d0},[ip@64]!
		SUBNE    r2,r2,#8

		@ from here
BLOCK_COPY_CMP:
		@ 16-byte align ????64 바이???�하??경우?
		CMP		 r2,#0x41
        BCC      COPY_UNDER_64B

BLOCK_COPY:
		SUBS	r2,r2,#0x40
		MOV		r3,#0x40
COPY_BY_64B:
        VLD1.8   {d0,d1,d2,d3},[r1]!
		CMP		r3,#0x320
		PLD		[r1,r3]
		ADDLE	r3,r3,#0x40
        SUBS     r2,r2,#0x40
        VST1.8   {d0,d1,d2,d3},[ip,:128]!
        VLD1.8   {d4,d5,d6,d7},[r1]!
        VST1.8   {d4,d5,d6,d7},[ip,:128]!
        BGE      COPY_BY_64B
		TST		r2,#0x3f
		BXEQ	lr
		TST		r2,#0x20
		BEQ		COPY_UNDER_16B
        VLD1.8   {d0,d1,d2,d3},[r1]!
        VST1.8   {d0,d1,d2,d3},[ip,:128]!

@		SUBS	r2,r2,#0x20
@		MOV		r3,#0x20
@COPY_BY_32B
@        VLD1.8   {d0,d1,d2,d3},[r1]!
@		CMP		r3,#0x140
@		PLD		[r1,r3]
@		ADDLE	r3,r3,#0x20
@        SUBS     r2,r2,#0x20
@        VST1.8   {d0,d1,d2,d3},[ip,:128]!
@        BGE      COPY_BY_32B
@		CMP		r2,#0x00
@		BXEQ	lr


COPY_UNDER_16B:
		TST		r2,#0x10
		BEQ		COPY_UNDER_15B
		VLD1.8	{d0,d1},[r1]!
		VST1.8	{d0,d1},[ip,:128]!

COPY_UNDER_15B:
		TST		r1,#0x03
		BNE		COPY_UNDER_15B_UNALIGN
COPY_UNDER_15B_ALIGN:
		@ src가 4-byte align ??경우 
		LSLS     r3,r2,#29
		LDRCS    r3,[r1],#4
		STRCS    r3,[ip],#4
		LDRCS    r3,[r1],#4
		STRCS    r3,[ip],#4
		LDRMI    r3,[r1],#4
		STRMI    r3,[ip],#4
		LSLS     r2,r2,#31
		LDRCSH   r3,[r1],#2
		STRCSH   r3,[ip],#2
		LDRMIB   r3,[r1],#1
		STRMIB   r3,[ip],#1
		BX		lr
COPY_UNDER_15B_UNALIGN:
		@ src가 4-byte align ???�닌 경우
		LSLS     r3,r2,#29
		BCC      COPY_UNDER_15B_UNALIGN_4B
		@ neon ?��?가?
		VLD1.8   {d0},[r1]!
		VST1.8   {d0},[ip]!
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
@		LDRBCS   r3,[r1],#1
@		STRBCS   r3,[ip],#1
COPY_UNDER_15B_UNALIGN_4B:
		BPL      COPY_UNDER_15B_UNALIGN_2B  
		LDRMIB   r3,[r1],#1
		STRMIB   r3,[ip],#1
		LDRMIB   r3,[r1],#1
		STRMIB   r3,[ip],#1
		LDRMIB   r3,[r1],#1
		STRMIB   r3,[ip],#1
		LDRMIB   r3,[r1],#1
		STRMIB   r3,[ip],#1
COPY_UNDER_15B_UNALIGN_2B:
		LSLS     r2,r2,#31
		LDRCSB   r3,[r1],#1
		STRCSB   r3,[ip],#1
		LDRCSB   r3,[r1],#1
		STRCSB   r3,[ip],#1
		LDRMIB   r3,[r1],#1
		STRMIB   r3,[ip],#1
		BX		lr

COPY_UNDER_64B:
        ADDNE    pc,pc,r2,LSL #2
        B        MEMCPY_WRAPUP
        B        MEMCPY_WRAPUP
        B        COPY_1B
        B        COPY_2B
        B        COPY_3B
        B        COPY_4B
        B        COPY_5B
        B        COPY_6B
        B        COPY_7B
        B        COPY_8B
        B        COPY_9B
        B        COPY_10B
        B        COPY_11B
        B        COPY_12B
        B        COPY_13B
        B        COPY_14B
        B        COPY_15B
        B        COPY_16B
        B        COPY_17B
        B        COPY_18B
        B        COPY_19B
        B        COPY_20B
        B        COPY_21B
        B        COPY_22B
        B        COPY_23B
        B        COPY_24B
        B        COPY_25B
        B        COPY_26B
        B        COPY_27B
        B        COPY_28B
        B        COPY_29B
        B        COPY_30B
        B        COPY_31B
        B        COPY_32B
        B        COPY_33B
        B        COPY_34B
        B        COPY_35B
        B        COPY_36B
        B        COPY_37B
        B        COPY_38B
        B        COPY_39B
        B        COPY_40B
        B        COPY_41B
        B        COPY_42B
        B        COPY_43B
        B        COPY_44B
        B        COPY_45B
        B        COPY_46B
        B        COPY_47B
        B        COPY_48B
        B        COPY_49B
        B        COPY_50B
        B        COPY_51B
        B        COPY_52B
        B        COPY_53B
        B        COPY_54B
        B        COPY_55B
        B        COPY_56B
        B        COPY_57B
        B        COPY_58B
        B        COPY_59B
        B        COPY_60B
        B        COPY_61B
        B        COPY_62B
        B        COPY_63B
        B        COPY_64B

@        ARM
@        REQUIRE8
@        PRESERVE8
@
@        AREA ||.text||, CODE, READONLY, ALIGN=4
COPY_1B:
		LDRB	r2,[r1]
		STRB	r2,[ip]
		BX		lr
COPY_1B_:
		VLD1.8    {d0[0]},[r1]
		VST1.8    {d0[0]},[ip]
		BX		lr
COPY_2B:
		ORR 	r3,ip,r1
		TST		r3,#3
		BNE		COPY_2B_
		LDRH	r2,[r1]
		STRH	r2,[ip]
        BXEQ	lr
COPY_2B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r3,[r1],#1
		STRB	r3,[ip],#1
@		VLD2.8		{d0[0],d1[0]},[r1]
@		VST2.8		{d0[0],d1[0]},[ip]
        BX		lr
COPY_3B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_3B_
		LDRH	r2,[r1,#0x00]
		STRH	r2,[ip,#0x00]
		LDRB	r2,[r1,#0x02]
		STRB	r2,[ip,#0x02]
        BX		lr
COPY_3B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r3,[r1],#1
		STRB	r3,[ip],#1
@		VLD3.8		{d0[0],d1[0],d2[0]},[r1]
@		VST3.8		{d0[0],d1[0],d2[0]},[ip]
        BX		lr
COPY_4B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_4B_
		LDR		r2,[r1]
		STR		r2,[ip]
        BX		lr
COPY_4B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
@		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]
@		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]
        BX		lr
COPY_5B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_5B_
		LDR		r2,[r1],#0x04
		STR		r2,[ip],#0x04
		LDRB	r2,[r1]
		STRB	r2,[ip]
        BX		lr
COPY_5B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8    {d4[0]},[r1]!
		VST1.8    {d4[0]},[ip]!
        BX		lr
COPY_6B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_6B_
		LDR		r2,[r1],#0x04
		STR		r2,[ip],#0x04
		LDRH	r3,[r1]
		STRH	r3,[ip]
        BX		lr
COPY_6B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
        BX		lr
COPY_7B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_7B_
		LDR		r2,[r1],#0x04
		STR		r2,[ip],#0x04
		LDRH	r2,[r1],#0x02
		STRH	r2,[ip],#0x02
		LDRB	r2,[r1]
		STRB	r2,[ip]
        BX		lr
COPY_7B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
@		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
@		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
@		VLD3.8    {d4[0],d5[0],d6[0]},[r1]!
@		VST3.8    {d4[0],d5[0],d6[0]},[ip]!
        BX		lr
COPY_8B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_8B_
		LDMEQ	r1!,{r2,r3}
		STMEQ	ip!,{r2,r3}
        BXEQ	lr
COPY_8B_:
		VLD1.8    {d0},[r1]
		VST1.8    {d0},[ip]		
        BX		lr
COPY_9B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_9B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRB	r2,[r1]
		STRB	r2,[ip]
        BX		lr
COPY_9B_:
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1

		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1

		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
@		VLD1.8    {d0},[r1]!
@		VST1.8    {d0},[ip]!		
@		VLD1.8    {d4[0]},[r1]
@		VST1.8    {d4[0]},[ip]
        BX		lr
COPY_10B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_10B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1]
		STRH	r2,[ip]
        BX		lr
COPY_10B_:
		VLD1.8    {d0},[r1]!
		VST1.8    {d0},[ip]!		
		VLD2.8    {d4[0],d5[0]},[r1]
		VST2.8    {d4[0],d5[0]},[ip]
        BX		lr
COPY_11B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_11B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#0x02
		STRH	r2,[ip],#0x02
		LDRB	r2,[r1]
		STRB	r2,[ip]
        BX		lr
COPY_11B_:
		VLD1.8    {d0},[r1]!
		VST1.8    {d0},[ip]!
		VLD3.8    {d4[0],d5[0],d6[0]},[r1]
		VST3.8    {d4[0],d5[0],d6[0]},[ip]
        BX		lr
COPY_12B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_12B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDR		r2,[r1]
		STR		r2,[ip]
        BX		lr
COPY_12B_:
		VLD1.8    {d0},[r1]!
		VST1.8    {d0},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]
        BX		lr
COPY_13B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_13B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDR		r2,[r1],#0x04
		STR		r2,[ip],#0x04
		LDRB	r3,[r1]
		STRB	r3,[ip]
        BX		lr
COPY_13B_:
		VLD1.8    {d0},[r1]!
		VST1.8    {d0},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8		{d2[0]},[r1]
		VST1.8		{d2[0]},[ip]
        BX		lr
COPY_14B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_14B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDR		r2,[r1],#0x04
		STR		r2,[ip],#0x04
		LDRH	r3,[r1]
		STRH	r3,[ip]
        BX		lr
COPY_14B_:
		VLD1.8    {d0},[r1]!
		VST1.8    {d0},[ip]!		
		VLD4.8		{d1[0],d2[0],d3[0],d4[0]},[r1]!
		VST4.8		{d1[0],d2[0],d3[0],d4[0]},[ip]!
		VLD2.8		{d5[0],d6[0]},[r1]
		VST2.8		{d5[0],d6[0]},[ip]
        BX		lr
COPY_15B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_15B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDR		r2,[r1],#0x04
		STR		r2,[ip],#0x04
		LDRH	r3,[r1],#0x02
		STRH	r3,[ip],#0x02
		LDRB	r2,[r1]
		STRB	r2,[ip]
        BX		lr
COPY_15B_:
		VLD1.8    {d0},[r1]!
		VST1.8    {d0},[ip]!		
		VLD4.8		{d1[0],d2[0],d3[0],d4[0]},[r1]!
		VST4.8		{d1[0],d2[0],d3[0],d4[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]
		VST3.8    {d5[0],d6[0],d7[0]},[ip]
        BX		lr
COPY_16B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_16B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
        BX		lr
COPY_16B_:
		VLD1.8    {d0,d1},[r1]
		VST1.8    {d0,d1},[ip]		
        BX		lr
COPY_17B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_17B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRB	r2,[r1]
		STRB	r2,[ip]
        BX		lr
COPY_17B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD1.8		{d2[0]},[r1]
		VST1.8		{d2[0]},[ip]
        BX		lr
COPY_18B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_18B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1]
		STRH	r2,[ip]
        BX		lr
COPY_18B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD2.8		{d0[0],d1[0]},[r1]
		VST2.8		{d0[0],d1[0]},[ip]
        BX		lr
COPY_19B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_19B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
        BX		lr
COPY_19B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]
		VST3.8    {d5[0],d6[0],d7[0]},[ip]
        BX		lr
COPY_20B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_20B_
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
        BX		lr
COPY_20B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]
        BX		lr
COPY_21B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_21B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRB	r4,[r1],#1
		STRB	r4,[ip],#1
		POP		{r4}
        BX		lr
COPY_21B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8		{d3[0]},[r1]!
		VST1.8		{d3[0]},[ip]!
        BX		lr
COPY_22B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_22B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r4,[r1],#2
		STRH	r4,[ip],#2
		POP		{r4}
        BX		lr
COPY_22B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]!
		VST2.8		{d0[0],d1[0]},[ip]!
        BX		lr
COPY_23B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_23B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r4,[r1],#2
		STRH	r4,[ip],#2
		LDRB	r4,[r1],#1
		STRB	r4,[ip],#1
		POP		{r4}
        BX		lr
COPY_23B_:
		VLD1.8    {d0,d1},[r1]!
		VST1.8    {d0,d1},[ip]!		
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]
		VST3.8    {d5[0],d6[0],d7[0]},[ip]
        BX		lr
COPY_24B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_24B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		POP		{r4}
        BX		lr
COPY_24B_:
		VLD1.8    {d0,d1,d2},[r1]
		VST1.8    {d0,d1,d2},[ip]
        BX		lr
COPY_25B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_25B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_25B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD1.8		{d3[0]},[r1]
		VST1.8		{d3[0]},[ip]
        BX		lr
COPY_26B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_26B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_26B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]
		VST2.8		{d0[0],d1[0]},[ip]
        BX		lr
COPY_27B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_27B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_27B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]
		VST3.8    {d5[0],d6[0],d7[0]},[ip]
        BX		lr
COPY_28B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_28B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		POP		{r4}
        BX		lr
COPY_28B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]
        BX		lr
COPY_29B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_29B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_29B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8		{d4[0]},[r1]
		VST1.8		{d4[0]},[ip]
        BX		lr
COPY_30B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_30B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_30B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]
		VST2.8		{d0[0],d1[0]},[ip]
        BX		lr
COPY_31B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_31B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_31B_:
		VLD1.8    {d0,d1,d2},[r1]!
		VST1.8    {d0,d1,d2},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]
		VST3.8    {d5[0],d6[0],d7[0]},[ip]
        BX		lr
COPY_32B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_32B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		POP		{r4}
        BX		lr
COPY_32B_:
		VLD1.8    {d0,d1,d2,d3},[r1]
		VST1.8    {d0,d1,d2,d3},[ip]
        BX		lr
COPY_33B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_33B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_33B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4[0]},[r1]
		VST1.8    {d4[0]},[ip]
        BX		lr
COPY_34B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_34B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_34B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]
		VST2.8		{d0[0],d1[0]},[ip]
        BX		lr
COPY_35B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_35B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_35B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]
		VST3.8    {d5[0],d6[0],d7[0]},[ip]
        BX		lr
COPY_36B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_36B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		POP		{r4}
        BX		lr
COPY_36B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
        BX		lr
COPY_37B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_37B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_37B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8    {d5[0]},[r1]!
		VST1.8    {d5[0]},[ip]!
        BX		lr
COPY_38B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_38B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_38B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD2.8		{d5[0],d6[0]},[r1]!
		VST2.8		{d5[0],d6[0]},[ip]!
		BX		lr
COPY_39B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_39B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_39B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_40B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_40B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		POP		{r4}
        BX		lr
COPY_40B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
        BX		lr
COPY_41B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_41B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_41B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD1.8    {d5[0]},[r1]!
		VST1.8    {d5[0]},[ip]!
        BX		lr
COPY_42B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_42B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_42B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]!
		VST2.8		{d0[0],d1[0]},[ip]!
        BX		lr
COPY_43B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_43B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_43B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_44B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_44B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		POP		{r4}
        BX		lr
COPY_44B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
        BX		lr
COPY_45B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_45B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_45B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8    {d6[0]},[r1]!
		VST1.8    {d6[0]},[ip]!
        BX		lr
COPY_46B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_46B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_46B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD2.8    {d0[0],d1[0]},[r1]!
		VST2.8    {d0[0],d1[0]},[ip]!
        BX		lr
COPY_47B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_47B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_47B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4},[r1]!
		VST1.8    {d4},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_48B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_48B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		POP		{r4}
        BX		lr
COPY_48B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
        BX		lr
COPY_49B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_49B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_49B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD1.8    {d6[0]},[r1]!
		VST1.8    {d6[0]},[ip]!
        BX		lr
COPY_50B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_50B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_50B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]!
		VST2.8		{d0[0],d1[0]},[ip]!
        BX		lr
COPY_51B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_51B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_51B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_52B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_52B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		POP		{r4}
        BX		lr
COPY_52B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
        BX		lr
COPY_53B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_53B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_53B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8    {d7[0]},[r1]!
		VST1.8    {d7[0]},[ip]!
        BX		lr
COPY_54B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_54B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRH	r2,[r1],#1
		STRH	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_54B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]!
		VST2.8		{d0[0],d1[0]},[ip]!
        BX		lr
COPY_55B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_55B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_55B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5},[r1]!
		VST1.8    {d4,d5},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_56B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_56B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		POP		{r4}
        BX		lr
COPY_56B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
        BX		lr
COPY_57B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_57B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_57B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD1.8    {d7[0]},[r1]!
		VST1.8    {d7[0]},[ip]!
        BX		lr
COPY_58B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_58B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_58B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]!
		VST2.8		{d0[0],d1[0]},[ip]!
        BX		lr
COPY_59B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_59B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3}
		STM		ip!,{r2,r3}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_59B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_60B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_60B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		POP		{r4}
        BX		lr
COPY_60B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
        BX		lr
COPY_61B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_61B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_61B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD1.8     {d0[0]},[r1]!
		VST1.8     {d0[0]},[ip]!
        BX		lr
COPY_62B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_62B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		POP		{r4}
        BX		lr
COPY_62B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD2.8		{d0[0],d1[0]},[r1]!
		VST2.8		{d0[0],d1[0]},[ip]!
        BX		lr
COPY_63B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_63B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4}
		LDRH	r2,[r1],#2
		STRH	r2,[ip],#2
		LDRB	r2,[r1],#1
		STRB	r2,[ip],#1
		POP		{r4}
        BX		lr
COPY_63B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VLD1.8    {d4,d5,d6},[r1]!
		VST1.8    {d4,d5,d6},[ip]!
		VLD4.8		{d0[0],d1[0],d2[0],d3[0]},[r1]!
		VST4.8		{d0[0],d1[0],d2[0],d3[0]},[ip]!
		VLD3.8    {d5[0],d6[0],d7[0]},[r1]!
		VST3.8    {d5[0],d6[0],d7[0]},[ip]!
        BX		lr
COPY_64B:
		ORR		r3,ip,r1
		TST		r3,#3
		BNE		COPY_64B_
		PUSH	{r4}
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4} @ 12
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4} @ 24
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4} @ 36
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4} @ 48
		LDM		r1!,{r2,r3,r4}
		STM		ip!,{r2,r3,r4} @ 60
		LDR		r2,[r1],#4
		STR		r2,[ip],#4
		POP		{r4}
        BX		lr
COPY_64B_:
		VLD1.8    {d0,d1,d2,d3},[r1]!
		VLD1.8    {d4,d5,d6,d7},[r1]!
		VST1.8    {d0,d1,d2,d3},[ip]!
		VST1.8    {d4,d5,d6,d7},[ip]!
        BX		lr

MEMCPY_WRAPUP:
        @MOV      r0,r5
        @POP      {r4-r6,pc}
		BX		lr

		.global memcpy_neon

