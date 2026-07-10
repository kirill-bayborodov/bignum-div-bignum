; =============================================================================
; @file    bignum_div_bignum.asm
; @author  git@bayborodov.com
; @version 1.0.11
; @date    10.07.2026
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
;   - rev. 3 (09.07.2026): Избавление от хардкода констант (использование макросов).
;   - rev. 4 (09.07.2026): Оптимизация циклов сдвига и вычитания с использованием
;   динамической длины (r10 + 1) вместо BIGNUM_CAPACITY.
;   - rev. 5 (09.07.2026): Оптимизация узких мест (избавление от RMW операций с памятью).
;   - rev. 6 (09.07.2026): Удалены дорогие инструкции pushfq/popfq, добавлен Loop Unrolling 
;   для цикла сравнения cmp_words_loop.
;   - rev. 7 (09.07.2026): Откат пессимизации Loop Unrolling в цикле сравнения.
;   - rev. 8 (09.07.2026): Возврат эффективных RMW-инструкций (rcl/sbb по памяти) в горячие 
;             циклы с сохранением архитектурных оптимизаций из rev6-7.
;   - rev. 9 (10.07.2026): Попытка реализации пословного деления (Knuth Algorithm D) 
;   - rev. 10 (10.07.2026): Успешная реализация пословного деления (Knuth Algorithm D)
;   - rev. 11 (10.07.2026): Микрооптимизации: инлайнинг check_overlap, использование 
;             shld/shrd для сдвигов, вынос вычисления адресов из горячих циклов.
;   - rev. 12 (10.07.2026): Откат медленных shld/shrd, Loop Unrolling для mul_sub_loop,
;              прямые RMW операции с памятью (sub [mem], reg).
;   - rev. 13 (10.07.2026): Откат RMW к явным mov -> sub -> mov для разгрузки Store Buffers в MT.
; =============================================================================

%define BIGNUM_CAPACITY        32
%define BIGNUM_WORD_SIZE       8
%define BIGNUM_WORDS_OFFSET    0
%define BIGNUM_LEN_OFFSET      (BIGNUM_CAPACITY * BIGNUM_WORD_SIZE)
%define BIGNUM_T_SIZE_ALIGNED  (BIGNUM_LEN_OFFSET + BIGNUM_WORD_SIZE)

; ----- коды возврата ---------------------------------------------------------
%define BIGNUM_DIV_BIGNUM_OK                  0
%define BIGNUM_DIV_BIGNUM_ERR_NULL_PTR       -1
%define BIGNUM_DIV_BIGNUM_ERR_DIV_BY_ZERO    -2
%define BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP -3
%define BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH     -4

; ----- макросы ---------------------------------------------------------------
%macro CHECK_OVERLAP 2
    lea     rcx, [%1 + rdx]
    lea     r8,  [%2 + rdx]
    cmp     %1, r8
    jae     %%no_overlap
    cmp     %2, rcx
    jae     %%no_overlap
    jmp     .err_overlap
%%no_overlap:
%endmacro

section .text
global bignum_div_bignum

bignum_div_bignum:
    ; ----- prologue -------------------------------------------------
    push    rbp
    mov     rbp, rsp
    push    rbx                 ; Save callee-saved register rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 8*(3*BIGNUM_CAPACITY + 1)   ; u, v, q buffers

    ; keep useful pointers in callee‑saved regs
    mov     r12, rdx            ; quotient *
    mov     r13, rcx            ; remainder *
    mov     r14, rdi            ; dividend *
    mov     r15, rsi            ; divisor  *

    ; ----- argument sanity checks ----------------------------------
    test    r14, r14
    jz      .err_null_ptr
    test    r15, r15
    jz      .err_null_ptr
    test    r12, r12
    jz      .err_null_ptr
    test    r13, r13
    jz      .err_null_ptr

    mov     rax, [r14 + BIGNUM_LEN_OFFSET]
    mov     rbx, [r15 + BIGNUM_LEN_OFFSET]
    cmp     eax, BIGNUM_CAPACITY
    ja      .err_bad_len
    cmp     ebx, BIGNUM_CAPACITY
    ja      .err_bad_len

    ; ----- overlap checks (quotient‑dividend‑divisor‑remainder) ----
    ; size = sizeof(bignum_t) = BIGNUM_CAPACITY*8 + 8
    mov     edx, BIGNUM_CAPACITY*8 + 8

    CHECK_OVERLAP r12, r14
    CHECK_OVERLAP r12, r15
    CHECK_OVERLAP r13, r14
    CHECK_OVERLAP r13, r15
    CHECK_OVERLAP r12, r13

    ; ----- compute real lengths (skip leading zero words) ---------
    mov     rcx, [r14 + BIGNUM_LEN_OFFSET]   ; m
    mov     r10d, ecx
