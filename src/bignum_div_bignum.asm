; =============================================================================
; @file    bignum_div_bignum.asm
; @author  git@bayborodov.com
; @version 1.0.2
; @date    08.07.2026
;
; @brief   Делит большое беззнаковое число `numer` на `denom`.
; @details
;   Реализует функцию bignum_div_bignum, совместимую с System V AMD64 ABI.
;   Внутренний цикл работает от старшего к младшему слову `numer`,
;   используя побитовый сдвиг и вычитание.
; @history
;   - rev. 0 (07.04.2026): Первоначальная реализация на ассемблере.
;   - rev. 1 (07.07.2026): Повторная реализация на ассемблере.
;   - rev. 2 (08.07.2026): Исправлены ошибки с флагом переноса (CF) в циклах сдвига и вычитания.
; =============================================================================

section .text
global bignum_div_bignum
%define BIGNUM_CAPACITY        32
%define BIGNUM_WORD_SIZE       8
%define BIGNUM_LEN_OFFSET      (BIGNUM_CAPACITY * BIGNUM_WORD_SIZE)
%define BIGNUM_T_SIZE_ALIGNED  (BIGNUM_LEN_OFFSET + BIGNUM_WORD_SIZE)

; ----- коды возврата ---------------------------------------------------------
%define BIGNUM_DIV_BIGNUM_OK                 0
%define BIGNUM_DIV_BIGNUM_ERR_NULL_PTR      -1
%define BIGNUM_DIV_BIGNUM_ERR_DIV_BY_ZERO   -2
%define BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP-3
%define BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH    -4

; Макрос для проверки перекрытия двух буферов
%macro CHECK_OVERLAP 2
    mov     rax, %1
    lea     r8, [%2 + BIGNUM_T_SIZE_ALIGNED]
    cmp     rax, r8
    jae     %%no_overlap
    mov     rax, %2
    lea     r8, [%1 + BIGNUM_T_SIZE_ALIGNED]
    cmp     rax, r8
    jb      .err_buf_overlap
%%no_overlap:
%endmacro

section .text
global bignum_div_bignum
bignum_div_bignum:
    ; ---------- пролог ----------
    push    r12
    push    r13
    push    r14
    push    r15
    push    rbx

    ; ---------- сохраняем аргументы ----------
    ; System V AMD64 ABI: rdi = numer, rsi = denom, rdx = quot, rcx = rem
    mov     r14, rdi        ; numer
    mov     r15, rsi        ; denom
    mov     r12, rdx        ; quot
    mov     r13, rcx        ; rem

    ; ---------- проверка NULL ----------
    test    r14, r14
    jz      .err_null_ptr
    test    r15, r15
    jz      .err_null_ptr
    test    r12, r12
    jz      .err_null_ptr
    test    r13, r13
    jz      .err_null_ptr

    ; ---------- проверка длины ----------
    mov     rax, [r14 + BIGNUM_LEN_OFFSET]
    cmp     rax, BIGNUM_CAPACITY
    ja      .err_bad_length
    mov     rax, [r15 + BIGNUM_LEN_OFFSET]
    cmp     rax, BIGNUM_CAPACITY
    ja      .err_bad_length

    ; ---------- проверка перекрытия буферов ----------
    CHECK_OVERLAP r12, r14  ; quot ↔ numer
    CHECK_OVERLAP r12, r15  ; quot ↔ denom
    CHECK_OVERLAP r13, r14  ; rem ↔ numer
    CHECK_OVERLAP r13, r15  ; rem ↔ denom
    CHECK_OVERLAP r12, r13  ; quot ↔ rem

    ; ---------- инициализация Q и R нулями ----------
    mov     rcx, 33
    xor     rax, rax
    mov     rdi, r12
    rep stosq

    mov     rcx, 33
    xor     rax, rax
    mov     rdi, r13
    rep stosq

    ; ---------- нормализация D ----------
    mov     r10, [r15 + BIGNUM_LEN_OFFSET]
.norm_D:
    test    r10, r10
    jz      .err_div_by_zero
    cmp     qword [r15 + r10*8 - 8], 0
    jnz     .D_normalized
    dec     r10
    jmp     .norm_D
.D_normalized:

    ; ---------- нормализация N ----------
    mov     r9, [r14 + BIGNUM_LEN_OFFSET]
.norm_N:
    test    r9, r9
    jz      .N_is_zero
    cmp     qword [r14 + r9*8 - 8], 0
    jnz     .N_normalized
    dec     r9
    jmp     .norm_N
