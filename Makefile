# Makefile for bignum library function

# --- Configurable Variables ---
CONFIG ?= debug
# values: auto | yes | no
USE_ASM ?= auto
REPORT_NAME ?= current
# values: no | address | undefined
SAN ?= no
# yes — прогнать *_mt тесты под valgrind --tool=helgrind
HELGRIND ?= no
VALGRIND ?= valgrind

# --- Calculated Variables ---
REPOSITORY_NAME := $(notdir $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST))))))
FAMILY_NAME := $(firstword $(subst -, ,$(REPOSITORY_NAME)))
OPERATION_NAME := $(strip $(patsubst $(FAMILY_NAME)-%,%,$(REPOSITORY_NAME)))
UPPER_FAMILY_NAME := $(subst z,Z,$(subst y,Y,$(subst x,X,$(subst w,W,$(subst v,V,$(subst u,U,$(subst t,T,$(subst s,S,$(subst r,R,$(subst q,Q,$(subst p,P,$(subst o,O,$(subst n,N,$(subst m,M,$(subst l,L,$(subst k,K,$(subst j,J,$(subst i,I,$(subst h,H,$(subst g,G,$(subst f,F,$(subst e,E,$(subst d,D,$(subst c,C,$(subst b,B,$(subst a,A,$(FAMILY_NAME)))))))))))))))))))))))))))
LIB_NAME := $(subst -,_,$(notdir $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))))
UPPER_LIB_NAME := $(subst z,Z,$(subst y,Y,$(subst x,X,$(subst w,W,$(subst v,V,$(subst u,U,$(subst t,T,$(subst s,S,$(subst r,R,$(subst q,Q,$(subst p,P,$(subst o,O,$(subst n,N,$(subst m,M,$(subst l,L,$(subst k,K,$(subst j,J,$(subst i,I,$(subst h,H,$(subst g,G,$(subst f,F,$(subst e,E,$(subst d,D,$(subst c,C,$(subst b,B,$(subst a,A,$(LIB_NAME)))))))))))))))))))))))))))
NP := $(shell nproc | awk '{print $$1}')

# --- Tools ---
CC = gcc
AS = yasm
PERF = /usr/local/bin/perf
RM = rm -rf
MKDIR = mkdir -p
AR = ar
STRIP = strip
RL = ranlib
CPPCHECK = cppcheck
OBJCOPY = objcopy
NM = nm

# --- Directories ---
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
LIBS_DIR = libs
TESTS_DIR = tests
BENCH_DIR = benchmarks
INCLUDE_DIR = include
DIST_DIR = dist

COMMON_NAME := $(FAMILY_NAME)-common
COMMON_DIR  := $(LIBS_DIR)/$(COMMON_NAME)
REPORTS_DIR = $(BENCH_DIR)/reports
DIST_INCLUDE_DIR = $(DIST_DIR)/$(INCLUDE_DIR)
DIST_LIB_DIR = $(DIST_DIR)/$(LIBS_DIR)

