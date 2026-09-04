#ifndef PORTAPAPELES_H
#define PORTAPAPELES_H

#include <stddef.h>
#include "cadena.h"

/**
 * ============================================================================
 * MODULO: Portapapeles secuencial (reto tecnico de los equipos de 3)
 * ============================================================================
 * Es una LISTA ENLAZADA SIMPLE de lineas copiadas. Se llama "secuencial"
 * porque conserva el orden en que se copiaron: cada 'y [n]' encola una linea
 * al final, y 'x [n]' las inserta todas en ese mismo orden.
 *
 *   y 3 ; y 1 ; x 5   ->  inserta primero la vieja linea 3 y luego la 1,
 *                         a partir de la linea 5.
 *
 * Mantener la cola ademas del cabeza hace que encolar sea O(1) en vez de O(n).
 */
typedef struct NodoCopia {
    char             *texto;      // Copia dinamica del contenido de la linea
    size_t            longitud;
    struct NodoCopia *siguiente;
} NodoCopia;

typedef struct {
    NodoCopia *cabeza;
    NodoCopia *cola;
    int        total;
} Portapapeles;

void portapapeles_iniciar(Portapapeles *p);
int  portapapeles_agregar(Portapapeles *p, const char *texto, size_t longitud);
void portapapeles_limpiar(Portapapeles *p);
int  portapapeles_listar(const Portapapeles *p, Cadena *salida);

#endif
