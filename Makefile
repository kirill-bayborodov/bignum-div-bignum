# Makefile for bignum library function

# --- Configurable Variables ---
CONFIG ?= debug
REPORT_NAME ?= current

# --- Calculated Variables --
REPOSITORY_NAME := $(notdir $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST))))))
FAMILY_NAME := $(firstword $(subst -, ,$(REPOSITORY_NAME)))
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

# Собираем список всех объектных файлов, которые должны войти в библиотеку
SUBMODULES  := $(patsubst $(LIBS_DIR)/%/,%,$(filter %/,$(wildcard $(LIBS_DIR)/*/)))
SUBMODULES_INCLUDE_DIR := $(foreach d,$(SUBMODULES),$(LIBS_DIR)/$(d)/$(INCLUDE_DIR))
OBJ_LIST    := $(filter-out $(COMMON_NAME),$(patsubst $(LIBS_DIR)/%/,%,$(filter %/,$(wildcard $(LIBS_DIR)/*/))))
OBJECTS     := $(foreach d,$(OBJ_LIST),$(LIBS_DIR)/$(d)/$(BUILD_DIR)/$(subst -,_,$(d)).o)
ASM_SOURCES := $(foreach d,$(OBJ_LIST),$(LIBS_DIR)/$(d)/$(SRC_DIR)/$(subst -,_,$(d)).asm)
HEADERS     := $(foreach d,$(OBJ_LIST),$(LIBS_DIR)/$(d)/$(INCLUDE_DIR)/$(subst -,_,$(d)).h)

