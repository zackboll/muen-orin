CLANG ?= /usr/lib/llvm-19/bin/clang
LINK ?= /usr/lib/llvm-19/bin/lld-link
FLAGS = --target=aarch64-pc-windows-msvc -ffreestanding -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -O2
OUT ?= /tmp/muen-efi-inventory-build
all: $(OUT)/inventory.efi
$(OUT):
	mkdir -p $(OUT)
$(OUT)/%.obj: %.c | $(OUT)
	$(CLANG) $(FLAGS) -c $< -o $@
$(OUT)/current_el.obj: current_el.S | $(OUT)
	$(CLANG) $(FLAGS) -c $< -o $@
$(OUT)/inventory.efi: $(OUT)/core.obj $(OUT)/probe.obj $(OUT)/current_el.obj
	$(LINK) /machine:arm64 /subsystem:efi_application /entry:efi_main /nodefaultlib /dynamicbase /timestamp:0 /out:$@ $^
.PHONY: all test
test:
	gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g core.c probe.c tests.c -o /tmp/muen-efi-inventory-test
	/tmp/muen-efi-inventory-test
	python3 -c 'import json; r=json.load(open("/tmp/muen-efi-inventory-record.json")); assert r["schema"] == 1 and r["reported_level"] == 1 and r["boot_services_map_status"] == "observed"'
