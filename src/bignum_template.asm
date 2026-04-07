; -----------------------------------------------------------------------------
; @file    bignum_template.asm
; @author  git@bayborodov.com
; @version 1.0.0
; @date    07.11.2025
;
; @brief   Низкоуровневая реализация логического сдвига bignum_t влево.
;
; @details
;   Эталонная ассемблерная реализация для Yasm x86-64 (System V ABI).
;   
;
; @history
;   - rev. 1 (04.08.2025): Первоначальная реализация на ассемблере.
;   - rev. 2 (04.08.2025): Рефакторинг по результатам ревью.
;   - rev. 3 (04.08.2025): Исправлена ошибка "invalid effective address".
;   - rev. 4 (04.08.2025): Повторное исправление "invalid effective address".
;   - rev. 5 (04.08.2025): Финальное исправление синтаксической ошибки.
;   - rev. 6 (04.08.2025): Исправлена логическая ошибка в проверке на
;                         переполнение и предупреждение компиляции.
;   - rev. 7 (04.08.2025): Рефакторинг по ревью: заменен `rep movsq`,
;                         оптимизирован битовый сдвиг, улучшен стиль.
;   - rev. 8 (04.08.2025): Исправлена ошибка "invalid combination of opcode".
;   - rev. 9 (04.08.2025): Исправлена регрессия с "invalid effective address".
;   - rev. 10 (04.08.2025): Окончательное исправление ошибки "invalid
;                          effective address", код прошел все тесты.
;   - rev. 11 (04.08.2025): Финальный аудит документации. Восстановлена полная
;                          история ревизий, уточнены комментарии.
;   - rev. 12 (05.08.2025): Оптимизация (Этап 1): Минимизировано количество
;                          сохраняемых регистров. Убраны rbx, r12-r15.
;   - rev. 13 (05.08.2025): Исправлена регрессия в rev. 12: `rdi` портился
;                          инструкцией `rep stosq`. `rdi` теперь сохраняется
;                          в `r11` перед блоком `zero_fill`.
;   - rev. 14 (14.08.2025): Уточнение арифметики указателей блоке word-shift:
;                          расчёт rbx = r11 – (word_shift * 8) теперь через lea и neg.
;                          Сделано упрощение error-paths, 
;                          сохраняются rbx и r11-r15 в прологе-епилоге,
;                          описаны @clobbers и @retval в документации функции.
;   - rev. 15 (15.08.2025): В ветке ПУТЬ ДЛЯ bit_shift == 0 оптимизирована 
;                          прямая копия с распаковкой ×4 вместо rep movsq
;   - rev. 16 (15.08.2025): Заменили операцию деления div на дешевый вариант  
;                          с комбинацией shr+and в расчете word_shift и bit_shift
;   - rev. 17 (29.09.2025): Оптимизация (Этап 2): оптимизированный подход, 
;                          который объединяет сдвиг по словам и битам в один проход 
;   - rev. 18 (07.11.2025): Removed version control functions and .data section
; -----------------------------------------------------------------------------

section .text

; =============================================================================
; @brief      Выполняет логический сдвиг большого числа влево.
;
; @details
;   **Алгоритм:**
;   1.  Проверка указателя на NULL.
;   2.  Проверка на переполнение:
;       a. Если `shift_amount` >= 2048, вернуть OVERFLOW.
;       b. Если `len` == 32, старший бит старшего слова установлен и
;          `shift_amount` > 0, вернуть OVERFLOW.
;   3.  Проверка тривиальных случаев (нулевой сдвиг, нулевая длина).
;   4.  Расчет сдвига в словах (`word_shift`) и битах (`bit_shift`).
;   5.  Расчет новой длины с усечением до `BIGNUM_CAPACITY`.
;   6.  Сдвиг по словам: копирование данных в старшие позиции.
;   7.  Сдвиг по битам: сдвиг внутри слов с переносом битов.
;   8.  Обновление `len` и нормализация (удаление ведущих нулей).
;
; @abi        System V AMD64 ABI
; @param[in]  rdi: bignum_t* restrict num (указатель на структуру)
; @param[in]  rsi: size_t shift_amount (величина сдвига)
;
; @return     rax: bignum_shift_status_t (0, -1 или -2)
; @retval 0 – success
; @retval -1 – null pointer
; @retval -2 – overflow
; @clobbers   rbx, r8–r15, rcx, rdx
; =============================================================================
; --- Константы ---
BIGNUM_CAPACITY         equ 32
BIGNUM_WORD_SIZE        equ 8
BIGNUM_BITS             equ BIGNUM_CAPACITY * 64
BIGNUM_OFFSET_WORDS     equ 0
BIGNUM_OFFSET_LEN       equ BIGNUM_CAPACITY * BIGNUM_WORD_SIZE
SUCCESS                 equ 0
ERROR_NULL_ARG          equ -1
ERROR_OVERFLOW          equ -2

