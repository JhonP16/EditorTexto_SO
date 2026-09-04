#ifndef CLI_H
#define CLI_H

#include "archivo.h"
#include "portapapeles.h"

/**
 * ============================================================================
 * MODULO: CLI (interprete de comandos del editor)
 * ============================================================================
 * Un unico despachador alimenta las dos formas de manejar el editor:
 *
 *      cli_ejecutar_comando("d 2")
 *           /                    \
 *   prompt ':' del modo visual    REPL por entrada estandar
 *   (comando.c, hay un teclado)   (cli_repl, hay una tuberia)
 *
 * Los comandos no imprimen nada por su cuenta: devuelven su salida en un
 * buffer dinamico (parametro 'salida') que cada interfaz presenta a su manera
 * -- el visual en una pantalla modal, el REPL directo a STDOUT. Asi la logica
 * del editor no depende de como se este mostrando.
 */
typedef enum {
    CLI_OK,      // Comando ejecutado
    CLI_SALIR,   // El usuario pidio salir (comando q)
    CLI_ERROR    // Comando invalido o fallido; el detalle va en 'salida'
} ResultadoCli;

/**
 * Ejecuta una linea de comando sobre el archivo y el portapapeles dados.
 * 'salida' recibe un buffer con malloc que el llamador debe liberar con free
 * (queda en NULL si el comando no produjo texto).
 */
ResultadoCli cli_ejecutar_comando(Archivo *a, Portapapeles *p,
                                  const char *linea, char **salida);

/* Bucle interactivo por entrada estandar. Devuelve el codigo de salida. */
int cli_repl(const char *ruta_inicial);

/* Texto de ayuda compartido por el comando h, por Ctrl+H y por --help. */
const char *cli_texto_ayuda(void);

#endif
