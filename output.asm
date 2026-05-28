section .data
__malloc_err_len: dq 20
__malloc_err_dat: db `out of memory error`, 0
__str_0_len:
    dq 11
__str_0_dat:
    db `palindrome`, 10, ``, 0
__str_1_len:
    dq 15
__str_1_dat:
    db `not palindrome`, 10, ``, 0

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

__fn_isPalindrome:
    push rbp
    mov rbp, rsp
    sub rsp, 80
    mov [rbp-8], rdi
    mov rax, [rbp-8]
    mov [rbp-16], rax
    mov rax, 100
    mov rcx, rax
    mov rax, [rbp-16]
    cqo
    idiv rcx
    mov [rbp-24], rax
    mov rax, [rbp-8]
    mov [rbp-32], rax
    mov rax, 10
    mov rcx, rax
    mov rax, [rbp-32]
    cqo
    idiv rcx
    mov [rbp-40], rax
    mov rax, 10
    mov rcx, rax
    mov rax, [rbp-40]
    cqo
    idiv rcx
    mov rax, rdx
    mov [rbp-48], rax
    mov rax, [rbp-8]
    mov [rbp-56], rax
    mov rax, 10
    mov rcx, rax
    mov rax, [rbp-56]
    cqo
    idiv rcx
    mov rax, rdx
    mov [rbp-64], rax
    mov rax, [rbp-24]
    mov [rbp-72], rax
    mov rax, [rbp-64]
    mov rcx, rax
    mov rax, [rbp-72]
    cmp rax, rcx
    sete al
    movzx rax, al
    mov rsp, rbp
    pop rbp
    ret

__fn_main:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rax, 122
    mov [rbp-8], rax
    mov rdi, [rbp-8]
    call __fn_isPalindrome
    cmp rax, 0
    jz .else_0
    mov rax, __str_0_len
    jmp .endif_1
.else_0:
    mov rax, __str_1_len
.endif_1:
    mov [rbp-16], rax
    mov rdi, [rbp-16]
    call __lang_print_str
    mov [rbp-24], rax
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