global bignum_template
bignum_template:
    ; --- Пролог: сохраняем только rbp ---
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    ; rdi = num*, rsi = shift_amount

    ; 1. Проверка на NULL
    test    rdi, rdi
    jz      .error_null_arg

    ; -------------------------------------------------
    ;   Задаём базовый адрес массива слов
    ; -------------------------------------------------
    lea     rbx, [rdi + BIGNUM_OFFSET_WORDS]   ; rbx = &num->words[0]

    ; 2. Проверка контракта на переполнение
    cmp     rsi, BIGNUM_BITS
    jae     .error_overflow

    movsxd  r8, dword [rdi + BIGNUM_OFFSET_LEN] ; r8 = num->len
    cmp     r8, BIGNUM_CAPACITY
    jne     .check_trivial_cases

    mov     r9, [rdi + BIGNUM_OFFSET_WORDS + (BIGNUM_CAPACITY - 1) * BIGNUM_WORD_SIZE]
    mov     r10, 0x8000000000000000
    test    r9, r10
    jz      .check_trivial_cases

    cmp     rsi, 0
    jne     .error_overflow

.check_trivial_cases:
    ; 3. Тривиальные случаи
    test    r8, r8
    jz      .success
    cmp     rsi, 0
    je      .success

    ; 4. 4. Расчёт word_shift и bit_shift без div
    ; --- после trivial cases ---
    ; rsi = shift_amount
    ; r8  = old_len

    mov     rax, rsi            ; rax = shift_amount
    shr     rax, 6              ; rax = word_shift = shift_amount / 64
    mov     r12, rax            ; сохранить word_shift

    mov     r13d, esi           ; r13d = shift_amount
    and     r13d, 0x3F          ; r13d = bit_shift = shift_amount % 64

    ; 5. Расчёт new_len
    mov     r9, r8              ; r9 = old_len
    add     r9, r12             ; new_len += word_shift
    test    r13d, r13d          ; если есть неполный битовый сдвиг
    jz      .new_len_done
    inc     r9                  ; учесть дополнительное слово
.new_len_done:
    cmp     r9, BIGNUM_CAPACITY
    jbe     .set_new_len
    mov     r9, BIGNUM_CAPACITY
.set_new_len:

    ; 6. Выбор пути: только word-shift или с битовым сдвигом
    mov     cl, r13b            ; cl = bit_shift
    test    cl, cl
    jnz     .combined_path     ; если bit_shift > 0
    ; jmp     .word_shift_only_path


; --- ПУТЬ ДЛЯ bit_shift == 0 ---
.word_shift_only_path:
    mov     r10, rax        ; r10 = word_shift
    mov     r11, rdi        ; Сохраняем оригинальный rdi
    
    mov     rcx, r8         ; rcx = old_len
    test    rcx, rcx
    jz      .fill_zeros_only_path

    ; Прямая копия с распаковкой ×4
    mov   rcx, r8            ; rcx = old_len
    lea   rsi, [r11 + r8*8 - 8]
    lea   rdi, [r11 + r9*8 - 8]
.L_copy_unroll4:
    cmp   rcx, 4
    jb    .L_copy_tail
    mov   rax, [rsi]
    mov   [rdi], rax
    mov   rax, [rsi - 8]
    mov   [rdi - 8], rax
    mov   rax, [rsi - 16]
    mov   [rdi - 16], rax
    mov   rax, [rsi - 24]
    mov   [rdi - 24], rax
    sub   rsi, 32
    sub   rdi, 32
    sub   rcx, 4
    jmp   .L_copy_unroll4

