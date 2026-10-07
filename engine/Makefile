TARGET = BO3MacFix.dylib

DEBUG   ?= 0
ARCH    ?= x86_64
SDK     ?= macosx
OSXVER  ?= 11.0

SYSROOT  := $(shell xcrun --sdk $(SDK) --show-sdk-path)
ifeq ($(SYSROOT),)
$(error Could not find SDK "$(SDK)")
endif
CLANG    := clang
CC       := $(CLANG) -isysroot $(SYSROOT) -arch $(ARCH) -mmacosx-version-min=$(OSXVER)

CFLAGS  = -O1 -Wall -I include/ -I src/ -fvisibility=hidden -ffile-prefix-map=$(CURDIR)=.
LDFLAGS = -dynamiclib -Llib -ldobby -ltomcrypt -ltommath -lc++

FRAMEWORKS = -framework CoreFoundation -framework IOKit -framework AppKit -framework AudioToolbox -framework CoreAudio

SOURCES = source/bo3macfix.c source/bo3macnative.m source/utilities.c source/steam.c source/exports.c source/network.c source/t7_protection.c source/t7_hooks.c source/steam_extra.c source/t7_log.c source/t7_lobby.c source/audio_fix.c source/boiii_probe.c source/boiii_client.c
CXX_SOURCES = source/t7_resolver.cpp source/t7_lua.cpp
CXX_OBJECTS = $(CXX_SOURCES:source/%.cpp=build/%.o)

all: $(TARGET)

$(TARGET): $(CXX_OBJECTS) $(SOURCES) $(wildcard source/*.h source/steam/*.h) lib/libdobby.a Makefile
	$(CC) $(CFLAGS) $(FRAMEWORKS) $(DEFINES) $(LDFLAGS) -o $@ $(SOURCES) $(CXX_OBJECTS)

build/%.o: source/%.cpp $(wildcard source/*.h) Makefile
	@mkdir -p build
	$(CC) -std=c++17 $(CFLAGS) -c $< -o $@

clean:
	rm -f -- $(TARGET) $(CXX_OBJECTS)

.PHONY: all clean test
test:
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I source tests/network_test.c source/network.c -o build/network_test
	./build/network_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined -I source tests/steam_test.c source/steam.c -o build/steam_test
	./build/steam_test
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined -I source tests/utilities_test.c source/utilities.c -o build/utilities_test
	./build/utilities_test
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I source tests/t7_protection_test.c source/t7_protection.c -o build/t7_protection_test
	./build/t7_protection_test
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I source tests/steam_extra_test.c source/steam_extra.c source/t7_protection.c -o build/steam_extra_test
	./build/steam_extra_test
	$(CC) -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I source tests/t7_lua_test.cpp -lc++ -o build/t7_lua_test
	./build/t7_lua_test
	$(CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I source tests/t7_lobby_test.c -o build/t7_lobby_test
	./build/t7_lobby_test
	$(CC) -std=c11 -g -fsanitize=address,undefined -I source -c source/utilities.c -o build/test_utilities.o
	$(CC) -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I source tests/t7_resolver_test.cpp source/t7_resolver.cpp build/test_utilities.o -lc++ -o build/t7_resolver_test
	./build/t7_resolver_test
	./build/t7_resolver_test bad-hash
	$(CC) -std=c11 -Wall -g -I source tests/dobby_prologue_test.c -Llib -ldobby -lc++ -o build/dobby_prologue_test
	./build/dobby_prologue_test
