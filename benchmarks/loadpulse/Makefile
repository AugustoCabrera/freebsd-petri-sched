# Makefile — loadpulse (FreeBSD / bmake, con fallback de pthread)
# Uso:
#   make            # build (release por defecto)
#   make debug      # build con -O0 -g
#   make asan       # build con sanitizers (clang)
#   make run        # ejecuta con flags por defecto
#   make install    # instala en /usr/local/bin
#   make clean

.PHONY: all release debug asan run install uninstall clean
.SUFFIXES:

CC        ?= cc
RM        ?= rm
TARGET     = loadpulse
SRC        = loadpulse.c

# Flags comunes
STD        = -std=c11
WARN       = -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wformat=2

# Perfiles
CFLAGS_RELEASE = -O2 -pipe $(STD) $(WARN)
CFLAGS_DEBUG   = -O0 -g   $(STD) $(WARN)
CFLAGS_ASAN    = -O0 -g   $(STD) $(WARN) -fsanitize=address,undefined

# LDFLAGS comunes
LDFLAGS_COMMON =
LDFLAGS_ASAN   = -fsanitize=address,undefined

# Instalación
PREFIX    ?= /usr/local
BINDIR     = $(PREFIX)/bin

# Opciones por defecto para 'make run'
RUNOPTS   ?= -t 4 -on 80 -off 20 -pin

all: release

release: CFLAGS := $(CFLAGS_RELEASE)
release: LDFLAGS := $(LDFLAGS_COMMON)
release: $(TARGET)

debug:   CFLAGS := $(CFLAGS_DEBUG)
debug:   LDFLAGS := $(LDFLAGS_COMMON)
debug:   $(TARGET)

asan:    CFLAGS := $(CFLAGS_ASAN)
asan:    LDFLAGS := $(LDFLAGS_COMMON) $(LDFLAGS_ASAN)
asan:    $(TARGET)

# Regla de build con fallback:
# 1) intenta con -pthread (compilación y link)
# 2) si falla, reintenta con -lpthread explícito
$(TARGET): $(SRC)
	@echo "==> Building $(TARGET)"
	@set -e; \
	if $(CC) $(CFLAGS) -pthread $(SRC) -o $(TARGET) $(LDFLAGS); then \
	  echo "OK: linked with -pthread"; \
	else \
	  echo "Retrying link with -lpthread..."; \
	  $(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) -lpthread; \
	fi

run: $(TARGET)
	./$(TARGET) $(RUNOPTS)

install: $(TARGET)
	install -d $(BINDIR)
	install -s $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	$(RM) -f $(BINDIR)/$(TARGET)

clean:
	$(RM) -f $(TARGET) *.o *.core