.L_copy_tail:
    test  rcx, rcx
    jz    .L_copy_done

.L_copy_one:
    mov   rax, [rsi]
    mov   [rdi], rax
    sub   rsi, 8
    sub   rdi, 8
    dec   rcx
    jnz   .L_copy_one

.L_copy_done:
    ; r11 (orig) уже хранит базовый адрес, rdi и rsi возвращаются из loop

.fill_zeros_only_path:
    mov     rcx, r10        ; rcx = word_shift
    test    rcx, rcx
    jz      .update_len_and_normalize

    lea     rdi, [r11 + BIGNUM_OFFSET_WORDS]
    xor     rax, rax
    rep stosq
    
    mov     rdi, r11 ; Восстанавливаем rdi
    jmp     .update_len_and_normalize


; --- ПУТЬ ДЛЯ bit_shift > 0 ---

    ;; -------------------------------------------------
    ;; 4. Путь «word‑shift + bit‑shift»
    ;; -------------------------------------------------
.combined_path:
    mov     rcx, r9                ; количество слов результата
    lea     r15, [rbx + r9*8]      ; r15 = адрес за концом области (base + new_len*8)

.fill_word_shift:
    test    rcx, rcx
    jz      .bit_shift_phase
    dec     rcx
    mov     r11, rcx
    sub     r11, r12               ; src_index = rcx - word_shift
    cmp     r11, 0
    jl      .ws_store_zero
    cmp     r11, r8
    jge     .ws_store_zero
    mov     rax, [rbx + r11*8]     ; взять слово из исходного массива
    jmp     .ws_store
.ws_store_zero:
    xor     rax, rax
.ws_store:
    sub     r15, 8                 ; сдвинуть указатель вниз перед записью
    mov     [r15], rax
    jmp     .fill_word_shift



; --- битовый сдвиг внутри слов ---
.bit_shift_phase:
    xor     r14, r14           ; carry = 0
    mov     cl, r13b           ; cl = bit_shift (0..63)
    xor     r10, r10           ; index = 0

.bit_shift_loop:
    cmp     r10, r9
    jae     .update_len_and_normalize

    mov     rax, [rbx + r10*8]     ; current word
    mov     r11, rax               ; save original for carry calc

    ; left shift by cl, add previous carry
    test    cl, cl
    jz      .no_left_shift
    shl     rax, cl
.no_left_shift:
    or      rax, r14               ; add carry from lower word
    mov     [rbx + r10*8], rax

    ; compute new carry = top (64 - cl) bits of original r11
    test    cl, cl
    jz      .carry_zero            ; if cl == 0, carry = 0

    ; compute shift_right = 64 - cl in r12d
    mov     r12d, 64
    sub     r12d, ecx              ; r12d = 64 - cl  (ecx holds cl as low8)

    ; use CL as shift count for shr: set cl = low8(r12d)
    mov     cl, r12b               ; cl = (64 - original_cl)
    mov     rdx, r11
    shr     rdx, cl                ; rdx = original >> (64 - original_cl)
    mov     r14, rdx               ; new carry
    ; restore cl to original bit_shift for next iteration
    mov     cl, r13b

    jmp     .advance_index

.carry_zero:
    xor     r14, r14               ; carry = 0
.advance_index:
    inc     r10
    jmp     .bit_shift_loop



.update_len_and_normalize:
    ; 8. Обновление len и нормализация
    mov     [rdi + BIGNUM_OFFSET_LEN], r9d

.normalize_loop:
    cmp     r9, 1
    jle     .success
    cmp     qword [rdi + r9 * BIGNUM_WORD_SIZE - BIGNUM_WORD_SIZE], 0
    jne     .success
    dec     r9
    mov     [rdi + BIGNUM_OFFSET_LEN], r9d
    jmp     .normalize_loop

.error_null_arg:
    mov     eax, ERROR_NULL_ARG
    jmp     .epilogue

.error_overflow:
    mov     eax, ERROR_OVERFLOW
    jmp    .epilogue

.success:
    xor eax,eax            ; ie mov eax, SUCCESS + flags reset

.epilogue:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     rbx
    pop     rbp
    ret