# 1. Очищаем исходный список от любых случайных пробелов сразу при получении
SUBMODULES_RAW := $(strip $(patsubst $(LIBS_DIR)/%/,%,$(wildcard $(LIBS_DIR)/*/)))

# 2. Собираем финальный список, также оборачивая результат в strip
SUBMODULES := $(strip $(filter $(COMMON_NAME),$(SUBMODULES_RAW)) $(filter-out $(COMMON_NAME),$(SUBMODULES_RAW)))

# Отделяем сабмодули с исходниками (есть Makefile) от вендорных (нет Makefile)
SRC_SUBMODULES  := $(strip $(foreach d,$(SUBMODULES),$(if $(wildcard $(LIBS_DIR)/$(d)/Makefile),$(d),)))
DIST_SUBMODULES := $(strip $(filter-out $(SRC_SUBMODULES),$(SUBMODULES))) 

# 1. Генерируем все возможные пути для обоих типов модулей
SRC_PATHS = $(foreach d,$(SRC_SUBMODULES),$(LIBS_DIR)/$(d)/$(INCLUDE_DIR))
DIST_PATHS = $(foreach d,$(DIST_SUBMODULES),$(LIBS_DIR)/$(d)/$(DIST_DIR))

# 2. Определяем конкретный путь к COMMON_NAME (динамический выбор папки)
ifeq ($(filter $(strip $(COMMON_NAME)),$(strip $(DIST_SUBMODULES))),)
    TARGET_PATH = $(LIBS_DIR)/$(COMMON_NAME)/$(INCLUDE_DIR)
else
    TARGET_PATH = $(LIBS_DIR)/$(COMMON_NAME)/$(DIST_DIR)
endif

# 3. Объединяем все пути, но вычитаем TARGET_PATH из общего массива, 
# чтобы затем поставить его первым без дублирования.
ALL_PATHS = $(SRC_PATHS) $(DIST_PATHS)
SUBMODULES_INCLUDE_DIR = $(TARGET_PATH) $(filter-out $(TARGET_PATH),$(ALL_PATHS))


SUBMODULES_DIST_DIR := $(foreach d,$(DIST_SUBMODULES),$(LIBS_DIR)/$(d)/$(DIST_DIR))
SUBMODULES_DIST_LIB := $(foreach d,$(DIST_SUBMODULES),$(subst -,_,$(d)))

# Собираем OBJECTS только для тех сабмодулей, у которых реально есть исходники в src/
OBJECTS         := $(foreach d,$(SRC_SUBMODULES),$(if $(wildcard $(LIBS_DIR)/$(d)/src/$(subst -,_,$(d)).c $(LIBS_DIR)/$(d)/src/$(subst -,_,$(d)).asm),$(LIBS_DIR)/$(d)/build/$(subst -,_,$(d)).o,))

# Собираем все заголовочные файлы сабмодулей
SUBMODULES_HEADERS_RAW := $(foreach dir,$(SUBMODULES_INCLUDE_DIR),$(wildcard $(dir)/*.h))

# Выносим bignum.h на первое место, а затем добавляем все остальные файлы
SUBMODULES_HEADERS := $(filter $(COMMON_DIR)/$(INCLUDE_DIR)/$(FAMILY_NAME).h, $(SUBMODULES_HEADERS_RAW)) \
                      $(filter-out $(COMMON_DIR)/$(INCLUDE_DIR)/$(FAMILY_NAME).h, $(SUBMODULES_HEADERS_RAW))

# --- Source & Target Files ---
ASM_SRC := $(SRC_DIR)/$(LIB_NAME).asm

ifeq ($(strip $(USE_ASM)),auto)
    ifneq ($(wildcard $(ASM_SRC)),)
        SRC_EXT := asm
    else
        SRC_EXT := c
    endif
else ifeq ($(strip $(USE_ASM)),yes)
    SRC_EXT := asm
else
    SRC_EXT := c
endif

C_SRC = $(SRC_DIR)/$(LIB_NAME).$(SRC_EXT)
HEADER = $(INCLUDE_DIR)/$(LIB_NAME).h
FAMILY_HEADER = $(INCLUDE_DIR)/$(FAMILY_NAME).h
OBJ = $(BUILD_DIR)/$(LIB_NAME).o

TEST_SRCS := $(wildcard $(TESTS_DIR)/*.c)
TEST_BINS_MT := $(filter $(TESTS_DIR)/%_mt.c,$(TEST_SRCS))
TEST_BINS    := $(patsubst $(TESTS_DIR)/%.c,$(BIN_DIR)/%,$(TEST_SRCS))

BENCH_BIN = bench_$(LIB_NAME)
BENCH_BIN_ST = $(BIN_DIR)/$(BENCH_BIN)
BENCH_BIN_MT = $(BIN_DIR)/$(BENCH_BIN)_mt
BENCH_BINS = $(BENCH_BIN_ST) $(BENCH_BIN_MT)

STATIC_LIB = $(DIST_DIR)/lib$(LIB_NAME).a
SINGLE_HEADER = $(DIST_DIR)/$(LIB_NAME).h

# --- Flags ---
CFLAGS_BASE = -std=c11 -Wall -Wextra -pedantic -I$(INCLUDE_DIR) $(addprefix -I , $(SUBMODULES_INCLUDE_DIR))
ASFLAGS_BASE = -f elf64
LDFLAGS = -no-pie -lm 

# Динамически линкуем все вендорные библиотеки (те, что попали в DIST_SUBMODULES)
# Заменяем дефисы на подчеркивания для имени библиотеки (например, bignum-common -> -lbignum_common)
# $(addprefix -L, $(SUBMODULES_DIST_DIR)) $(addprefix -l, $(SUBMODULES_DIST_LIB))
LDFLAGS += $(foreach d,$(DIST_SUBMODULES),-L$(LIBS_DIR)/$(d)/dist -l$(subst -,_,$(d))) 

#Особый случай/Special Case
ifeq ($(strip $(OPERATION_NAME)),shift-right)
    LDFLAGS += -lgmp
endif

# --- Sanitizer flags ---
ifeq ($(strip $(SAN)),address)
    SAN_CFLAGS  := -fsanitize=address -g -O1 -fno-omit-frame-pointer
    SAN_LDFLAGS := -fsanitize=address
    SAN_LABEL   := AddressSanitizer
else ifeq ($(strip $(SAN)),undefined)
    SAN_CFLAGS  := -fsanitize=undefined -g -O1 -fno-omit-frame-pointer
    SAN_LDFLAGS := -fsanitize=undefined
    SAN_LABEL   := UndefinedBehaviorSanitizer
else ifeq ($(strip $(SAN)),thread)
    $(warning SAN=thread не инструментирует yasm. Используйте `make test_helgrind` для гонок.)
    SAN_CFLAGS  :=
    SAN_LDFLAGS :=
    SAN_LABEL   := (none)
else
    SAN_CFLAGS  :=
    SAN_LDFLAGS :=
    SAN_LABEL   := (none)
endif

SAN_LOG_PREFIX := $(BIN_DIR)/sanitize_

ifeq ($(strip $(CONFIG)), release)
    CFLAGS = $(CFLAGS_BASE) -O2 -march=native $(SAN_CFLAGS)
    ASFLAGS = $(ASFLAGS_BASE)
else
    CFLAGS = $(CFLAGS_BASE) -g $(SAN_CFLAGS)
    ASFLAGS = $(ASFLAGS_BASE) -g dwarf2
endif

CFLAGS += -Wl,-z,noexecstack
LDFLAGS += $(SAN_LDFLAGS)

# --- Perf-specific settings ---
ifeq ($(SRC_EXT),asm)
ASM_LABELS := $(shell grep -E '^[[:space:]]*\.[A-Za-z0-9_].*:' $(ASM_SRC) 2>/dev/null | sed -E 's/^[[:space:]]*\.([A-Za-z0-9_]+):/\1/; s/[[:space:]]\+/|/g' )
else
ASM_LABELS :=
endif
space := $(empty) $(empty)
ASM_LABELS := $(subst $(space),|,$(ASM_LABELS))

PERF_SYMBOL_FILTER = '$(LIB_NAME)\.($(ASM_LABELS))'
PERF_DATA_ST = /tmp/$(LIB_NAME)_$(REPORT_NAME)_st.perf
PERF_DATA_MT = /tmp/$(LIB_NAME)_$(REPORT_NAME)_mt.perf
REPORT_FILE_ST = $(REPORTS_DIR)/$(REPORT_NAME)_st.txt
REPORT_FILE_MT = $(REPORTS_DIR)/$(REPORT_NAME)_mt.txt
RECORD_OPT = -F 1000 -e cycles,cache-misses,branch-misses -g --call-graph fp
REPORT_OPT = --percent-limit 1.0 --sort comm,dso,symbol --symbol-filter=$(PERF_SYMBOL_FILTER)

.PHONY: all build lint test test_sanitize test_helgrind bench install generate-header dist clean help show-calc

all: build
build: $(OBJ) $(OBJECTS)

# --- Обычный прогон: однократно, без санитайзеров.
test: $(TEST_BINS)
	@echo "=== Running unit tests (CONFIG=$(CONFIG), SAN=$(SAN_LABEL)) ==="
	@total=0; fail=0; \
	for t in $(TEST_BINS); do \
	  total=$$((total+1)); \
	  echo "--- $$t ---"; \
	  if ./$$t; then :; else fail=$$((fail+1)); echo "*** $$t FAILED ***"; fi; \
	done; \
	echo "=== Summary: $$fail / $$total failed ==="; \
	test $$fail -eq 0

# --- Прогон под ASan/UBSan.
# Логика: доверяем exit code санитайзера. ASan при наличии проблемы
# возвращает ненулевой код (по умолчанию halt_on_error=1), UBSan — тоже.
# halt_on_error=0 нужен, чтобы при ОДНОЙ найденной проблеме прогон
# продолжился и мы увидели ВСЕ проблемы, а не упали на первой.
# Exit code в этом случае = 0 (тест формально прошёл), поэтому после
# прогона проверяем stderr на маркеры санитайзера, которые пишутся
# ТОЛЬКО при реальных проблемах ("==PID==ERROR:", "runtime error:", "leak").
# Использование:
#   make test_sanitize SAN=address
#   make test_sanitize SAN=undefined CONFIG=debug
test_sanitize: $(TEST_BINS)
	@echo "=== Running tests under $(SAN_LABEL) (CONFIG=$(CONFIG)) ==="
	@total=0; fail=0; san_fail=0; \
	for t in $(TEST_BINS); do \
	  total=$$((total+1)); \
	  name=$$(basename $$t); \
	  log=$(SAN_LOG_PREFIX)$$name.log; \
	  echo "--- $$t (log: $$log) ---"; \
	  rm -f $$log; \
	  ASAN_OPTIONS=halt_on_error=0:detect_leaks=1:abort_on_error=0 \
	  UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1:abort_on_error=0 \
	    ./$$t > $$log 2>&1; \
	  rc=$$?; \
	  # Маркеры, которые санитайзеры пишут ТОЛЬКО при реальных проблемах. \
	  # Эти строки не встречаются в обычном выводе тестов. \
	  if grep -qE '(==[0-9]+==(ERROR|WARNING|runtime error|AddressSanitizer|LeakSanitizer)|SUMMARY: AddressSanitizer|runtime error:|leak [A-Za-z]+ detected)' $$log; then \
	    echo "  SANITIZER ISSUE (rc=$$rc, see $$log)"; \
	    san_fail=$$((san_fail+1)); \
	  elif [ $$rc -ne 0 ]; then \
	    echo "  TEST FAILED (rc=$$rc, see $$log)"; \
	    fail=$$((fail+1)); \
	  else \
	    echo "  OK"; \
	  fi; \
	done; \
	echo "=== Summary: tests=$$total, failed=$$fail, sanitizer_issues=$$san_fail ==="; \
	test $$fail -eq 0 && test $$san_fail -eq 0

# --- Прогон *_mt тестов под Helgrind.
# Использование:
#   make test_helgrind
# Требования: valgrind (apt install valgrind).
test_helgrind: $(TEST_BINS)
	@echo "=== Running MT tests under Helgrind (CONFIG=$(CONFIG)) ==="
	@total=0; fail=0; \
	mt_binaries="$(patsubst $(TESTS_DIR)/%.c,$(BIN_DIR)/%,$(TEST_BINS_MT))"; \
	for t in $$mt_binaries; do \
	  if [ ! -x $$t ]; then continue; fi; \
	  total=$$((total+1)); \
	  name=$$(basename $$t); \
	  log=$(BIN_DIR)/helgrind_$$name.log; \
	  echo "--- $$t (log: $$log) ---"; \
	  rm -f $$log; \
	  if $(VALGRIND) --tool=helgrind --error-exitcode=42 --log-file=$$log ./$$t > /dev/null 2>&1; then \
	    echo "  OK (no races detected)"; \
	  else \
	    rc=$$?; \
	    if [ $$rc -eq 42 ]; then \
	      echo "  RACE DETECTED (see $$log)"; fail=$$((fail+1)); \
	    else \
	      echo "  TEST FAILED (rc=$$rc, see $$log)"; fail=$$((fail+1)); \
	    fi; \
	  fi; \
	done; \
	echo "=== Summary: $$fail / $$total helgrind runs found races ==="; \
	test $$fail -eq 0

# rev.12: clean убран из зависимостей; ST и MT — отдельные таргеты;
# MT бенмарк собирается с -pthread.
bench: bench_st bench_mt | $(REPORTS_DIR)
	@echo ""
	@echo "Both bench reports written to $(REPORTS_DIR)/"
	@ls -l $(REPORTS_DIR)/$(REPORT_NAME)_*.txt

bench_st: $(BENCH_BIN_ST)
	@echo "=== ST benchmark for report: $(REPORT_NAME) (CONFIG=$(CONFIG)) ==="
	@$(MKDIR) $(REPORTS_DIR)
	@sudo sysctl -w kernel.perf_event_max_sample_rate=10000 > /dev/null
	@taskset 0x1 $(PERF) record $(RECORD_OPT) -o $(PERF_DATA_ST) -- $(BENCH_BIN_ST)
	@$(PERF) report -i $(PERF_DATA_ST) $(REPORT_OPT) --dsos $(BENCH_BIN) --stdio > $(REPORT_FILE_ST)
	@$(RM) $(PERF_DATA_ST)
	@echo "ST report: $(REPORT_FILE_ST)"

bench_mt: $(BENCH_BIN_MT)
	@echo "=== MT benchmark for report: $(REPORT_NAME) (CONFIG=$(CONFIG)) ==="
	@$(MKDIR) $(REPORTS_DIR)	
	@taskset --cpu-list 1-$(NP) $(PERF) record $(RECORD_OPT) -o $(PERF_DATA_MT) -- $(BENCH_BIN_MT)
	@$(PERF) report -i $(PERF_DATA_MT) $(REPORT_OPT) --dsos $(BENCH_BIN)_mt --stdio > $(REPORT_FILE_MT)
	@$(RM) $(PERF_DATA_MT)
	@echo "MT report: $(REPORT_FILE_MT)"


install: clean $(OBJ) $(OBJECTS) | $(DIST_INCLUDE_DIR) $(DIST_LIB_DIR)
	@printf "%s" "Installing product to $(DIST_DIR)/ (CONFIG=$(CONFIG))..."
	@if [ -f "$(INCLUDE_DIR)/$(FAMILY_NAME).h" ]; then \
		cp "$(INCLUDE_DIR)/$(FAMILY_NAME).h" "$(DIST_INCLUDE_DIR)/"; \
	fi	
	@cp $(HEADER) $(SUBMODULES_HEADERS) $(DIST_INCLUDE_DIR)/
	@cp $(OBJ) $(OBJECTS) $(DIST_LIB_DIR)/
	@$(foreach d,$(DIST_SUBMODULES), \
		cd $(DIST_LIB_DIR) && $(AR) x ../../$(LIBS_DIR)/$(d)/dist/lib$(subst -,_,$(d)).a && cd ../..; \
	)
	@echo "Ok"
	@tree $(DIST_DIR)/
	@cp $(TESTS_DIR)/test_$(LIB_NAME)_runner.c $(DIST_DIR)/
	@$(CC) $(DIST_DIR)/test_$(LIB_NAME)_runner.c  $(DIST_DIR)/$(LIBS_DIR)/*.o -I$(DIST_DIR)/$(INCLUDE_DIR) -o $(DIST_DIR)/test_$(LIB_NAME)_runner -no-pie
	@$(DIST_DIR)/test_$(LIB_NAME)_runner
	@$(RM) $(DIST_DIR)/test_$(LIB_NAME)_runner

generate-header:
	@$(MKDIR) $(DIST_DIR)
	@printf "%s" "Generating single-file header..."
	@echo "#ifndef $(UPPER_LIB_NAME)_SINGLE_H" > $(SINGLE_HEADER)
	@echo "#define $(UPPER_LIB_NAME)_SINGLE_H" >> $(SINGLE_HEADER)
	@echo "" >> $(SINGLE_HEADER)
	@if [ -n "$(strip $(SUBMODULES_HEADERS))" ]; then \
		sed -e '/#include "$(FAMILY_NAME).h"/d' -e '/#include <$(FAMILY_NAME).h>/d' $(SUBMODULES_HEADERS) >> $(SINGLE_HEADER); \
	else \
		echo "\n\tSubmodules is empty. Use family header"; \
		sed -e '/#include "$(FAMILY_NAME).h"/d' -e '/#include <$(FAMILY_NAME).h>/d' $(FAMILY_HEADER) >> $(SINGLE_HEADER); \
	fi
	echo "\n/* --- Included from include/$(LIB_NAME).h --- */" >> $(SINGLE_HEADER)
	sed -e '/$(UPPER_LIB_NAME)_H/d' -e '/#include <$(FAMILY_NAME).h>/d' -e '/#include "$(FAMILY_NAME).h"/d' $(HEADER) >> $(SINGLE_HEADER)	
	@echo "" >> $(SINGLE_HEADER)
	@echo "#endif // $(UPPER_LIB_NAME)_SINGLE_H" >> $(SINGLE_HEADER)
	@echo "\n\tStep 1: Removing duplicate code blocks..."
	@awk ' \
	BEGIN { in_guard = 0; depth = 0; } \
	{ \
		stripped = $$0; sub(/^[[:space:]]*/, "", stripped); \
		if (in_guard == 1) { \
			if (stripped ~ /^#(if|ifndef|ifdef)/) { depth++; } \
			else if (stripped ~ /^#endif/) { \
				depth--; \
				if (depth == 0) in_guard = 0; \
			} \
			next; \
		} \
		if (stripped ~ /^#ifndef[[:space:]]+[A-Za-z0-9_]+/) { \
			split(stripped, p, /[[:space:]]+/); name = p[2]; \
			if (seen[name]) { in_guard = 1; depth = 1; next; } \
			seen[name] = 1; \
		} \
		print $$0; \
	}' $(SINGLE_HEADER) > $(SINGLE_HEADER)_tmp.h
	@echo "\tStep 2: Removing duplicate Doxygen blocks..."
	@awk ' \
	BEGIN { in_comment = 0; } \
	{ \
		stripped = $$0; sub(/^[[:space:]]*/, "", stripped); \
		if (in_comment == 1) { \
			if (stripped ~ /\*\//) { in_comment = 0; } \
			next; \
		} \
		if (stripped ~ /^\/\*\*/) { \
			# Мы нашли Doxygen-блок. Проверяем, видели ли мы его раньше. \
			# Чтобы идентифицировать блок, мы создаем хеш из первых двух строк. \
			block_id = stripped; \
			getline next_line; \
			block_id = block_id " " next_line; \
			\
			if (seen_doc[block_id]) { \
				in_comment = 1; \
				# Нужно пропустить текущую строку, так как она уже в block_id \
				# Но мы должны проверить, не закрылся ли комментарий сразу \
				if (next_line ~ /\*\//) { in_comment = 0; } \
				next; \
			} \
			seen_doc[block_id] = 1; \
			print stripped; \
			print next_line; \
			next; \
		} \
		print $$0; \
	}' $(SINGLE_HEADER)_tmp.h > $(SINGLE_HEADER)_final.h
	@rm -f $(SINGLE_HEADER)_tmp.h
	@mv $(SINGLE_HEADER)_final.h $(SINGLE_HEADER)
	@echo "Done. Result saved to $(SINGLE_HEADER)"		
	@echo "Ok"

dist: clean
	@echo "Creating single-file header distribution in $(DIST_DIR)/ (CONFIG=$(CONFIG))...."
	@$(MKDIR) $(DIST_DIR)
	@$(MAKE) -s build CONFIG=release
	@printf "%s" "Stripping object files, keeping symbol $(LIB_NAME)..."
	@$(STRIP) --strip-debug $(OBJ) $(OBJECTS) || true;
	@$(STRIP) --strip-unneeded $(OBJ) $(OBJECTS) || true;
	@echo "Ok"
	@printf "%s" "Create static library lib$(LIB_NAME).a ..."
	@$(AR) rcs $(STATIC_LIB) $(OBJ) $(OBJECTS)
	@$(foreach d,$(DIST_SUBMODULES), \
	$(MKDIR) $(BUILD_DIR)/tmp_$(d) && \
	cd $(BUILD_DIR)/tmp_$(d) && \
	$(AR) x ../../$(LIBS_DIR)/$(d)/dist/lib$(subst -,_,$(d)).a && \
	$(AR) r ../../$(STATIC_LIB) *.o && \
	cd ../..; \
	)
	@$(RL) $(STATIC_LIB)
	@echo "Ok"
	@$(NM) -g --defined-only  $(STATIC_LIB)
	@$(MAKE) -s generate-header
	@cp README.md $(DIST_DIR)/
	@cp LICENSE $(DIST_DIR)/
	@cp $(TESTS_DIR)/test_$(LIB_NAME)_runner.c $(DIST_DIR)/
	@$(CC) $(DIST_DIR)/test_$(LIB_NAME)_runner.c -L$(DIST_DIR) -l$(LIB_NAME) -o $(DIST_DIR)/test_$(LIB_NAME)_runner -no-pie
	@$(DIST_DIR)/test_$(LIB_NAME)_runner
	@$(RM) $(DIST_DIR)/test_$(LIB_NAME)_runner
	@echo "Distribution created successfully in $(DIST_DIR)/ "
	@ls -l $(DIST_DIR)

# --- Compilation Rules ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling C: $< -> $@ (CONFIG=$(CONFIG))..."
	@$(MKDIR) $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.asm
	@echo "Assembling ASM: $< -> $@ (CONFIG=$(CONFIG))..."
	@$(MKDIR) $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(OBJ): $(C_SRC)
	@echo "Builds the main object file '$(OBJ)' (CONFIG=$(CONFIG))..."
	@$(MKDIR) $(BUILD_DIR)
ifeq ($(SRC_EXT),c)
	$(CC) $(CFLAGS) -c $(C_SRC) -o $(OBJ)
else
	$(AS) $(ASFLAGS) -o $(OBJ) $(C_SRC)
endif


$(OBJECTS):
	@echo "Building source submodules... (CONFIG=$(CONFIG))... "
	@$(foreach d,$(SRC_SUBMODULES), \
	  (echo "\tBuild for $(d) ..." && $(MAKE) -C $(LIBS_DIR)/$(d) -s build CONFIG=release USE_ASM=auto CFLAGS+=-Wl,-z,noexecstack) || echo "\n\t\t⚠️  $(d) no rule build\n"; \
	)


$(BIN_DIR)/%: $(TESTS_DIR)/%.c $(OBJ) $(OBJECTS) | $(BIN_DIR)
	@$(MKDIR) $(BIN_DIR)
	@$(CC) $(CFLAGS) $< $(OBJECTS) $(OBJ) -o $@ $(LDFLAGS) \
	  $(if $(filter %_mt,$*),-pthread)
$(BIN_DIR)/bench_%: $(BENCH_DIR)/bench_%.c $(OBJ) $(OBJECTS) | $(BIN_DIR)
	@$(MAKE) -s build CONFIG=debug
	@$(CC) $(CFLAGS) -g $< $(OBJECTS) $(OBJ) -o $@ $(LDFLAGS) $(if $(filter %_mt,$*),-pthread)

# --- Utility Targets ---
$(BIN_DIR) $(REPORTS_DIR) $(DIST_INCLUDE_DIR) $(DIST_LIB_DIR):
	@$(MKDIR) $@

lint:
	@echo "Running static analysis on C source files..."
	@$(CPPCHECK) --std=c11 --enable=all --error-exitcode=1 --suppress=missingIncludeSystem \
	    --inline-suppr --inconclusive --check-config \
	    -I$(INCLUDE_DIR) $(addprefix -I , $(SUBMODULES_INCLUDE_DIR)) \
	    $(TESTS_DIR)/ $(BENCH_DIR)/ $(DIST_DIR)/ $(SRC_DIR)/ $(LIBS_DIR)/

clean:
	@echo "Cleaning up build artifacts (build/, bin/, dist/)..."
	@$(RM) $(BUILD_DIR) $(BIN_DIR) $(DIST_DIR)
	@echo "Cleaning up submodule artifacts:" ;
	@$(foreach d,$(SRC_SUBMODULES), \
	  if [ -f $(LIBS_DIR)/$(d)/Makefile ]; then \
	    (printf "%s" "Clean for $(d) : " && $(MAKE) -C $(LIBS_DIR)/$(d) -s clean) || echo "\n\t\t⚠️  $(d) has no rule clean\n"; \
	  else \
	    echo "Skipping clean for $(d) (no Makefile found)"; \
	  fi; \
	)


help:
	@echo "Usage: make <target> [CONFIG=release] [REPORT_NAME=my_report]"
	@echo ""
	@echo "Main Targets:"
	@echo "  all/build      Builds the main object file."
	@echo "  lint           Static analysis on C sources."
	@echo "  test           Builds and runs all unit tests."
	@echo "  test_sanitize  Runs tests under sanitizer: make test_sanitize SAN={address|undefined}"
	@echo "  test_helgrind  Runs *_mt tests under valgrind --tool=helgrind for race detection."
	@echo "  bench          Runs performance benchmarks with perf."
	@echo "  install        Installs product into dist/ for internal use."
	@echo "  dist           Builds a single-header + static-lib distribution in dist/."
	@echo "  clean          Removes build/, bin/, dist/."
	@echo "  help           Shows this help message."
	@echo ""
	@echo "Logs:"
	@echo "  Sanitizer logs: \$$(BIN_DIR)/sanitize_<test>.log"
	@echo "  Helgrind logs:  \$$(BIN_DIR)/helgrind_<test>_mt.log"
	@echo ""
	@echo "Optimization Cycle Example:"
	@echo "  1. make bench REPORT_NAME=baseline"
	@echo "  2. ...edit code..."
	@echo "  3. make test"
	@echo "  4. make bench REPORT_NAME=opt_v1"
	@echo "  5. diff -u benchmarks/reports/baseline_st.txt benchmarks/reports/opt_v1_st.txt"	

show-calc:
	@echo "REPOSITORY_NAME = '$(REPOSITORY_NAME)'"
	@echo "FAMILY_NAME = '$(FAMILY_NAME)'"
	@echo "OPERATION_NAME = '$(OPERATION_NAME)'"	
	@echo "LIB_NAME = '$(LIB_NAME)'"
	@echo "UPPER_LIB_NAME = '$(UPPER_LIB_NAME)'"
	@echo "NP = '$(NP)'"
	@echo "ASM_LABELS = '$(ASM_LABELS)'"
	@echo "Количество меток: $(words $(subst |, ,$(ASM_LABELS)))"
	@echo "OBJ = '$(OBJ)'"
	@echo "OBJECTS = '$(OBJECTS)'"
	@echo "C_SRC = '$(C_SRC)'"
	@echo "HEADER = '$(HEADER)'"
	@echo "FAMILY_HEADER = '$(FAMILY_HEADER)'"
	@echo "HEADERS = '$(HEADERS)'"
	@echo "SINGLE_HEADER = '$(SINGLE_HEADER)'"
	@echo "SRC_EXT = '$(SRC_EXT)'"
	@echo "USE_ASM = '$(USE_ASM)'"
	@echo "ASM_SRC = '$(ASM_SRC)'"
	@echo "SUBMODULES = '$(SUBMODULES)'"
	@echo "COMMON_NAME = '$(COMMON_NAME)'"	
	@echo "SUBMODULES_INCLUDE_DIR = '$(SUBMODULES_INCLUDE_DIR)'"
	@echo "SUBMODULES_DIST_DIR = '$(SUBMODULES_DIST_DIR)'"
	@echo "SUBMODULES_DIST_LIB = '$(SUBMODULES_DIST_LIB)'"	
	@echo "SUBMODULES_HEADERS_RAW = '$(SUBMODULES_HEADERS_RAW)'"	
	@echo "SUBMODULES_HEADERS = '$(SUBMODULES_HEADERS)'"
	@echo "TEST_BINS_MT = '$(TEST_BINS_MT)'"
	@echo "TEST_BINS = '$(TEST_BINS)'"
	@echo "SAN = $(SAN) ($(SAN_LABEL))"
	@echo "HELGRIND = $(HELGRIND)"
	@echo "SRC_SUBMODULES = '$(SRC_SUBMODULES)'"	
	@echo "DIST_SUBMODULES = '$(DIST_SUBMODULES)'"	