.strip_dividend:
    test    r10d, r10d
    jz      .strip_divisor
    mov     rax, [r14 + BIGNUM_WORDS_OFFSET + r10*8 - 8]
    test    rax, rax
    jne     .strip_divisor
    dec     r10d
    jmp     .strip_dividend

.strip_divisor:
    mov     rcx, [r15 + BIGNUM_LEN_OFFSET]   ; n
    mov     r11d, ecx
.strip_divisor_loop:
    test    r11d, r11d
    jz      .div_by_zero
    mov     rax, [r15 + BIGNUM_WORDS_OFFSET + r11*8 - 8]
    test    rax, rax
    jne     .len_ready
    dec     r11d
    jmp     .strip_divisor_loop

.len_ready:
    ; ----- dividend < divisor fast path ----------------------------
    cmp     r10d, r11d
    jb      .copy_as_remainder
    ja      .maybe_multi_word

    ; m == n – compare most significant word downwards
    mov     rcx, r10
    dec     ecx
.cmp_loop:
    mov     rax, [r14 + BIGNUM_WORDS_OFFSET + rcx*8]
    mov     rbx, [r15 + BIGNUM_WORDS_OFFSET + rcx*8]
    cmp     rax, rbx
    ja      .maybe_multi_word
    jb      .copy_as_remainder
    dec     rcx
    jns     .cmp_loop
    ; equal → fall through to generic path (quotient will become 1)
.maybe_multi_word:

    ; ----- single‑word divisor fast path ---------------------------
    cmp     r11d, 1
    jne     .multi_word_divisor

    ; divisor word in r8
    mov     r8, [r15 + BIGNUM_WORDS_OFFSET]
    xor     r9, r9                ; remainder word
    mov     rcx, r10
    dec     ecx                   ; i = m‑1 .. 0
.single_word_loop:
    mov     rax, [r14 + BIGNUM_WORDS_OFFSET + rcx*8]
    mov     rdx, r9               ; RDX = старшая часть (предыдущий остаток)
    div     r8                    ; RDX:RAX / r8 -> RAX = q_i, RDX = новый остаток
    mov     [r12 + BIGNUM_WORDS_OFFSET + rcx*8], rax
    mov     r9, rdx
    dec     rcx
    jns     .single_word_loop

    ; store remainder word
    mov     [r13 + BIGNUM_WORDS_OFFSET], r9
    ; set lengths (strip leading zeros)
    ; quotient length
    mov     rcx, r10
    mov     rdx, rcx
.q_len_trim:
    test    rdx, rdx
    jz      .q_len_done
    dec     rdx
    mov     rax, [r12 + BIGNUM_WORDS_OFFSET + rdx*8]
    test    rax, rax
    jnz     .q_len_set
    jmp     .q_len_trim
.q_len_set:
    inc     rdx
.q_len_done:
    mov     [r12 + BIGNUM_LEN_OFFSET], rdx
    ; remainder length (0 or 1)
    test    r9, r9
    jz      .r_len_zero
    mov     qword [r13 + BIGNUM_LEN_OFFSET], 1
    jmp     .zero_tails
.r_len_zero:
    mov     qword [r13 + BIGNUM_LEN_OFFSET], 0
    jmp     .zero_tails

    ; ----- copy dividend → remainder, quotient = 0 -----------------
.copy_as_remainder:
    ; zero quotient
    xor     rax, rax
    mov     [r12 + BIGNUM_LEN_OFFSET], rax
    ; copy dividend words
    mov     rcx, r10
    xor     rdx, rdx
.copy_rem_loop:
    mov     rax, [r14 + BIGNUM_WORDS_OFFSET + rdx*8]
    mov     [r13 + BIGNUM_WORDS_OFFSET + rdx*8], rax
    inc     rdx
    cmp     rdx, rcx
    jb      .copy_rem_loop
    mov     rcx, r10
    mov     [r13 + BIGNUM_LEN_OFFSET], rcx
    jmp     .zero_tails
    ; ------------------------------------------------------------
    ;  Multi‑word divisor → full Knuth D
    ; ------------------------------------------------------------