.N_normalized:

    ; ---------- вычисление старшего бита N ----------
    mov     r8, [r14 + r9*8 - 8]
    bsr     rcx, r8
    lea     rbx, [r9 - 1]
    shl     rbx, 6
    add     rbx, rcx

    ; ---------- основной цикл побитового деления ----------
.div_loop:
    ; Сдвиг R влево на 1 бит
    clc
    mov     rcx, 32
    lea     rdi, [r13]
.shift_R_loop:
    rcl     qword [rdi], 1
    lea     rdi, [rdi + 8]      ; ИСПОЛЬЗУЕМ lea, чтобы не затереть CF!
    dec     rcx                 ; dec не изменяет CF
    jnz     .shift_R_loop
    setc    r9b                 ; ИСПРАВЛЕНИЕ: Сохраняем CF (2049-й бит R) в r9b

    ; Перенос i-го бита N в младший бит R
    mov     rax, rbx
    shr     rax, 6
    mov     rcx, rbx
    and     rcx, 63
    mov     r8, [r14 + rax*8]
    bt      r8, rcx             ; bt перезапишет CF, но мы его уже сохранили!
    jnc     .bit_zero
    bts     qword [r13], 0
.bit_zero:
    test    r9b, r9b            ; ИСПРАВЛЕНИЕ: Проверяем сохраненный 2049-й бит
    jnz     .R_greater          ; Если он равен 1, то R гарантированно больше D!

    ; Сравнение R и D
    mov     rcx, 31
.cmp_check_R_high:
    cmp     rcx, r10
    jl      .cmp_words
    cmp     qword [r13 + rcx*8], 0
    ja      .R_greater
    dec     rcx
    jmp     .cmp_check_R_high

.cmp_words:
    ; rcx = r10 - 1
.cmp_words_loop:
    test    rcx, rcx
    js      .R_equal
    mov     rax, [r13 + rcx*8]
    cmp     rax, [r15 + rcx*8]
    ja      .R_greater
    jb      .R_less
    dec     rcx
    jmp     .cmp_words_loop

.R_equal:
.R_greater:
    ; R >= D, выполняем R = R - D
    mov     r11, 32
    sub     r11, r10            ; r11 = количество оставшихся слов для распространения заема
    mov     rcx, r10
    xor     r8, r8
    clc
.sub_loop:
    mov     rax, [r15 + r8*8]
    sbb     [r13 + r8*8], rax
    lea     r8, [r8 + 1]        ; lea не изменяет CF
    dec     rcx                 ; dec не изменяет CF
    jnz     .sub_loop

    jnc     .sub_done           ; Если нет заема, выходим
    mov     rcx, r11
    jrcxz   .sub_done           ; Если r11 == 0, выходим (jrcxz не изменяет флаги)
.sub_prop_loop:
    sbb     qword [r13 + r8*8], 0
    lea     r8, [r8 + 1]
    dec     rcx
    jz      .sub_done
    jc      .sub_prop_loop      ; Продолжаем, пока есть заем
.sub_done:

    ; Устанавливаем i-й бит в Q
    mov     rax, rbx
    shr     rax, 6
    mov     rcx, rbx
    and     rcx, 63
    bts     qword [r12 + rax*8], rcx

.R_less:
    dec     rbx
    jns     .div_loop

    ; ---------- установка длины Q ----------
    mov     rcx, 32
.find_Q_len:
    test    rcx, rcx
    jz      .Q_len_found
    cmp     qword [r12 + rcx*8 - 8], 0
    jnz     .Q_len_found
    dec     rcx
    jmp     .find_Q_len
.Q_len_found:
    mov     [r12 + BIGNUM_LEN_OFFSET], rcx

    ; ---------- установка длины R ----------
    mov     rcx, 32
.find_R_len:
    test    rcx, rcx
    jz      .R_len_found
    cmp     qword [r13 + rcx*8 - 8], 0
    jnz     .R_len_found
    dec     rcx
    jmp     .find_R_len
.R_len_found:
    mov     [r13 + BIGNUM_LEN_OFFSET], rcx

    ; ---------- успешное завершение ----------
    mov     eax, BIGNUM_DIV_BIGNUM_OK
    jmp     .exit

.N_is_zero:
    mov     eax, BIGNUM_DIV_BIGNUM_OK
    jmp     .exit

    ; ---------- обработчики ошибок ----------
.err_null_ptr:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_NULL_PTR
    jmp     .exit

.err_div_by_zero:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_DIV_BY_ZERO
    jmp     .exit

.err_buf_overlap:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP
    jmp     .exit

.err_bad_length:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH
    jmp     .exit

    ; ---------- эпилог ----------
.exit:
    pop     rbx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    ret
