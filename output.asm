section .data
__malloc_err_len: dq 20
__malloc_err_dat: db `out of memory error`, 0
__str_0_len:
    dq 22
__str_0_dat:
    db `match: no matching arm`, 0

section .bss
    __read_buf: resb 4096
    __print_int_buf: resb 24

section .note.GNU-stack noalloc noexec nowrite progbits
section .text
global _start

extern lang_print_float
extern lang_parse_float
extern lang_parse_int
extern lang_str_eq


__lang_malloc:
    push rbp
    mov rbp, rsp
    add rdi, 7
    and rdi, -8
    mov rsi, rdi
    mov rdi, 0
    mov rdx, 3
    mov r10, 34
    mov r8,  -1
    mov r9,  0
    mov rax, 9
    syscall
    cmp rax, -1
    jne .malloc_ok
    ; ошибка - выводим сообщение и завершаем
    mov rdi, __malloc_err_len
    call __lang_panic
.malloc_ok:
    pop rbp
    ret

__lang_print_int:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13

    mov rax, rdi
    mov r12, __print_int_buf
    add r12, 23
    mov byte [r12], 10
    dec r12
    mov r13, 0

    cmp rax, 0
    jns .pint_pos
    mov r13, 1
    neg rax
.pint_pos:
    mov rbx, 10
.pint_loop:
    xor rdx, rdx
    div rbx
    add dl, '0'
    mov [r12], dl
    dec r12
    cmp rax, 0
    jnz .pint_loop

    cmp r13, 0
    jz .pint_nosign
    mov byte [r12], '-'
    dec r12
.pint_nosign:
    inc r12
    mov rcx, __print_int_buf
    add rcx, 24
    sub rcx, r12
    mov rax, 1
    mov rdi, 1
    mov rsi, r12
    mov rdx, rcx
    syscall

    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

__lang_print_str:
    push rbp
    mov rbp, rsp
    push rbx

    mov rbx, rdi
    mov rdx, [rbx]
    lea rsi, [rbx + 8]
    mov rax, 1
    mov rdi, 1
    syscall

    pop rbx
    pop rbp
    ret

__lang_read_str:
    push rbp
    mov rbp, rsp
    push rbx

    mov rax, 0
    mov rdi, 0
    mov rsi, __read_buf
    mov rdx, 4095
    syscall
    mov rbx, rax

    ; убираем завершающий '\n' если есть
    cmp rbx, 0
    jz .rstr_alloc
    mov rcx, __read_buf
    add rcx, rbx
    dec rcx
    cmp byte [rcx], 10
    jne .rstr_alloc
    dec rbx

.rstr_alloc:
    mov rdi, rbx
    add rdi, 8
    call __lang_malloc
    mov [rax], rbx

    mov rdi, rax
    add rdi, 8
    mov rsi, __read_buf
    mov rcx, rbx
    rep movsb

    pop rbx
    pop rbp
    ret

__lang_panic:
    push rbp
    mov rbp, rsp
    call __lang_print_str
    mov rdi, 1
    mov rax, 60
    syscall

__lang_exit:
    mov rax, 60
    syscall

__fn_solve:
    push rbp
    mov rbp, rsp
    sub rsp, 192
    mov [rbp-8], rdi
    mov [rbp-16], rsi
    mov [rbp-24], rdx
    mov rax, [rbp-8]
    mov [rbp-32], rax
    mov rax, [rbp-32]
    mov rcx, [rax]
    cmp rcx, 0
    jnz .match_next_1
    mov rax, [rbp-16]
    mov [rbp-40], rax
    mov rdi, [rbp-40]
    call __lang_print_int
    mov [rbp-48], rax
    mov rax, [rbp-24]
    mov [rbp-56], rax
    mov rdi, [rbp-56]
    call __lang_print_int
    mov [rbp-64], rax
    mov rax, 0
    jmp .match_end_0
.match_next_1:
    mov rax, [rbp-32]
    mov rcx, [rax]
    cmp rcx, 0
    jz .match_next_2
    mov [rbp-72], rax
    mov rax, [rbp-72]
    mov rcx, [rax + 8]
    mov rax, rcx
    mov [rbp-80], rax
    mov rax, [rbp-72]
    mov rcx, [rax + 16]
    mov rax, rcx
    mov [rbp-88], rax
    ; genBinary isFloat=true
    mov rax, [rbp-80]
    mov [rbp-96], rax
    mov rax, 0 ; float64 0.000000
    movq xmm1, rax
    mov rcx, rax
    mov rax, [rbp-96]
    movq xmm0, rax
    ucomisd xmm0, xmm1
    setb al
    movzx rax, al
    cmp rax, 0
    jz .else_3
    mov rax, [rbp-88]
    mov [rbp-104], rax
    ; genBinary isFloat=false
    mov rax, [rbp-16]
    mov [rbp-112], rax
    mov rax, 1
    mov rcx, rax
    mov rax, [rbp-112]
    add rax, rcx
    mov [rbp-120], rax
    mov rax, [rbp-24]
    mov [rbp-128], rax
    mov rdi, [rbp-104]
    mov rsi, [rbp-120]
    mov rdx, [rbp-128]
    call __fn_solve
    jmp .endif_4
