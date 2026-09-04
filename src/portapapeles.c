#include "portapapeles.h"
#include <stdlib.h>  // malloc, free
#include <string.h>  // memcpy
#include <stdio.h>   // perror

void portapapeles_iniciar(Portapapeles *p) {
    if (!p) return;
    p->cabeza = NULL;
    p->cola = NULL;
    p->total = 0;
}

/**
 * Encola una linea al final de la lista (comando y).
 * Guardamos una COPIA propia del texto: el buffer del llamador puede
 * liberarse o cambiar, y el portapapeles debe sobrevivir a eso.
 */
int portapapeles_agregar(Portapapeles *p, const char *texto, size_t longitud) {
    if (!p || !texto) return -1;

    NodoCopia *nodo = (NodoCopia *)malloc(sizeof(NodoCopia));
    if (!nodo) {
        perror("malloc (nodo de portapapeles)");
        return -1;
    }

    nodo->texto = (char *)malloc(longitud + 1);
    if (!nodo->texto) {
        perror("malloc (texto de portapapeles)");
        free(nodo);
        return -1;
    }
    memcpy(nodo->texto, texto, longitud);
    nodo->texto[longitud] = '\0';
    nodo->longitud = longitud;
    nodo->siguiente = NULL;

    if (!p->cabeza) p->cabeza = nodo;
    else            p->cola->siguiente = nodo;
    p->cola = nodo;
    p->total++;
    return 0;
}

/**
 * Libera toda la lista (comando yc y salida del programa).
 * Se guarda 'siguiente' ANTES de liberar el nodo: leerlo despues del free
 * seria un uso de memoria liberada.
 */
void portapapeles_limpiar(Portapapeles *p) {
    if (!p) return;
    NodoCopia *actual = p->cabeza;
    while (actual) {
        NodoCopia *siguiente = actual->siguiente;
        free(actual->texto);
        free(actual);
        actual = siguiente;
    }
    portapapeles_iniciar(p);
}

int portapapeles_listar(const Portapapeles *p, Cadena *salida) {
    if (!p || !salida) return -1;
    if (p->total == 0) {
        cadena_anexar_str(salida, "El portapapeles esta vacio (use 'y [n]' para copiar).\n");
        return 0;
    }
    cadena_printf(salida, "==== PORTAPAPELES SECUENCIAL (%d linea(s)) ====\n", p->total);
    int i = 1;
    for (NodoCopia *actual = p->cabeza; actual; actual = actual->siguiente, i++)
        cadena_printf(salida, "%4d | %s\n", i, actual->texto);
    return p->total;
}