.multi_word_divisor:
    ; ---- allocate temporary buffers (already on stack) ------------
    ; u = rsp                (BIGNUM_CAPACITY+1 words)
    ; v = rsp + (BIGNUM_CAPACITY+1)*8
    ; q = v + BIGNUM_CAPACITY*8
    lea     rax, [rsp]                                   ; u base
    lea     rbx, [rsp + (BIGNUM_CAPACITY+1)*8]           ; v base
    lea     rcx, [rbx + BIGNUM_CAPACITY*8]               ; q base

    ; ---- D1 – normalization ---------------------------------------
    ; shift = leading zeros of highest divisor word
    mov     rdx, [r15 + BIGNUM_WORDS_OFFSET + (r11-1)*8]
    bsr     rdx, rdx                ; index of most‑significant 1‑bit
    mov     r9, 63
    sub     r9, rdx                 ; shift = 63‑msb
    mov     r8d, r9d                ; keep shift in r8d

    test    r8d, r8d
    jz      .norm_skip

    ; ---- normalize divisor into v[0..n‑1] -------------------------
    xor     rdx, rdx
    mov     rsi, r15                ; v base
    mov     rdi, r15                ; divisor base (still in r15)
    mov     r9d, r11d
    dec     r9d                     ; i = n‑1 .. 1
.norm_div_loop:
    mov     rax, [r15 + BIGNUM_WORDS_OFFSET + r9*8]
    mov     ecx, r8d
    shl     rax, cl
    mov     rdx, [r15 + BIGNUM_WORDS_OFFSET + r9*8 - 8]
    mov     ecx, 64
    sub     ecx, r8d
    shr     rdx, cl
    or      rax, rdx
    mov     [rbx + r9*8], rax
    dec     r9d
    jg      .norm_div_loop

    ; v[0]
    mov     rax, [r15 + BIGNUM_WORDS_OFFSET]
    mov     ecx, r8d
    shl     rax, cl
    mov     [rbx], rax

    ; ---- normalize dividend into u[0..m] (extra word) ------------
    xor     rdx, rdx                ; orig_u[-1] = 0
    xor     r9d, r9d                ; i = 0
.norm_divid_loop:
    mov     rax, [r14 + BIGNUM_WORDS_OFFSET + r9*8]
    mov     rdi, rax                ; save orig_u[i]
    mov     ecx, r8d
    shl     rax, cl
    or      rax, rdx
    mov     [rsp + r9*8], rax
    mov     rdx, rdi    
        mov     ecx, 64
        sub     ecx, r8d
        shr     rdx, cl
        inc     r9d
        cmp     r9d, r10d
        jl      .norm_divid_loop
    ; final carry word
    mov     [rsp + r10*8], rdx
    mov     r15d, r8d               ; save shift to r15d

    jmp     .knuth_main

.norm_skip:
    ; ---- copy divisor → v -----------------------------------------
    mov     ecx, r11d
    xor     rdx, rdx
.copy_v_loop:
    mov     rax, [r15 + BIGNUM_WORDS_OFFSET + rdx*8]
    mov     [rbx + rdx*8], rax
    inc     rdx
    cmp     rdx, rcx
    jl      .copy_v_loop
    ; copy dividend → u, extra zero word
    mov     rcx, r10
    xor     rdx, rdx
.copy_u_loop:
    mov     rax, [r14 + BIGNUM_WORDS_OFFSET + rdx*8]
    mov     [rsp + rdx*8], rax
    inc     rdx
    cmp     rdx, rcx
    jl      .copy_u_loop
    mov     qword [rsp + r10*8], 0
    mov     r15d, r8d               ; save shift to r15d

    ; ------------------------------------------------------------
.knuth_main:
    mov     [r12 + BIGNUM_LEN_OFFSET], r10  ; SAVE m
    ; j = m‑n … 0
    mov     esi, r10d
    sub     esi, r11d               ; esi = m‑n
.main_loop_j:
    ; ---- D3: trial quotient ------------------------------------
    lea     rdi, [rsi + r11]                    ; rdi = j + n
    mov     rdx, [rsp + rdi*8]                  ; u[j+n] (high)
    mov     rax, [rsp + rdi*8 - 8]              ; u[j+n‑1] (low)
    mov     rcx, [rbx + (r11-1)*8]              ; v[n‑1]

    ; division
    cmp     rdx, rcx
    jne     .qhat_normal
    mov     r8, -1                               ; q̂ = UINT64_MAX
    mov     r9, rax                              ; r̂ = u[j+n‑1]
    add     r9, rcx                              ; r̂ += v_top
    jb      .multiply_subtract
    jmp     .qhat_done
.qhat_normal:
    div     rcx                                  ; RDX:RAX / rcx → RAX = q̂, RDX = r̂
    mov     r8, rax
    mov     r9, rdx
.qhat_done:

    ; ---- correction loop ----------------------------------------
