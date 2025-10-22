# Makefile para loadpulse.c (FreeBSD-friendly / POSIX)
# Uso:
#   make            # build release (default)
#   make debug      # build con -O0 -g
#   make asan       # build con sanitizers (clang)
#   make run        # ejecuta con flags por defecto
#   make install    # instala en /usr/local/bin
#   make clean

CC       ?= cc
TARGET    = loadpulse
SRC       = loadpulse.c

WARN     = -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wformat=2
THREAD   = -pthread
DEFS     = -D_GNU_SOURCE

CFLAGS_RELEASE = -O2 -pipe $(THREAD) $(WARN) $(DEFS)
CFLAGS_DEBUG   = -O0 -g   $(THREAD) $(WARN) $(DEFS)

# Sanitizers (clang)
CFLAGS_ASAN  = -O0 -g $(THREAD) $(WARN) $(DEFS) -fsanitize=address,undefined
LDFLAGS_ASAN = -fsanitize=address,undefined

# Instalación
PREFIX   ?= /usr/local
BINDIR    = $(PREFIX)/bin

# Opciones de ejecución por defecto
RUNOPTS ?= -t 4 -on 80 -off 20 -pin

.PHONY: all release debug asan run install uninstall clean

all: release

release: CFLAGS := $(CFLAGS_RELEASE)
release: $(TARGET)

debug:   CFLAGS := $(CFLAGS_DEBUG)
debug:   $(TARGET)

asan:    CFLAGS := $(CFLAGS_ASAN)
asan:    LDFLAGS += $(LDFLAGS_ASAN)
asan:    $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) $(RUNOPTS)

install: $(TARGET)
	install -d $(BINDIR)
	install -s $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	$(RM) $(BINDIR)/$(TARGET)

clean:
	$(RM) -f $(TARGET) *.o *.core
