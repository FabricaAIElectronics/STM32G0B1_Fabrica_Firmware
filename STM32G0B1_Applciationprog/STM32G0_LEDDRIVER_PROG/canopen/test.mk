# Testing stuff
.SECONDARY:
BUILD_DIR = build
BIN_DIR = bin

TEST_BUILD_DIR = $(BUILD_DIR)/test
TEST_BIN_DIR = $(BIN_DIR)/test

MOCK_DIR = mocks
MOCK_CSRCS = $(wildcard $(MOCK_DIR)/*.c)
MOCK_NAMES := $(patsubst $(MOCK_DIR)/mock_%, %, $(MOCK_CSRCS))
MOCK_INC = -I$(MOCK_DIR)
MOCK_OBJS = $(addprefix $(TEST_BUILD_DIR)/, $(notdir $(MOCK_CSRCS:.c=.o)))

CMOCK_DIR = /root/cmock/src
CMOCK_CSRCS = $(wildcard $(CMOCK_DIR)/*.c)
CMOCK_INC = -I$(CMOCK_DIR)
CMOCK_OBJ = $(addprefix $(TEST_BUILD_DIR)/, $(notdir $(CMOCK_CSRCS:.c=.o)))

UNITY_DIR ?= /root/cmock/vendor/unity/src
UNITY_INC = -I$(UNITY_DIR)
UNITY_SRC = $(UNITY_DIR)/unity.c
UNITY_OBJ = $(addprefix $(TEST_BUILD_DIR)/, $(notdir $(UNITY_SRC:.c=.o)))

# change the PROG_SRC to point to the Src folder of our code
PROG_SRC := Src
# remove the things that are mocked
PROG_CSRCS := $(foreach src, $(wildcard $(PROG_SRC)/**/*.c) $(wildcard $(PROG_SRC)/*.c), \
			  $(if $(filter $(notdir $(src)), $(MOCK_NAMES)),,$(src)))
PROG_OBJS := $(addprefix $(TEST_BUILD_DIR)/, $(notdir $(PROG_CSRCS:.c=.o)))
PROG_INCLUDE = -IInc

NATIVE_TEST_CSRCS = $(wildcard test/native/**/test_*.c)
NATIVE_TEST_CSRCS += $(wildcard test/native/test_*.c)
NATIVE_TEST_TARGETS = $(patsubst %.c,%, $(notdir $(NATIVE_TEST_CSRCS)))
NATIVE_TEST_OBJS = $(addprefix $(TEST_BUILD_DIR)/,$(notdir $(NATIVE_TEST_CSRCS:.c=.o)))

ALL_SRCS := $(UNITY_SRC) $(NATIVE_TEST_CSRCS) $(PROG_CSRCS) $(MOCK_CSRCS) $(CMOCK_CSRCS)
ALL_OBJ = $(UNITY_OBJ) $(PROG_OBJS) $(MOCK_OBJS) $(CMOCK_OBJ)
ALL_INC = $(MOCK_INC) $(UNITY_INC) $(CMOCK_INC) $(PROG_INCLUDE)

# Compiler stuff
NATIVE_CC = gcc

COLOR_RED = \e[0;31m
COLOR_GREEN = \e[0;32m
COLOR_WHITE = \e[0;37m

# vpath %.c
vpath %.c $(sort $(dir $(ALL_SRCS)))

.PHONY: run_native_test
run_native_test:
	@for test in $(addprefix $(TEST_BIN_DIR)/, $(NATIVE_TEST_TARGETS)); do \
		echo "Running $$test..."; \
		echo -n "$(COLOR_RED)"; \
		($$test | grep -e FAIL) || echo "$(COLOR_GREEN)OK";\
		echo "$(COLOR_WHITE)"; \
	done

.PHONY: build_native_test
build_native_test: $(addprefix $(TEST_BIN_DIR)/, $(NATIVE_TEST_TARGETS)) $(ALL_OBJ)

$(TEST_BIN_DIR)/%: $(TEST_BUILD_DIR)/%.o $(ALL_OBJ) test.mk | $(TEST_BIN_DIR)
	$(NATIVE_CC) -o $@ $< $(ALL_OBJ)

$(TEST_BUILD_DIR)/%.o: %.c test.mk | $(TEST_BUILD_DIR)
	$(NATIVE_CC) -Wall -Og -g -DNATIVE_TEST $(ALL_INC) -c $< -o $@

# $(UNITY_OBJ): $(UNITY_SRC) test.mk | $(TEST_BUILD_DIR)
# 	$(NATIVE_CC) -Wall -Og -I$(UNITY_INC) -c $< -o $@

$(UNITY_OBJ): $(UNITY_SRC) test.mk | $(TEST_BUILD_DIR)
	$(NATIVE_CC) -Wall -Og -g $(UNITY_INC) -c $< -o $@

$(CMOCK_OBJ): $(CMOCK_CSRCS) test.mk | $(TEST_BUILD_DIR)
	$(NATIVE_CC) -Wall -Og -g $(CMOCK_INC) $(UNITY_INC) -c $< -o $@

$(TEST_BIN_DIR):
	@mkdir -p $@

$(TEST_BUILD_DIR):
	@mkdir -p $@
