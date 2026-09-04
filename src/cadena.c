#include "cadena.h"
#include <stdlib.h>  // malloc, realloc, free
#include <string.h>  // strlen, memcpy
#include <stdio.h>   // vsnprintf, perror
#include <stdarg.h>  // va_list para cadena_printf

#define CADENA_CAPACIDAD_INICIAL 128

void cadena_iniciar(Cadena *c) {
    if (!c) return;
    c->datos = NULL;
    c->longitud = 0;
    c->capacidad = 0;
}

/**
 * Garantiza que quepan 'bytes_extra' bytes mas (mas el '\0' final).
 * Estrategia: duplicar la capacidad hasta que alcance. Amortizado O(1).
 * Retorna 0 si hay espacio, -1 si el kernel no pudo darnos memoria.
 */
int cadena_reservar(Cadena *c, size_t bytes_extra) {
    if (!c) return -1;
    size_t necesaria = c->longitud + bytes_extra + 1;
    if (necesaria <= c->capacidad) return 0;

    size_t nueva = c->capacidad ? c->capacidad : CADENA_CAPACIDAD_INICIAL;
    while (nueva < necesaria) nueva *= 2;

    char *bloque = (char *)realloc(c->datos, nueva);
    if (!bloque) {
        perror("realloc (cadena_reservar)");
        return -1;
    }
    c->datos = bloque;
    c->capacidad = nueva;
    return 0;
}

int cadena_anexar(Cadena *c, const char *texto, size_t n) {
    if (!c || !texto) return -1;
    if (n == 0) return cadena_reservar(c, 0);   // fuerza un buffer valido
    if (cadena_reservar(c, n) == -1) return -1;
    memcpy(c->datos + c->longitud, texto, n);
    c->longitud += n;
    c->datos[c->longitud] = '\0';
    return 0;
}

int cadena_anexar_str(Cadena *c, const char *texto) {
    if (!texto) return -1;
    return cadena_anexar(c, texto, strlen(texto));
}

/**
 * Version con formato. Se llama a vsnprintf dos veces: la primera para que el
 * sistema nos diga cuantos bytes necesita, la segunda para escribirlos ya con
 * el espacio reservado. Asi nunca truncamos por un tamano adivinado.
 */
int cadena_printf(Cadena *c, const char *formato, ...) {
    if (!c || !formato) return -1;

    va_list args;
    va_start(args, formato);
    int necesarios = vsnprintf(NULL, 0, formato, args);
    va_end(args);

    if (necesarios < 0) return -1;
    if (cadena_reservar(c, (size_t)necesarios) == -1) return -1;

    va_start(args, formato);
    vsnprintf(c->datos + c->longitud, (size_t)necesarios + 1, formato, args);
    va_end(args);

    c->longitud += (size_t)necesarios;
    return 0;
}

void cadena_liberar(Cadena *c) {
    if (!c) return;
    free(c->datos);
    c->datos = NULL;
    c->longitud = 0;
    c->capacidad = 0;
}

char *cadena_entregar(Cadena *c) {
    if (!c) return NULL;
    char *datos = c->datos;
    c->datos = NULL;
    c->longitud = 0;
    c->capacidad = 0;
    return datos;
}

char *duplicar_cadena(const char *texto) {
    if (!texto) return NULL;
    size_t n = strlen(texto);
    char *copia = (char *)malloc(n + 1);
    if (!copia) {
        perror("malloc (duplicar_cadena)");
        return NULL;
    }
    memcpy(copia, texto, n + 1);
    return copia;
}
