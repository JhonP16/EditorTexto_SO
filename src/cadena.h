#ifndef CADENA_H
#define CADENA_H

#include <stddef.h>

/**
 * ============================================================================
 * MODULO: Cadena (Buffer de texto dinamico)
 * ============================================================================
 * El proyecto original usaba arreglos fijos (char buffer[4096]) para armar
 * texto. Eso desborda la pila apenas una linea supera los 4095 bytes.
 *
 * Este modulo implementa un buffer que crece solo con realloc(), duplicando
 * su capacidad cada vez que se llena. Es la "manipulacion de buffers dinamicos
 * en memoria (malloc/free)" que exige el reto tecnico del parcial, y lo usan
 * tanto la capa de disco (archivo.c) como la estructura en RAM (estructura.c).
 */
typedef struct {
    char  *datos;      // Bytes almacenados (siempre terminados en '\0')
    size_t longitud;   // Bytes usados, sin contar el '\0'
    size_t capacidad;  // Bytes reservados con malloc/realloc
} Cadena;

void cadena_iniciar(Cadena *c);
int  cadena_reservar(Cadena *c, size_t bytes_extra);
int  cadena_anexar(Cadena *c, const char *texto, size_t n);
int  cadena_anexar_str(Cadena *c, const char *texto);
int  cadena_printf(Cadena *c, const char *formato, ...);
void cadena_liberar(Cadena *c);

/* Entrega la propiedad del buffer interno al llamador (que debera liberarlo
 * con free) y deja la Cadena vacia. Devuelve NULL si nunca se escribio nada. */
char *cadena_entregar(Cadena *c);

/* Copia de una cadena C con malloc. Equivale a strdup, que no es C99. */
char *duplicar_cadena(const char *texto);

#endif
