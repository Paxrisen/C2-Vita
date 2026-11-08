    .text
    .globl call_guest, landing_host, stub_lo_start, stub_lo_end, sig_entry
    .globl h_rsp, h_fs, h_land, h_regs, h_far
# void call_guest(uint32_t* regs, uint32_t target)
call_guest:
    push %rbx; push %rbp; push %r12; push %r13; push %r14; push %r15
    mov %rsp, h_rsp(%rip)
    mov %esi, h_far(%rip)
    movw $0x23, h_far+4(%rip)
    mov %rdi, h_regs(%rip)
    mov $0x2b, %eax
    mov %eax, %fs
    mov %eax, %ds
    mov %eax, %es
    mov $0x100000, %eax
    wrfsbase %rax
    mov 16(%rdi), %esp
    mov 0(%rdi), %eax
    mov 4(%rdi), %ecx
    mov 8(%rdi), %edx
    mov 12(%rdi), %ebx
    mov 20(%rdi), %ebp
    mov 24(%rdi), %esi
    mov 28(%rdi), %edi
    cld
    ljmpl *h_far(%rip)
landing_host:
    mov h_regs(%rip), %rdi
    mov 0x202000, %eax; mov %eax, 0(%rdi)
    mov 0x202004, %eax; mov %eax, 4(%rdi)
    mov 0x202008, %eax; mov %eax, 8(%rdi)
    mov 0x20200c, %eax; mov %eax, 12(%rdi)
    mov 0x202010, %eax; mov %eax, 16(%rdi)
    mov 0x202014, %eax; mov %eax, 20(%rdi)
    mov 0x202018, %eax; mov %eax, 24(%rdi)
    mov 0x20201c, %eax; mov %eax, 28(%rdi)
    cld
    pop %r15; pop %r14; pop %r13; pop %r12; pop %rbp; pop %rbx
    ret
sig_entry:
    xor %eax, %eax
    mov %eax, %fs
    mov h_fs(%rip), %rax
    wrfsbase %rax
    jmp sig_handler_c
    .data
h_rsp: .quad 0
h_fs: .quad 0
h_land: .quad 0
h_regs: .quad 0
h_far: .long 0
       .word 0
    .text
    .code32
stub_lo_start:
    mov %eax, 0x202000
    mov %ecx, 0x202004
    mov %edx, 0x202008
    mov %ebx, 0x20200c
    mov %esp, 0x202010
    mov %ebp, 0x202014
    mov %esi, 0x202018
    mov %edi, 0x20201c
    ljmp $0x33, $0x200300
stub_lo_end:
    .code64
    .section .note.GNU-stack,"",@progbits
