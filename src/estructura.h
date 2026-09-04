#ifndef ESTRUCTURA_H
#define ESTRUCTURA_H

#include <stddef.h> // Para tipos base como size_t y constantes como NULL
#include <stdio.h>  // Para operaciones de entrada/salida estándar

/**
 * Estructura para representar una palabra (letras continuas o espacios).
 * Forma parte de una lista enlazada dentro de una línea.
 */
typedef struct NodoPalabra {
    char *texto;
    size_t longitud;
    size_t capacidad;
    struct NodoPalabra *anterior;
    struct NodoPalabra *siguiente;
} NodoPalabra;

/**
 * Estructura para representar una línea de texto individual.
 * Ahora contiene una lista ligada de palabras.
 */
typedef struct NodoLinea {
    NodoPalabra *palabras_cabeza;
    NodoPalabra *palabras_cola;
    size_t longitud;             // Longitud actual de la línea
    struct NodoLinea *anterior;  // Puntero a la línea anterior
    struct NodoLinea *siguiente; // Puntero a la línea siguiente
} NodoLinea;

/**
 * Estructura para representar el estructura de texto completo.
 */
typedef struct {
    NodoLinea *cabeza;         // Primera línea del texto
    NodoLinea *cola;           // Última línea del texto
    int totalLineas;           // Contador del total de líneas
} EstructuraTexto;

// Funciones
EstructuraTexto* estructura_crear();
void estructura_destruir(EstructuraTexto *alm);
NodoLinea* estructura_insertar_linea(EstructuraTexto *alm, NodoLinea *despues_de);
void estructura_eliminar_linea(EstructuraTexto *alm, NodoLinea *linea);
void linea_insertar_caracter(NodoLinea *linea, size_t pos, char c);
void linea_eliminar_caracter(NodoLinea *linea, size_t pos);
int estructura_guardar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out);
int estructura_cargar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out);

// Buscar palabra
int estructura_buscar_palabra(EstructuraTexto *alm, const char *palabra, NodoLinea **linea_encontrada, int *pos_x, int *pos_y);

#endif
