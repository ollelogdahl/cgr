ODIR = .target
ODIR_REL = ${ODIR}/release

HFILES_SRC = $(shell find src/ -type f -name *.h)
CCFILES_SRC = $(shell find src/ -type f -name *.cc)

HFILES_VENDOR = $(shell find vendor/ -type f -name *.h)
CCFILES_VENDOR = $(shell find vendor/ -type f -name *.cc)

CC_OFILES_SRC_RELEASE = $(patsubst src/%.cc,${ODIR_REL}/src/%.cc.o,$(CCFILES_SRC))
CC_OFILES_VENDOR_RELEASE = $(patsubst vendor/%.cc,${ODIR_REL}/vendor/%.cc.o,$(CCFILES_VENDOR))

OFILES_RELEASE = $(CC_OFILES_SRC_RELEASE) $(CC_OFILES_VENDOR_RELEASE)

CCFLAGS_COMMON = -std=c++20 -Wall -Wextra -Werror -Ivendor/include -Isrc
LDFLAGS_COMMON = -lglfw -lvulkan

CCFLAGS_RELEASE = $(CCFLAGS_COMMON) -O3

LDFLAGS_RELEASE = $(LDFLAGS_COMMON)

cgr: $(ODIR_REL)/cgr
	@cp $(ODIR_REL)/cgr cgr

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
