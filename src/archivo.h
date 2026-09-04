#ifndef ARCHIVO_H
#define ARCHIVO_H

#include <sys/types.h>  // off_t, ssize_t
#include <stddef.h>     // size_t
#include "cadena.h"

/**
 * ============================================================================
 * MODULO: Archivo (capa de acceso a disco mediante llamadas al sistema)
 * ============================================================================
 * Este es el nucleo del parcial. Aqui el DISCO es la fuente de verdad: los
 * comandos del editor (a, d, i, x) modifican los bytes del archivo real usando
 * exclusivamente system calls POSIX -- open, read, write, lseek, ftruncate,
 * fstat y close -- sin pasar por stdio (nada de fopen/fread/fwrite/fclose).
 *
 * Para no releer el archivo entero en cada comando, mantenemos en RAM un
 * INDICE DE LINEAS: dos arreglos dinamicos (malloc/realloc) con el offset de
 * inicio y el largo de cada linea. El indice se reconstruye despues de cada
 * mutacion, de modo que RAM y disco jamas quedan desincronizados.
 *
 *   archivo.txt en disco:  "hola\nmundo\n"
 *                           ^     ^
 *   inicio[] =            { 0,    5 }
 *   largo[]  =            { 4,    5 }     (sin contar el '\n')
 */
typedef struct {
    int     fd;                // Descriptor devuelto por open(); -1 si esta cerrado
    char   *ruta;              // Copia dinamica de la ruta abierta
    off_t  *inicio;            // Offset absoluto donde empieza cada linea
    size_t *largo;             // Largo de cada linea, sin el '\n' terminador
    int     total;             // Cantidad de lineas indexadas
    size_t  capacidad;         // Capacidad reservada en los arreglos del indice
    int     termina_sin_salto; // 1 si el ultimo byte del archivo no es '\n'
    off_t   tamano;            // Tamano total en bytes segun el ultimo indexado
} Archivo;

/* --- Ciclo de vida --------------------------------------------------------*/
void archivo_iniciar(Archivo *a);
int  archivo_abrir(Archivo *a, const char *ruta);   // open(O_RDWR|O_CREAT, 0644)
int  archivo_esta_abierto(const Archivo *a);
int  archivo_indexar(Archivo *a);                   // lseek + read recorriendo bytes
void archivo_cerrar(Archivo *a);                    // close + free del indice

/* --- Lectura (comandos p, s, m) -------------------------------------------*/
char *archivo_leer_linea(Archivo *a, int n, size_t *largo_out); // devuelve malloc
int   archivo_imprimir_linea(Archivo *a, int n, Cadena *salida);
int   archivo_imprimir_todo(Archivo *a, Cadena *salida);
int   archivo_buscar(Archivo *a, const char *palabra, Cadena *salida);
int   archivo_metadatos(Archivo *a, Cadena *salida);            // fstat

/* --- Mutacion (comandos a, d, i, x) ---------------------------------------*/
int archivo_anexar(Archivo *a, const char *texto);              // lseek(SEEK_END)+write
int archivo_insertar_linea(Archivo *a, int n, const char *texto);
int archivo_borrar_linea(Archivo *a, int n);                    // + ftruncate

/* --- Utilidades de E/S robusta --------------------------------------------*/
/* write() y read() pueden atender menos bytes de los pedidos o ser
 * interrumpidos por una senal; estos envoltorios reintentan hasta completar. */
ssize_t escribir_todo(int fd, const void *buffer, size_t n);
ssize_t leer_todo(int fd, void *buffer, size_t n);

#endif
