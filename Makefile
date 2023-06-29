prefix := /usr/local
install-runner := sudo

pc-libs := sqlite3

CXXFLAGS := $(CXXFLAGS) -MMD -MP -Wall -Wextra -Werror -Wtype-limits -Wpedantic -pedantic-errors \
	   -std=c++20 -D_GNU_SOURCE -march=native -pipe -Isrc \
	   $(shell pkg-config --cflags $(pc-libs))
CXXFLAGS_release := -DNDEBUG -O3 -flto
CXXFLAGS_debug := -ggdb3 -fsanitize=address -fsanitize=undefined -D_GLIBCXX_DEBUG

LDFLAGS := $(LDFLAGS) -pipe
LDFLAGS_release := -flto -s
LDFLAGS_debug := -fsanitize=address -fsanitize=undefined
LDLIBS := -lstdc++ $(shell pkg-config --libs $(pc-libs))

MAKEFLAGS := -j $(shell nproc)
.DEFAULT_GOAL := debug

prog := gconsentfox
modes := debug release
cppfiles := $(wildcard src/*.cpp)
objects := $(notdir $(cppfiles:.cpp=.o))
build_dirs := $(addprefix build_,$(modes))

$(build_dirs):
	mkdir -p $@

define template
objects_$(1) := $$(addprefix build_$(1)/,$$(objects))
$$(objects_$(1)): CXXFLAGS += $$(CXXFLAGS_$(1))
$$(objects_$(1)): | build_$(1)
build_$(1)/%.o: src/%.cpp
	$$(COMPILE.cc) -o $$@ $$<
-include $$(objects_$(1):.o=.d)
build_$(1)/$$(prog): LDFLAGS += $$(LDFLAGS_$(1))
build_$(1)/$$(prog): $$(objects_$(1))
	   $$(LINK.o) -o $$@ $$(LDLIBS) $$^
$(1): build_$(1)/$$(prog)
endef

$(foreach mode,$(modes),$(eval $(call template,$(mode))))

all: $(modes)

clean:
	$(RM) -r $(build_dirs)

compile_commands.json:
	bear -- $(MAKE) -B debug

install: build_release/$(prog)
	$(install-runner) install -D -t $(DESTDIR)$(prefix)/bin $<

uninstall:
	$(install-runner) $(RM) $(DESTDIR)$(prefix)/bin/$(prog)

.PHONY: all clean compile_commands.json install uninstall $(modes)
.DELETE_ON_ERROR:
