# All outputs go below OUT (default /tmp); nothing is written to the tree.
CLANG ?= /usr/lib/llvm-19/bin/clang
LINK ?= /usr/lib/llvm-19/bin/lld-link
HOST_CC ?= gcc
HOST_CLANG ?= $(CLANG)
PYTHON ?= python3
DTC ?= dtc
FLAGS = --target=aarch64-pc-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -O2
# -MMD -MP: each object gets a .d file listing the headers it includes, so
# changing efi.h or core.h rebuilds the affected objects and relinks.
DEPFLAGS = -MMD -MP
OUT ?= /tmp/muen-efi-inventory-build
TEST_OUT ?= $(OUT)/test
OBJS = $(OUT)/core.obj $(OUT)/probe.obj $(OUT)/current_el.obj
HOST_SRC = core.c probe.c tests.c
HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -DPROBE_HOST -g
SAN = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

all: $(OUT)/inventory.efi
$(OUT) $(TEST_OUT):
	mkdir -p $@
$(OUT)/%.obj: %.c | $(OUT)
	$(CLANG) $(FLAGS) $(DEPFLAGS) -c $< -o $@
$(OUT)/current_el.obj: current_el.S | $(OUT)
	$(CLANG) $(FLAGS) $(DEPFLAGS) -c $< -o $@
$(OUT)/inventory.efi: $(OBJS)
	$(LINK) /machine:arm64 /subsystem:efi_application /entry:efi_main /nodefaultlib /dynamicbase /timestamp:0 /out:$@ $(OBJS)
-include $(OBJS:.obj=.d)

# Host doubles: sanitizers with GCC and Clang at -O0 and -O2, plus a build
# with a deliberately small record buffer for the truncation path. Host
# tests are phony and always rebuild. (Explicit rules: GNU make does not
# apply pattern rules to .PHONY targets.)
TEST_VARIANTS = gcc-O0 gcc-O2 clang-O0 clang-O2
compiler_gcc = $(HOST_CC)
compiler_clang = $(HOST_CLANG)
.PHONY: all test clean fixtures-check $(addprefix test-,$(TEST_VARIANTS)) test-small
test: $(addprefix test-,$(TEST_VARIANTS)) test-small
define host_test
test-$(1)-$(2): | $$(TEST_OUT)
	$$(compiler_$(1)) $$(HOST_FLAGS) -$(2) $$(SAN) $$(HOST_SRC) -o $$(TEST_OUT)/$(1)-$(2)
	rm -rf $$(TEST_OUT)/$(1)-$(2).records && mkdir -p $$(TEST_OUT)/$(1)-$(2).records
	$$(TEST_OUT)/$(1)-$(2) $$(TEST_OUT)/$(1)-$(2).records
	$$(PYTHON) check_records.py $$(TEST_OUT)/$(1)-$(2).records
endef
$(foreach c,gcc clang,$(foreach o,O0 O2,$(eval $(call host_test,$(c),$(o)))))
test-small: | $(TEST_OUT)
	$(HOST_CC) $(HOST_FLAGS) -O2 $(SAN) -DRECORD_CAPACITY=512 -DSMALL_RECORD_TEST $(HOST_SRC) -o $(TEST_OUT)/small
	rm -rf $(TEST_OUT)/small.records && mkdir -p $(TEST_OUT)/small.records
	$(TEST_OUT)/small $(TEST_OUT)/small.records t_record_truncated
	$(PYTHON) check_records.py $(TEST_OUT)/small.records --small
# Regenerate fixtures.h with an external dtc and require no difference.
fixtures-check: | $(TEST_OUT)
	$(PYTHON) gen_fixtures.py --dtc $(DTC) --out $(TEST_OUT)/fixtures.h
	cmp fixtures.h $(TEST_OUT)/fixtures.h
clean:
	rm -rf $(OUT)
