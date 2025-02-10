ODIR = .target
ODIR_REL = ${ODIR}/release
ODIR_MEM = ${ODIR}/memcheck

CXX = clang++

USE_MOLD_LINKER = false

HFILES_SRC = $(shell find src/ -type f -name *.h)
CCFILES_SRC = $(shell find src/ -type f -name *.cc)

HFILES_VENDOR = $(shell find vendor/ -type f -name *.h)
CCFILES_VENDOR = $(shell find vendor/ -type f -name *.cc)

CC_OFILES_SRC_RELEASE = $(patsubst src/%.cc,${ODIR_REL}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_RELEASE = $(patsubst vendor/%.cc,${ODIR_REL}/vendor/%.cc.o,$(CCFILES_VENDOR))

CC_OFILES_SRC_MEM = $(patsubst src/%.cc,${ODIR_MEM}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_MEM = $(patsubst vendor/%.cc,${ODIR_MEM}/vendor/%.cc.o,$(CCFILES_VENDOR))

OFILES_RELEASE = $(CC_OFILES_VENDOR_RELEASE) $(CC_OFILES_SRC_RELEASE)
OFILES_MEM = $(CC_OFILES_VENDOR_MEM) $(CC_OFILES_SRC_MEM)

CCFLAGS_COMMON = -std=c++2a -Wall -Wextra -Werror -Ivendor/include -Isrc -I. -DDEBUG -g \
	-Wno-nullability-completeness -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-private-field \
	-Wno-missing-field-initializers
LDFLAGS_COMMON = -ldl -lglfw -lvulkan

CCFLAGS_RELEASE = $(CCFLAGS_COMMON) -O0
CCFLAGS_MEM = $(CCFLAGS_COMMON) -O0 -g -fsanitize=undefined -fsanitize=address

LDFLAGS_RELEASE = $(LDFLAGS_COMMON)
LDFLAGS_MEM = $(LDFLAGS_COMMON) -fsanitize=undefined -fsanitize=address

ifeq ($(USE_MOLD_LINKER),true)
LDFLAGS_COMMON += --ld-path=/usr/bin/mold
endif

.PHONY: clean memcheck

cgr: $(ODIR_REL)/cgr
	@cp $(ODIR_REL)/cgr cgr

memcheck: $(ODIR_MEM)/cgr
	@cp $(ODIR_MEM)/cgr cgr

clean:
	@rm -rf cgr $(ODIR_REL)/cgr $(ODIR_REL)/src $(ODIR_MEM)/cgr $(ODIR_MEM)/src

$(ODIR_REL)/cgr: $(OFILES_RELEASE)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(CXX) -o $@ $^ $(CCFLAGS_RELEASE) $(LDFLAGS_RELEASE)

$(ODIR_REL)/src/%.cc.o: src/%.cc $(HFILES_VENDOR) $(HFILES_SRC)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_RELEASE)

$(ODIR_REL)/vendor/%.cc.o: vendor/%.cc $(HFILES_VENDOR)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_RELEASE)

$(ODIR_MEM)/cgr: $(OFILES_MEM)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(CXX) -o $@ $^ $(CCFLAGS_MEM) $(LDFLAGS_MEM)

$(ODIR_MEM)/src/%.cc.o: src/%.cc $(HFILES_VENDOR) $(HFILES_SRC)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_MEM)

$(ODIR_MEM)/vendor/%.cc.o: vendor/%.cc $(HFILES_VENDOR)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_MEM)