# --- Source & Target Files ---
ASM_SRC = $(SRC_DIR)/$(LIB_NAME).asm
HEADER = $(INCLUDE_DIR)/$(LIB_NAME).h
OBJ = $(BUILD_DIR)/$(LIB_NAME).o 
TEST_BINS = $(patsubst $(TESTS_DIR)/%.c, $(BIN_DIR)/%, $(wildcard $(TESTS_DIR)/*.c))
BENCH_BIN = bench_$(LIB_NAME)
BENCH_BIN_ST = $(BIN_DIR)/$(BENCH_BIN)
BENCH_BIN_MT = $(BIN_DIR)/$(BENCH_BIN)_mt
BENCH_BINS = $(BENCH_BIN_ST) $(BENCH_BIN_MT)

# --- Target Files ---
# Имя финальной статической библиотеки
STATIC_LIB = $(DIST_DIR)/lib$(LIB_NAME).a
# Имя финального единого заголовочного файла
SINGLE_HEADER = $(DIST_DIR)/$(LIB_NAME).h

# --- Flags ---
CFLAGS_BASE = -std=c11 -Wall -Wextra -pedantic -I$(INCLUDE_DIR) $(addprefix -I , $(SUBMODULES_INCLUDE_DIR))
ASFLAGS_BASE = -f elf64
LDFLAGS = -no-pie -lm

ifeq ($(CONFIG), release)
    CFLAGS = $(CFLAGS_BASE) -O2 -march=native
    ASFLAGS = $(ASFLAGS_BASE)
else
    CFLAGS = $(CFLAGS_BASE) -g
    ASFLAGS = $(ASFLAGS_BASE) -g dwarf2
endif

CFLAGS += -Wl,-z,noexecstack

# --- Perf-specific settings ---
ASM_LABELS := $(shell grep -E '^[[:space:]]*\.[A-Za-z0-9_].*:' $(ASM_SRC) | sed -E 's/^[[:space:]]*\.([A-Za-z0-9_]+):/\1/; s/[[:space:]]\+/|/g' )
space := $(empty) $(empty)
ASM_LABELS := $(subst $(space),|,$(ASM_LABELS))

PERF_SYMBOL_FILTER = '$(LIB_NAME)\.($(ASM_LABELS))'
PERF_DATA_ST = /tmp/$(LIB_NAME)_$(REPORT_NAME)_st.perf
PERF_DATA_MT = /tmp/$(LIB_NAME)_$(REPORT_NAME)_mt.perf
REPORT_FILE_ST = $(REPORTS_DIR)/$(REPORT_NAME)_st.txt
REPORT_FILE_MT = $(REPORTS_DIR)/$(REPORT_NAME)_mt.txt
RECORD_OPT = -F 1000 -e cycles,cache-misses,branch-misses -g --call-graph fp
REPORT_OPT = --percent-limit 1.0 --sort comm,dso,symbol --symbol-filter=$(PERF_SYMBOL_FILTER)

.PHONY: all build lint test bench install dist clean help

all: build
build: $(OBJ) $(OBJECTS)

test: $(TEST_BINS)
	@echo "Running unit tests (CONFIG=$(CONFIG))..."
	@for test in $(TEST_BINS); do ./$$test; done

bench: clean $(BENCH_BINS) | $(REPORTS_DIR)
	@echo "Running benchmarks for report: $(REPORT_NAME) (CONFIG=$(CONFIG))..."
	@sudo sysctl -w kernel.perf_event_max_sample_rate=10000 > /dev/null
	@# --- Single-threaded ---
	@taskset 0x1 $(PERF) record $(RECORD_OPT) -o $(PERF_DATA_ST) -- $(BENCH_BIN_ST)
	@$(PERF) report -i $(PERF_DATA_ST) $(REPORT_OPT) --dsos $(BENCH_BIN) --stdio > $(REPORT_FILE_ST)
	@$(RM) $(PERF_DATA_ST)
	@# --- Multi-threaded ---
	@taskset --cpu-list 1-$(NP) $(PERF) record $(RECORD_OPT) -o $(PERF_DATA_MT) -- $(BENCH_BIN_MT)
	@$(PERF) report -i $(PERF_DATA_MT) $(REPORT_OPT) --dsos $(BENCH_BIN)_mt  --stdio > $(REPORT_FILE_MT)
	@$(RM) $(PERF_DATA_MT)
	@echo "Reports saved. Temporary perf data removed."

install: clean $(OBJ) $(OBJECTS) | $(DIST_INCLUDE_DIR) $(DIST_LIB_DIR)
	@printf "%s" "Installing product to $(DIST_DIR)/ (CONFIG=$(CONFIG))..."
	@cp $(HEADER) $(foreach dir,$(SUBMODULES_INCLUDE_DIR),$(wildcard $(dir)/*.h)) $(DIST_INCLUDE_DIR)/
	@cp $(OBJ) $(OBJECTS) $(DIST_LIB_DIR)/
	@echo "Ok"
	@tree $(DIST_DIR)/
# Компилируем тест-раннер в dist, линкуя объектник из dist и тестируем сборку
	@cp $(TESTS_DIR)/test_$(LIB_NAME)_runner.c $(DIST_DIR)/
	@$(CC) $(DIST_DIR)/test_$(LIB_NAME)_runner.c  $(DIST_DIR)/$(LIBS_DIR)/*.o -I$(DIST_DIR)/$(INCLUDE_DIR) -o $(DIST_DIR)/test_$(LIB_NAME)_runner -no-pie
	@$(DIST_DIR)/test_$(LIB_NAME)_runner	
	@$(RM) $(DIST_DIR)/test_$(LIB_NAME)_runner

dist: clean
	@echo "Creating single-file header distribution in $(DIST_DIR)/ (CONFIG=$(CONFIG))..."
	@$(MKDIR) $(DIST_DIR)
# 1. Собираем объектный файл в release-конфигурации
	@$(MAKE) -s build CONFIG=release

# 2. Удаляем всю лишнюю информацию из объектного файла
	@printf "%s" "Stripping object files, keeping symbol $(LIB_NAME)..."
	@$(STRIP) --strip-debug $(OBJ) $(OBJECTS) || true; 
	@$(STRIP) --strip-unneeded $(OBJ) $(OBJECTS) || true;  
	@echo "Ok"
# 3. Создаем статическую библиотеку
	@printf "%s" "Create static library lib$(LIB_NAME).a ..."
	@$(AR) rcs $(STATIC_LIB) $(OBJ) $(OBJECTS)
	@$(RL) $(STATIC_LIB)
	@echo "Ok"
	@$(NM) -g --defined-only  $(STATIC_LIB)		 
# 4. Создаем КОРРЕКТНЫЙ единый заголовочный файл
	@printf "%s"  "Generating single-file header..."
# 4.1. Начинаем с единого include guard
	@echo "#ifndef $(UPPER_LIB_NAME)_SINGLE_H" > $(SINGLE_HEADER)
	@echo "#define $(UPPER_LIB_NAME)_SINGLE_H" >> $(SINGLE_HEADER)
	@echo "" >> $(SINGLE_HEADER)

# 4.2. Вставляем содержимое bignum.h, но БЕЗ его собственных include guards
	@echo "/* --- Included from libs/bignum-common/include/bignum.h --- */" >> $(SINGLE_HEADER)
# sed удаляет строки, содержащие BIGNUM_H
	@sed '/BIGNUM_H/d' $(COMMON_DIR)/$(INCLUDE_DIR)/$(FAMILY_NAME).h >> $(SINGLE_HEADER)
	@echo "" >> $(SINGLE_HEADER)

# 4.3. Вставляем содержимое $(LIB_NAME).h, но БЕЗ его include guards и БЕЗ #include "bignum.h"
	@echo "/* --- Included from include/$(LIB_NAME).h --- */" >> $(SINGLE_HEADER)
# sed удаляет строки с BIGNUM_SHIFT_LEFT_H и #include "bignum.h"
	@sed -e '/$(UPPER_LIB_NAME)_H/d' -e '/#include <$(FAMILY_NAME).h>/d' $(HEADER) >> $(SINGLE_HEADER)
	@echo "" >> $(SINGLE_HEADER)

# 4.4. Закрываем единый include guard
	@echo "#endif // $(UPPER_LIB_NAME)_SINGLE_H" >> $(SINGLE_HEADER)
	@echo "Ok"
# 5. Копируем README и LICENSE
	@cp README.md $(DIST_DIR)/
	@cp LICENSE $(DIST_DIR)/
# 6. Компилируем тест-раннер в dist, статически линкуя библиотеку из dist и тестируем сборку с библиотекой
	@cp $(TESTS_DIR)/test_$(LIB_NAME)_runner.c $(DIST_DIR)/
	@$(CC) $(DIST_DIR)/test_$(LIB_NAME)_runner.c -L$(DIST_DIR) -l$(LIB_NAME) -o $(DIST_DIR)/test_$(LIB_NAME)_runner -no-pie
	@$(DIST_DIR)/test_$(LIB_NAME)_runner	
	@$(RM) $(DIST_DIR)/test_$(LIB_NAME)_runner

	@echo "Distribution created successfully in $(DIST_DIR)/ "
	@ls -l $(DIST_DIR)

# --- Compilation Rules ---
$(OBJ): $(ASM_SRC) 
	@echo "Builds the main object file 'build/$(LIB_NAME).o' (CONFIG=$(CONFIG))..." 
	@$(MKDIR) $(BUILD_DIR)
	@$(AS) $(ASFLAGS) -o $@ $<
$(OBJECTS): $(ASM_SOURCES)
	@echo "Building submodules... (CONFIG=$(CONFIG))... "
	@$(foreach d,$(OBJ_LIST), \
	  (echo "\tBuild for $(d) ..." && $(MAKE) -C $(LIBS_DIR)/$(d) -s build CONFIG=release CFLAGS+=-Wl,-z,noexecstack) || echo "\n\t\t⚠️  $(d) no rule build\n"; \
	)	
$(BIN_DIR)/%: $(TESTS_DIR)/%.c $(OBJ) $(OBJECTS) | $(BIN_DIR)
	@$(CC) $(CFLAGS) $< $(OBJECTS) $(OBJ) -o $@ $(LDFLAGS) $(if $(filter %_mt,$*),-pthread)
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
	    $(TESTS_DIR)/ $(BENCH_DIR)/ $(DIST_DIR)/

clean:
	@echo "Cleaning up build artifacts (build/, bin/, dist/)..."
	@$(RM) $(BUILD_DIR) $(BIN_DIR) $(DIST_DIR)
	@echo "Cleaning up submodule artifacts:" ; 		
	@$(foreach d,$(OBJ_LIST), \
	  (printf "%s" "Clean for $(d) : " && $(MAKE) -C $(LIBS_DIR)/$(d) -s clean) || echo "\n\t\t⚠️  $(d) has no rule clean\n"; \
	)

help:
	@echo "Usage: make <target> [CONFIG=release] [REPORT_NAME=my_report]"
	@echo ""
	@echo "Main Targets:"
	@echo "  all/build    Builds the main object file 'build/bignum_shift_left.o'."
	@echo "  lint         Running static analysis on C source files"
	@echo "  test         Builds and runs all unit tests from the 'tests/' directory."
	@echo "  bench        Builds and runs performance benchmarks, generating named reports."
	@echo "  install      Packages the product into the 'dist/' directory for internal use."
	@echo "  dist         Packages the product into the 'dist/' directory for external use. (single-header, static-lib)"    
	@echo "  clean        Removes all temporary build files and the 'dist/' directory."
	@echo "  help         Shows this help message."
	@echo ""
	@echo "Optimization Cycle Example:"
	@echo "  1. make bench REPORT_NAME=baseline"
	@echo "  2. ...edit code..."
	@echo "  3. make test"
	@echo "  4. make bench REPORT_NAME=opt_v1"
	@echo "  5. diff -u benchmarks/reports/baseline_st.txt benchmarks/reports/opt_v1_st.txt"

# Тестовый таргет для вычисляемых переменных
.PHONY: show-calc
show-calc:
	@echo "REPOSITORY_NAME = $(REPOSITORY_NAME)"
	@echo "FAMILY_NAME = $(FAMILY_NAME)"	
	@echo "LIB_NAME = $(LIB_NAME)"
	@echo "UPPER_LIB_NAME = $(UPPER_LIB_NAME)"	
	@echo "NP = $(NP)"
	@echo "ASM_LABELS = $(ASM_LABELS)"
	@echo "Количество меток: $(words $(subst |, ,$(ASM_LABELS)))"
	@echo "OBJECTS = $(OBJECTS)"
	@echo "OBJ_LIST = $(OBJ_LIST)"	
	@echo "ASM_SOURCES = $(ASM_SOURCES)"
	@echo "HEADERS = $(HEADERS)"			
	@echo "OBJ = $(OBJ)"
	@echo "SUBMODULES_INCLUDE_DIR = $(SUBMODULES_INCLUDE_DIR)"	
	@echo "	$(foreach dir,$(SUBMODULES_INCLUDE_DIR),$(wildcard $(dir)/*.h))	"