.corr_loop:
    mov     rax, [rbx + (r11-2)*8]    ; v[n‑2]
    mul     r8                        ; RDX:RAX = q̂ * v[n‑2]
    ; Сравниваем 128-битные числа: RDX:RAX и r9:u[j+n-2]
    cmp     rdx, r9
    ja      .corr_decrement
    jb      .after_corr
    mov     rcx, [rsp + rdi*8 - 16]
    cmp     rax, rcx
    ja      .corr_decrement
    jmp     .after_corr
.corr_decrement:
    dec     r8
    add     r9, [rbx + (r11-1)*8]     ; r̂ += v_top
    jnc     .corr_loop
.after_corr:

    ; ---- D4: u[j..j+n] -= q̂ * v[0..n‑1] (UNROLLED x2) ---------
.multiply_subtract:
    lea     rdi, [rsp + rsi*8]      ; rdi = базовый адрес u[j]
    xor     r9, r9                  ; borrow = 0
    xor     rcx, rcx                ; i = 0
    
    mov     r14d, r11d
    shr     r14d, 1                 ; r14d = n / 2 (количество пар)
    jz      .mul_sub_odd_check      ; если n < 2, пропускаем развернутый цикл

.mul_sub_unrolled_loop:
    ; --- Итерация 1 (четная) ---
    mov     rax, [rbx + rcx*8]      ; v[i]
    mul     r8                      ; RDX:RAX = q̂ * v[i]
    add     rax, r9                 ; добавляем borrow
    adc     rdx, 0                  ; carry в старшую часть
    mov     r10, [rdi + rcx*8]
    sub     r10, rax
    mov     [rdi + rcx*8], r10
    adc     rdx, 0                  ; сохраняем новый borrow
    mov     r9, rdx                 ; borrow = rdx

    ; --- Итерация 2 (нечетная) ---
    mov     rax, [rbx + rcx*8 + 8]  ; v[i+1]
    mul     r8                      ; RDX:RAX = q̂ * v[i+1]
    add     rax, r9                 ; добавляем borrow
    adc     rdx, 0                  ; carry в старшую часть
    mov     r10, [rdi + rcx*8 + 8]
    sub     r10, rax
    mov     [rdi + rcx*8 + 8], r10
    adc     rdx, 0                  ; сохраняем новый borrow
    mov     r9, rdx                 ; borrow = rdx

    add     rcx, 2
    dec     r14d
    jnz     .mul_sub_unrolled_loop

.mul_sub_odd_check:
    test    r11d, 1                 ; проверяем, остался ли нечетный элемент
    jz      .mul_sub_done

    ; --- Последняя нечетная итерация ---
    mov     rax, [rbx + rcx*8]
    mul     r8
    add     rax, r9
    adc     rdx, 0
    mov     r10, [rdi + rcx*8]
    sub     r10, rax
    mov     [rdi + rcx*8], r10
    adc     rdx, 0
    mov     r9, rdx

.mul_sub_done:
    ; subtract top word u[j+n] - borrow
    sub     [rdi + r11*8], r9

    ; ---- D5/D6: if borrow (b_top) then correct ------------------
    jnc     .no_correction
    dec     r8                      ; q̂--
    ; add back v[0..n‑1] to u[j..j+n‑1]
    xor     r9, r9                  ; carry = 0
    xor     rcx, rcx                ; i = 0
.add_back_loop:
    mov     rax, [rdi + rcx*8]      ; u[j+i]
    add     rax, r9                 ; добавляем предыдущий carry
    mov     r9, 0
    adc     r9, 0                   ; r9 = carry от первого сложения
    add     rax, [rbx + rcx*8]      ; прибавляем v[i]
    adc     r9, 0                   ; r9 += carry от второго сложения
    mov     [rdi + rcx*8], rax
    inc     rcx
    cmp     ecx, r11d
    jl      .add_back_loop

    ; final carry to top word
    add     [rdi + r11*8], r9
.no_correction:

    ; store trial quotient word
    lea     rdi, [rsp + (2*BIGNUM_CAPACITY+1)*8]
    mov     [rdi + rsi*8], r8        ; q[j] = q̂

    ; next j
    dec     esi
    jns     .main_loop_j

    ; ------------------------------------------------------------
    ; D8 – denormalise remainder (right shift by 'shift')
    ; ------------------------------------------------------------
    test    r15d, r15d
    jz      .store_results
    ; shift right across words
    xor     r9d, r9d
    mov     r14d, r11d
    dec     r14d                    ; r14d = n - 1
    jz      .dn_shift_last
