# ============================================================================
# Makefile del Editor de Texto (Sistemas Operativos - EAFIT)
#   make        -> compila el binario ./editor
#   make test   -> compila y ejecuta el script de pruebas
#   make clean  -> borra objetos y binario
# ============================================================================
CC       = gcc

# -Wall -Wextra -pedantic : la rubrica exige compilacion limpia.
# -std=c99                : estandar del curso.
# _DEFAULT_SOURCE / _POSIX_C_SOURCE : con -std=c99 el compilador activa
#   __STRICT_ANSI__ y glibc esconde los prototipos POSIX que necesitamos
#   (ftruncate, fstat, isatty, TIOCGWINSZ). Estas macros los vuelven visibles.
CFLAGS   = -Wall -Wextra -pedantic -std=c99 -g \
           -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L

SRC_DIR  = src
OBJ_DIR  = obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
EXEC = editor

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $@

test: $(EXEC)
	bash pruebas.sh

clean:
	rm -rf $(OBJ_DIR) $(EXEC)

.PHONY: all clean test
