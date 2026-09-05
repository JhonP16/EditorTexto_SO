CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE
ARCHSALIDA = eafitOS
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_aplicaciones.c
OBJS = $(SRCS:.c=.o)

all: $(ARCHSALIDA)

# Construye el shell SIN lanzarlo (el target 'all' original lo ejecuta al
# terminar de enlazar). Necesario para compilar desde un guion de pruebas.
compilar: $(OBJS)
	$(CC) $(CFLAGS) -o $(ARCHSALIDA) $(OBJS)

$(ARCHSALIDA): $(OBJS)
	$(CC) $(CFLAGS) -o $(ARCHSALIDA) $(OBJS)
	./$(ARCHSALIDA)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(ARCHSALIDA) $(OBJS)   # el original decía $(TARGET), variable inexistente

.PHONY: all clean compilar