.dn_shift_loop:
    mov     rax, [rsp + r9*8]       ; u[i]
    mov     rdx, [rsp + r9*8 + 8]   ; u[i+1]
    mov     ecx, r15d
    shr     rax, cl                 ; ОТКАТ: используем shr/shl/or вместо shrd
    mov     ecx, 64
    sub     ecx, r15d
    shl     rdx, cl
    or      rax, rdx
    mov     [r13 + BIGNUM_WORDS_OFFSET + r9*8], rax
    inc     r9d
    cmp     r9d, r14d
    jl      .dn_shift_loop
.dn_shift_last:
    ; last word
    mov     rax, [rsp + r14*8]
    mov     ecx, r15d
    shr     rax, cl
    mov     [r13 + BIGNUM_WORDS_OFFSET + r14*8], rax
    jmp     .store_results_skip_copy

    ; ------------------------------------------------------------
    ; copy quotient & set lengths
    ; ------------------------------------------------------------
.store_results:
    ; copy remainder (first n words of u) → remainder->words
    mov     ecx, r11d
    xor     rdx, rdx
.copy_r:
    mov     rax, [rsp + rdx*8]
    mov     [r13 + BIGNUM_WORDS_OFFSET + rdx*8], rax
    inc     rdx
    cmp     edx, ecx
    jl      .copy_r

.store_results_skip_copy:
    mov     r10, [r12 + BIGNUM_LEN_OFFSET]  ; RESTORE m
    ; copy q[0..m‑n] → quotient->words
    mov     r8d, r10d
    sub     r8d, r11d
    inc     r8d                     ; r8d = m‑n+1
    xor     r9d, r9d                ; i = 0
    lea     rdi, [rsp + (2*BIGNUM_CAPACITY+1)*8] ; q base
.copy_q:
    mov     rax, [rdi + r9*8]       ; q word
    mov     [r12 + BIGNUM_WORDS_OFFSET + r9*8], rax
    inc     r9d
    cmp     r9d, r8d
    jl      .copy_q

    ; set quotient.len (strip leading zeros)
    mov     rdx, r8
.multi_q_len_trim:
    test    rdx, rdx
    jz      .multi_q_len_zero
    dec     rdx
    mov     rax, [r12 + BIGNUM_WORDS_OFFSET + rdx*8]
    test    rax, rax
    jnz     .multi_q_len_set
    jmp     .multi_q_len_trim
.multi_q_len_set:
    inc     rdx
.multi_q_len_zero:
    mov     [r12 + BIGNUM_LEN_OFFSET], rdx

    ; set remainder.len (strip leading zeros)
    mov     rdx, r11
.multi_r_len_trim:
    test    rdx, rdx
    jz      .multi_r_len_zero
    dec     rdx
    mov     rax, [r13 + BIGNUM_WORDS_OFFSET + rdx*8]
    test    rax, rax
    jnz     .multi_r_len_set
    jmp     .multi_r_len_trim
.multi_r_len_set:
    inc     rdx
.multi_r_len_zero:
    mov     [r13 + BIGNUM_LEN_OFFSET], rdx

    ; fall through to .zero_tails

    ; ------------------------------------------------------------
    ;  Zero tails (clear unused words to prevent uninitialized data)
    ; ------------------------------------------------------------
.zero_tails:
    ; zero tail of quotient
    mov     rcx, [r12 + BIGNUM_LEN_OFFSET]
    lea     rdi, [r12 + BIGNUM_WORDS_OFFSET + rcx*8]
    mov     eax, BIGNUM_CAPACITY
    sub     eax, ecx
    mov     ecx, eax
    xor     eax, eax
    rep stosq

    ; zero tail of remainder
    mov     rcx, [r13 + BIGNUM_LEN_OFFSET]
    lea     rdi, [r13 + BIGNUM_WORDS_OFFSET + rcx*8]
    mov     eax, BIGNUM_CAPACITY
    sub     eax, ecx
    mov     ecx, eax
    xor     eax, eax
    rep stosq

    ; ------------------------------------------------------------
    ;  Success return
    ; ------------------------------------------------------------
.success:
    mov     eax, BIGNUM_DIV_BIGNUM_OK
    jmp     .epilogue

    ; ------------------------------------------------------------
    ;  Error returns
    ; ------------------------------------------------------------
.err_null_ptr:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_NULL_PTR
    jmp     .epilogue
.err_bad_len:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_BAD_LENGTH
    jmp     .epilogue
.err_overlap:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_BUFFER_OVERLAP
    jmp     .epilogue
.div_by_zero:
    mov     eax, BIGNUM_DIV_BIGNUM_ERR_DIV_BY_ZERO
    jmp     .epilogue

.epilogue:
    add     rsp, 8*(3*BIGNUM_CAPACITY + 1)
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx                 ; Restore callee-saved register rbx
    pop     rbp
    ret
