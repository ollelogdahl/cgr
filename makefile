ODIR = .target
ODIR_REL = ${ODIR}/release

CXX = clang++

USE_MOLD_LINKER = false

HFILES_SRC = $(shell find src/ -type f -name *.h)
CCFILES_SRC = $(shell find src/ -type f -name *.cc)

HFILES_VENDOR = $(shell find vendor/ -type f -name *.h)
CCFILES_VENDOR = $(shell find vendor/ -type f -name *.cc)

CC_OFILES_SRC_RELEASE = $(patsubst src/%.cc,${ODIR_REL}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_RELEASE = $(patsubst vendor/%.cc,${ODIR_REL}/vendor/%.cc.o,$(CCFILES_VENDOR))

OFILES_RELEASE = $(CC_OFILES_VENDOR_RELEASE) $(CC_OFILES_SRC_RELEASE)

CCFLAGS_COMMON = -std=c++2a -Wall -Wextra -Werror -Ivendor/include -Isrc -I. -DDEBUG -g -fsanitize=undefined \
    -Wno-nullability-completeness -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-private-field \
    -Wno-missing-field-initializers
LDFLAGS_COMMON = -ldl -lglfw -lvulkan

CCFLAGS_RELEASE = $(CCFLAGS_COMMON) -O1

LDFLAGS_RELEASE = $(LDFLAGS_COMMON)

ifeq ($(USE_MOLD_LINKER),true)
LDFLAGS_COMMON += --ld-path=/usr/bin/mold
endif

.PHONY: clean

cgr: $(ODIR_REL)/cgr
	@cp $(ODIR_REL)/cgr cgr

clean:
	@rm -rf $(ODIR_REL)/chr ${ODIR_REL}/src

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
