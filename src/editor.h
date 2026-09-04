#ifndef EDITOR_H
#define EDITOR_H

#include "estructura.h"
#include "archivo.h"
#include "portapapeles.h"

// ============================================================================
// PERSONALIZACIÓN DE LA TERMINAL (TEMA VISUAL)
// ============================================================================
// Aquí puedes modificar los colores ANSI para cambiar la apariencia del editor.
// El formato general es: \x1b[<estilo>;<color>m
#define TEMA_FONDO         "\x1b[49m"   // Fondo de la terminal (49 = Defecto)
#define TEMA_TEXTO         "\x1b[39m"   // Color del texto principal (39 = Defecto)
#define TEMA_BARRA_ESTADO  "\x1b[7m"    // Barra inferior invertida
#define TEMA_LINEA_VACIA   "~"          // Símbolo para las líneas vacías al final
#define TEMA_RESTAURAR     "\x1b[0m"    // Restablece todos los formatos

/**
 * Módulo: Editor
 * Propósito: Manejar la Lógica de Negocio y la Interfaz de Usuario (UI).
 *
 * Contiene el estado global de la sesión de edición y las funciones para
 * renderizar caracteres y procesar atajos de teclado.
 *
 * Conviven dos representaciones del documento, con roles distintos:
 *   - 'estructura': la lista doblemente enlazada que se dibuja y se escribe
 *     con el teclado. Es la VISTA en memoria.
 *   - 'archivo': el descriptor abierto sobre el que operan los comandos ':'
 *     mediante llamadas al sistema. Es la FUENTE DE VERDAD en disco.
 * Tras cada comando ':' la vista se recarga desde el disco para que ambas
 * nunca queden desincronizadas.
 */
typedef struct {
    EstructuraTexto *estructura;    // Estructura de datos (Modelo en RAM)
    Archivo archivo;                // Descriptor + índice de líneas (Modelo en disco)
    Portapapeles portapapeles;      // Portapapeles secuencial de los comandos y/x
    int cursorX;              // Posición lógica del cursor (Columnas)
    int cursorY;              // Posición lógica del cursor (Filas)
    int filasPantalla;        // Límite visual de la ventana
    int columnasPantalla;     // Límite visual de la ventana
    NodoLinea *lineaActual;   // Referencia rápida al nodo donde estamos escribiendo
    char *nombreArchivo;      // Ruta del archivo abierto
    int mostrarAyuda;         // Bandera booleana de UI
    int mostrarEstructura;    // Bandera booleana de UI para debug
    int mostrarMemoria;       // Bandera booleana de UI para monitoreo de RAM
    int desplazamientoFila;   // Desplazamiento (Scroll) vertical
    int modificado;           // 1 si hay cambios escritos a mano sin guardar
    int salir;                // 1 cuando el usuario pide salir (Ctrl+Q o :q)
} EstadoEditor;

// Funciones del ciclo de vida y renderizado
void editor_inicializar(EstadoEditor *e, const char* nombreArchivo);
void editor_liberar(EstadoEditor *e);
void editor_refrescar_pantalla(EstadoEditor *e);
void editor_procesar_tecla(EstadoEditor *e);

// Funciones expuestas del UI para el procesamiento de comandos
void editor_pantalla_limpiar(void);

// Muestra una pantalla de bienvenida al arrancar
void editor_pantalla_bienvenida(EstadoEditor *e);

/**
 * Vuelve a cargar la lista enlazada desde el archivo en disco y reubica el
 * cursor. Se llama después de cada comando ':' que modifica bytes, porque en
 * ese momento el disco quedó por delante de la vista en memoria.
 */
void editor_recargar_desde_disco(EstadoEditor *e);

/**
 * Vuelca la lista enlazada al disco (lo que hace Ctrl+S) y reindexa el
 * descriptor. Devuelve 0 si todo salió bien.
 */
int editor_guardar_en_disco(EstadoEditor *e, char *log_opcional);

#endif
