BUILD_DIR ?= build
PREFIX   ?= /usr/local
DESTDIR  ?=

CMAKE_ARGS  ?= -DCMAKE_BUILD_TYPE=Release
CMAKE_ARGS  += -DCMAKE_INSTALL_PREFIX=$(PREFIX)

.PHONY: all build install clean

all: build

build:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR) -j$$(nproc)

install: build
	env DESTDIR=$(DESTDIR) cmake --install $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