.else_3:
    ; genBinary isFloat=true
    mov rax, [rbp-80]
    mov [rbp-136], rax
    mov rax, 0 ; float64 0.000000
    movq xmm1, rax
    mov rcx, rax
    mov rax, [rbp-136]
    movq xmm0, rax
    ucomisd xmm0, xmm1
    seta al
    movzx rax, al
    cmp rax, 0
    jz .else_5
    mov rax, [rbp-88]
    mov [rbp-144], rax
    mov rax, [rbp-16]
    mov [rbp-152], rax
    ; genBinary isFloat=false
    mov rax, [rbp-24]
    mov [rbp-160], rax
    mov rax, 1
    mov rcx, rax
    mov rax, [rbp-160]
    add rax, rcx
    mov [rbp-168], rax
    mov rdi, [rbp-144]
    mov rsi, [rbp-152]
    mov rdx, [rbp-168]
    call __fn_solve
    jmp .endif_6
.else_5:
    mov rax, [rbp-88]
    mov [rbp-176], rax
    mov rax, [rbp-16]
    mov [rbp-184], rax
    mov rax, [rbp-24]
    mov [rbp-192], rax
    mov rdi, [rbp-176]
    mov rsi, [rbp-184]
    mov rdx, [rbp-192]
    call __fn_solve
.endif_6:
.endif_4:
    jmp .match_end_0
.match_next_2:
    mov rdi, __str_0_len
    call __lang_panic
.match_end_0:
    mov rsp, rbp
    pop rbp
    ret

__fn_main:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov rdi, 8
    call __lang_malloc
    mov qword [rax], 0
    mov [rbp-8], rax
    mov rax, 4660298390377805815 ; float64 3674.347365
    movq xmm0, rax
    mov rcx, 0x8000000000000000
    movq xmm1, rcx
    xorpd xmm0, xmm1
    movq rax, xmm0
    mov [rbp-16], rax
    mov rdi, 24
    call __lang_malloc
    mov qword [rax], 1
    mov rcx, [rbp-16]
    mov [rax + 8], rcx
    mov rcx, [rbp-8]
    mov [rax + 16], rcx
    mov [rbp-8], rax
    mov rax, 4664141052972395135 ; float64 6747.576000
    mov [rbp-24], rax
    mov rdi, 24
    call __lang_malloc
    mov qword [rax], 1
    mov rcx, [rbp-24]
    mov [rax + 8], rcx
    mov rcx, [rbp-8]
    mov [rax + 16], rcx
    mov [rbp-8], rax
    mov rax, 4629891017717349509 ; float64 33.354300
    mov [rbp-32], rax
    mov rdi, 24
    call __lang_malloc
    mov qword [rax], 1
    mov rcx, [rbp-32]
    mov [rax + 8], rcx
    mov rcx, [rbp-8]
    mov [rax + 16], rcx
    mov [rbp-8], rax
    mov rax, 4643122037509485022 ; float64 253.465400
    movq xmm0, rax
    mov rcx, 0x8000000000000000
    movq xmm1, rcx
    xorpd xmm0, xmm1
    movq rax, xmm0
    mov [rbp-40], rax
    mov rdi, 24
    call __lang_malloc
    mov qword [rax], 1
    mov rcx, [rbp-40]
    mov [rax + 8], rcx
    mov rcx, [rbp-8]
    mov [rax + 16], rcx
    mov [rbp-8], rax
    mov rax, 4675091465168954065 ; float64 35351.343000
    mov [rbp-48], rax
    mov rdi, 24
    call __lang_malloc
    mov qword [rax], 1
    mov rcx, [rbp-48]
    mov [rax + 8], rcx
    mov rcx, [rbp-8]
    mov [rax + 16], rcx
    mov [rbp-8], rax
    mov rax, [rbp-8]
    mov [rbp-56], rax
    mov rax, 0
    mov [rbp-64], rax
    mov rax, 0
    mov [rbp-72], rax
    mov rdi, [rbp-56]
    mov rsi, [rbp-64]
    mov rdx, [rbp-72]
    call __fn_solve
    mov [rbp-80], rax
    mov rax, 0
    mov rsp, rbp
    pop rbp
    ret

_start:
    and rsp, -16
    call __fn_main
    mov rdi, rax 
    mov rax, 60
    syscall

