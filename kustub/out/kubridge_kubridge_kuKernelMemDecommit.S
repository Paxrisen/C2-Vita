.arch armv7a

.section .vitalink.fstubs.kubridge,"ax",%progbits

	.align 4
	.global kuKernelMemDecommit
	.type kuKernelMemDecommit, %function
kuKernelMemDecommit:
.if GEN_WEAK_EXPORTS
	.word 0x00010008
.else
	.word 0x00010000
.endif //GEN_WEAK_EXPORTS
	.word 0x6DB9389B
	.word 0x9BDF5881
	.align 4

