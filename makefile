ODIR = .target
ODIR_DEV = ${ODIR}/dev
ODIR_REL = ${ODIR}/release
ODIR_MEM = ${ODIR}/memcheck

CXX = clang++

USE_MOLD_LINKER = false

HFILES_SRC = $(shell find src/ -type f -name *.h)
CCFILES_SRC = $(shell find src/ -type f -name *.cc)

HFILES_VENDOR = $(shell find vendor/ -type f -name *.h)
CCFILES_VENDOR = $(shell find vendor/ -type f -name *.cc)

CC_OFILES_SRC_DEV = $(patsubst src/%.cc,${ODIR_DEV}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_DEV = $(patsubst vendor/%.cc,${ODIR_DEV}/vendor/%.cc.o,$(CCFILES_VENDOR))
C_OFILES_VENDOR_DEV = $(patsubst vendor/%.c,${ODIR_DEV}/vendor/%.c.o,$(CFILES_VENDOR))

CC_OFILES_SRC_RELEASE = $(patsubst src/%.cc,${ODIR_REL}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_RELEASE = $(patsubst vendor/%.cc,${ODIR_REL}/vendor/%.cc.o,$(CCFILES_VENDOR))
C_OFILES_VENDOR_RELEASE = $(patsubst vendor/%.c,${ODIR_REL}/vendor/%.c.o,$(CFILES_VENDOR))

CC_OFILES_SRC_MEM = $(patsubst src/%.cc,${ODIR_MEM}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_MEM = $(patsubst vendor/%.cc,${ODIR_MEM}/vendor/%.cc.o,$(CCFILES_VENDOR))
C_OFILES_VENDOR_MEM = $(patsubst vendor/%.c,${ODIR_MEM}/vendor/%.c.o,$(CFILES_VENDOR))

CCFILES_TRACY = vendor/tracy/TracyClient.cpp
CC_OFILES_TRACY = $(ODIR_DEV)/vendor/TracyClient.cpp.o

OFILES_DEV = $(CC_OFILES_TRACY) $(CC_OFILES_VENDOR_DEV) $(CC_OFILES_SRC_DEV) $(C_OFILES_VENDOR_DEV)
OFILES_RELEASE = $(CC_OFILES_VENDOR_RELEASE) $(CC_OFILES_SRC_RELEASE) $(C_OFILES_VENDOR_RELEASE)
OFILES_MEM = $(CC_OFILES_VENDOR_MEM) $(CC_OFILES_SRC_MEM) $(C_OFILES_VENDOR_MEM)

CCFLAGS_COMMON = -std=c++2a -Wall -Wextra -Werror -Ivendor/include -Isrc -I. -g \
	-Wno-nullability-completeness -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-private-field \
	-Wno-missing-field-initializers \
	-march=native					\
	-DVK_USE_PLATFORM_XCB_KHR		\
	-DVK_NO_PROTOTYPES				\
	-Ivendor/tracy
LDFLAGS_COMMON = -ldl -lglfw -lz -pthread

CCFLAGS_DEV = $(CCFLAGS_COMMON) -DDEBUG -O3 -DTRACY_ENABLE -DTRACY_CALLSTACK=2
CCFLAGS_RELEASE = $(CCFLAGS_COMMON) -DRELEASE -O3
CCFLAGS_MEM = $(CCFLAGS_COMMON) -DVALIDATE -O0  -fsanitize=address

CCFLAGS_VENDOR_DEV = $(CCFLAGS_COMMON) -DDEBUG -O3
CCFLAGS_VENDOR_RELEASE = $(CCFLAGS_COMMON) -DRELEASE -O3
CCFLAGS_VENDOR_MEM = $(CCFLAGS_COMMON) -DVALIDATE -O0

LDFLAGS_DEV = $(LDFLAGS_COMMON)
LDFLAGS_RELEASE = $(LDFLAGS_COMMON)
LDFLAGS_MEM = $(LDFLAGS_COMMON) -fsanitize=address

ifeq ($(USE_MOLD_LINKER),true)
LDFLAGS_COMMON += --ld-path=/usr/bin/mold
endif

.PHONY: clean dev release memcheck

dev: $(ODIR_DEV)/cgr
	@cp $(ODIR_DEV)/cgr cgr

release: $(ODIR_REL)/cgr
	@cp $(ODIR_REL)/cgr cgr

memcheck: $(ODIR_MEM)/cgr
	@cp $(ODIR_MEM)/cgr cgr

clean:
	@rm -rf cgr $(ODIR_DEV)/cgr $(ODIR_DEV)/src $(ODIR_REL)/cgr $(ODIR_REL)/src $(ODIR_MEM)/cgr $(ODIR_MEM)/src

$(ODIR_DEV)/cgr: $(OFILES_DEV)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(CXX) -o $@ $^ $(CCFLAGS_DEV) $(LDFLAGS_DEV)

$(ODIR_DEV)/src/%.cc.o: src/%.cc $(HFILES_VENDOR) $(HFILES_SRC)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_DEV)

$(ODIR_DEV)/vendor/%.cc.o: vendor/%.cc $(HFILES_VENDOR)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_VENDOR_DEV)

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
	@$(CXX) -c -o $@ $< $(CCFLAGS_VENDOR_RELEASE)

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
	@$(CXX) -c -o $@ $< $(CCFLAGS_VENDOR_MEM)

$(CC_OFILES_TRACY): vendor/tracy/TracyClient.cpp
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CXX) -c -o $@ $< $(CCFLAGS_DEV)
