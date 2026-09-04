#include "cli.h"
#include "cadena.h"
#include <unistd.h>  // isatty, STDOUT_FILENO
#include <stdlib.h>  // malloc, free, strtol
#include <string.h>  // strcmp, strlen
#include <stdio.h>   // fgets

#define MAX_LINEA_COMANDO 8192

const char *cli_texto_ayuda(void) {
    return
        "==== COMANDOS DEL EDITOR ====\n"
        "\n"
        "  o [archivo]      Abre un archivo; si no existe lo crea      (open)\n"
        "  p                Imprime todo el archivo numerado           (lseek+read)\n"
        "  p [n]            Imprime unicamente la linea n              (lseek+read)\n"
        "  a [texto]        Anade el texto como ultima linea           (lseek+write)\n"
        "  d [n]            Borra la linea n y recorta el archivo      (write+ftruncate)\n"
        "  i [n] [texto]    Inserta el texto como linea n              (read+lseek+write)\n"
        "  s [palabra]      Busca la palabra y lista las coincidencias\n"
        "  m                Metadatos: tamano, permisos, inodo, fecha  (fstat)\n"
        "  y [n]            Copia la linea n al portapapeles secuencial\n"
        "  y                Muestra el contenido del portapapeles\n"
        "  yc               Vacia el portapapeles\n"
        "  x [n]            Pega el portapapeles completo desde la linea n\n"
        "  h                Muestra esta ayuda\n"
        "  q                Cierra el descriptor, libera memoria y sale (close)\n";
}

/* ==========================================================================
 * ANALISIS DE LA LINEA DE COMANDO
 * ========================================================================== */

static const char *saltar_espacios(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/**
 * Convierte una cadena a numero de linea (entero positivo).
 * Rechaza texto sobrante ("3x") y valores fuera de rango.
 */
static int parsear_numero(const char *texto, int *destino) {
    if (!texto) return -1;
    texto = saltar_espacios(texto);
    if (*texto == '\0') return -1;

    char *fin = NULL;
    long valor = strtol(texto, &fin, 10);
    if (fin == texto) return -1;

    fin = (char *)saltar_espacios(fin);
    if (*fin != '\0') return -1;
    if (valor < 1 || valor > 1000000000L) return -1;

    *destino = (int)valor;
    return 0;
}

/**
 * Separa "3 hola mundo" en el numero 3 y el resto "hola mundo".
 * El resto se devuelve tal cual (con sus espacios interiores intactos).
 */
static int parsear_numero_y_resto(const char *argumento, int *numero, const char **resto) {
    if (!argumento) return -1;
    argumento = saltar_espacios(argumento);
    if (*argumento == '\0') return -1;

    char *fin = NULL;
    long valor = strtol(argumento, &fin, 10);
    if (fin == argumento) return -1;
    if (valor < 1 || valor > 1000000000L) return -1;

    *numero = (int)valor;
    *resto = saltar_espacios(fin);
    return 0;
}

/* ==========================================================================
 * COMANDOS
 * ========================================================================== */

static ResultadoCli comando_abrir(Archivo *a, const char *argumento, Cadena *salida) {
    if (!argumento || argumento[0] == '\0') {
        cadena_anexar_str(salida, "Uso: o [archivo]\n");
        return CLI_ERROR;
    }
    if (archivo_abrir(a, argumento) == -1) {
        cadena_printf(salida, "No se pudo abrir '%s' (ver el detalle de perror arriba).\n",
                      argumento);
        return CLI_ERROR;
    }
    cadena_printf(salida, "Archivo '%s' abierto: fd=%d, %d linea(s), %ld byte(s).\n",
                  a->ruta, a->fd, a->total, (long)a->tamano);
    return CLI_OK;
}

static ResultadoCli comando_imprimir(Archivo *a, const char *argumento, Cadena *salida) {
    if (!argumento || argumento[0] == '\0')
        return archivo_imprimir_todo(a, salida) == -1 ? CLI_ERROR : CLI_OK;

    int n = 0;
    if (parsear_numero(argumento, &n) == -1) {
        cadena_printf(salida, "Uso: p [n]   ('%s' no es un numero de linea valido)\n", argumento);
        return CLI_ERROR;
    }
    return archivo_imprimir_linea(a, n, salida) == -1 ? CLI_ERROR : CLI_OK;
}

static ResultadoCli comando_anexar(Archivo *a, const char *argumento, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return CLI_ERROR;
    }
    if (archivo_anexar(a, argumento ? argumento : "") == -1) {
        cadena_anexar_str(salida, "No se pudo anexar la linea.\n");
        return CLI_ERROR;
    }
    cadena_printf(salida, "Linea %d anadida al final.\n", a->total);
    return CLI_OK;
}

static ResultadoCli comando_borrar(Archivo *a, const char *argumento, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return CLI_ERROR;
    }
    int n = 0;
    if (parsear_numero(argumento, &n) == -1) {
        cadena_anexar_str(salida, "Uso: d [n]\n");
        return CLI_ERROR;
    }
    if (n > a->total) {
        cadena_printf(salida, "Error: la linea %d no existe (el archivo tiene %d lineas).\n",
                      n, a->total);
        return CLI_ERROR;
    }
    if (archivo_borrar_linea(a, n) == -1) {
        cadena_printf(salida, "No se pudo borrar la linea %d.\n", n);
        return CLI_ERROR;
    }
    cadena_printf(salida, "Linea %d borrada. Quedan %d linea(s).\n", n, a->total);
    return CLI_OK;
}

static ResultadoCli comando_insertar(Archivo *a, const char *argumento, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return CLI_ERROR;
    }
    int n = 0;
    const char *texto = NULL;
    if (parsear_numero_y_resto(argumento, &n, &texto) == -1) {
        cadena_anexar_str(salida, "Uso: i [n] [texto]\n");
        return CLI_ERROR;
    }
    int mas_alla_del_final = (n > a->total);
    if (archivo_insertar_linea(a, n, texto) == -1) {
        cadena_printf(salida, "No se pudo insertar en la linea %d.\n", n);
        return CLI_ERROR;
    }
    if (mas_alla_del_final)
        cadena_printf(salida, "La linea %d no existia: el texto se anadio al final (linea %d).\n",
                      n, a->total);
    else
        cadena_printf(salida, "Texto insertado como linea %d. Ahora hay %d linea(s).\n",
                      n, a->total);
    return CLI_OK;
}

static ResultadoCli comando_copiar(Archivo *a, Portapapeles *p,
                                   const char *argumento, Cadena *salida) {
    // 'y' sin argumento muestra lo que hay acumulado.
    if (!argumento || argumento[0] == '\0')
        return portapapeles_listar(p, salida) == -1 ? CLI_ERROR : CLI_OK;

    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return CLI_ERROR;
    }
    int n = 0;
    if (parsear_numero(argumento, &n) == -1) {
        cadena_anexar_str(salida, "Uso: y [n]\n");
        return CLI_ERROR;
    }
    if (n > a->total) {
        cadena_printf(salida, "Error: la linea %d no existe (el archivo tiene %d lineas).\n",
                      n, a->total);
        return CLI_ERROR;
    }

    size_t largo = 0;
    char *texto = archivo_leer_linea(a, n, &largo);
    if (!texto) {
        cadena_printf(salida, "No se pudo leer la linea %d.\n", n);
        return CLI_ERROR;
    }
    int estado = portapapeles_agregar(p, texto, largo);
    free(texto);
    if (estado == -1) {
        cadena_anexar_str(salida, "No se pudo copiar al portapapeles.\n");
        return CLI_ERROR;
    }
    cadena_printf(salida, "Linea %d copiada. El portapapeles tiene %d linea(s).\n", n, p->total);
    return CLI_OK;
}

static ResultadoCli comando_pegar(Archivo *a, Portapapeles *p,
                                  const char *argumento, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return CLI_ERROR;
    }
    if (p->total == 0) {
        cadena_anexar_str(salida, "El portapapeles esta vacio (use 'y [n]' para copiar).\n");
        return CLI_ERROR;
    }
    int n = 0;
    if (parsear_numero(argumento, &n) == -1) {
        cadena_anexar_str(salida, "Uso: x [n]\n");
        return CLI_ERROR;
    }

    // Se insertan en el orden en que fueron copiadas, cada una una linea
    // mas abajo que la anterior.
    int destino = n;
    for (NodoCopia *nodo = p->cabeza; nodo; nodo = nodo->siguiente) {
        if (archivo_insertar_linea(a, destino, nodo->texto) == -1) {
            cadena_printf(salida, "Fallo al pegar en la linea %d.\n", destino);
            return CLI_ERROR;
        }
        destino++;
    }
    cadena_printf(salida, "%d linea(s) pegada(s) desde la linea %d. Ahora hay %d linea(s).\n",
                  p->total, n, a->total);
    return CLI_OK;
}

/* ==========================================================================
 * DESPACHADOR
 * ========================================================================== */

ResultadoCli cli_ejecutar_comando(Archivo *a, Portapapeles *p,
                                  const char *linea, char **salida) {
    if (salida) *salida = NULL;
    if (!a || !p) return CLI_ERROR;

    Cadena texto_salida;
    cadena_iniciar(&texto_salida);

    /* 1. Separar el verbo del resto de la linea. */
    const char *cursor = saltar_espacios(linea ? linea : "");
    char verbo[16];
    size_t i = 0;
    while (*cursor && *cursor != ' ' && *cursor != '\t' &&
           *cursor != '\n' && *cursor != '\r') {
        if (i < sizeof verbo - 1) verbo[i++] = *cursor;
        cursor++;                       // un verbo larguisimo se consume igual
    }
    verbo[i] = '\0';

    /* 2. El argumento es el resto crudo de la linea, sin el salto final.
     *    Se conserva tal cual para que 'a hola   mundo' respete los espacios. */
    cursor = saltar_espacios(cursor);
    char *argumento = duplicar_cadena(cursor);
    if (!argumento) {
        cadena_liberar(&texto_salida);
        return CLI_ERROR;
    }
    size_t largo_argumento = strlen(argumento);
    while (largo_argumento > 0 && (argumento[largo_argumento - 1] == '\n' ||
                                   argumento[largo_argumento - 1] == '\r')) {
        argumento[--largo_argumento] = '\0';
    }

    /* 3. Despachar. */
    ResultadoCli resultado;
    if (verbo[0] == '\0' || verbo[0] == '#') {          // linea vacia o comentario
        resultado = CLI_OK;
    } else if (strcmp(verbo, "o") == 0) {
        resultado = comando_abrir(a, argumento, &texto_salida);
    } else if (strcmp(verbo, "p") == 0) {
        resultado = comando_imprimir(a, argumento, &texto_salida);
    } else if (strcmp(verbo, "a") == 0) {
        resultado = comando_anexar(a, argumento, &texto_salida);
    } else if (strcmp(verbo, "d") == 0) {
        resultado = comando_borrar(a, argumento, &texto_salida);
    } else if (strcmp(verbo, "i") == 0) {
        resultado = comando_insertar(a, argumento, &texto_salida);
    } else if (strcmp(verbo, "s") == 0) {
        resultado = archivo_buscar(a, argumento, &texto_salida) == -1 ? CLI_ERROR : CLI_OK;
    } else if (strcmp(verbo, "m") == 0) {
        resultado = archivo_metadatos(a, &texto_salida) == -1 ? CLI_ERROR : CLI_OK;
    } else if (strcmp(verbo, "y") == 0) {
        resultado = comando_copiar(a, p, argumento, &texto_salida);
    } else if (strcmp(verbo, "yl") == 0) {
        resultado = portapapeles_listar(p, &texto_salida) == -1 ? CLI_ERROR : CLI_OK;
    } else if (strcmp(verbo, "yc") == 0) {
        portapapeles_limpiar(p);
        cadena_anexar_str(&texto_salida, "Portapapeles vaciado.\n");
        resultado = CLI_OK;
    } else if (strcmp(verbo, "x") == 0) {
        resultado = comando_pegar(a, p, argumento, &texto_salida);
    } else if (strcmp(verbo, "h") == 0 || strcmp(verbo, "?") == 0) {
        cadena_anexar_str(&texto_salida, cli_texto_ayuda());
        resultado = CLI_OK;
    } else if (strcmp(verbo, "q") == 0) {
        resultado = CLI_SALIR;
    } else {
        cadena_printf(&texto_salida,
                      "Comando desconocido: '%s'. Escriba 'h' para ver la ayuda.\n", verbo);
        resultado = CLI_ERROR;
    }

    free(argumento);
    if (salida) *salida = cadena_entregar(&texto_salida);
    cadena_liberar(&texto_salida);
    return resultado;
}

/* ==========================================================================
 * BUCLE INTERACTIVO POR ENTRADA ESTANDAR
 * ==========================================================================
 * Se usa cuando el editor recibe ordenes por una tuberia (script de pruebas,
 * shell) en vez de por un teclado. Toda la salida sale por write() para no
 * mezclarla con el buffer de stdio y perder el orden.
 */
int cli_repl(const char *ruta_inicial) {
    Archivo archivo;
    Portapapeles portapapeles;
    archivo_iniciar(&archivo);
    portapapeles_iniciar(&portapapeles);

    int codigo_salida = 0;
    int interactivo = isatty(STDIN_FILENO);

    if (ruta_inicial) {
        if (archivo_abrir(&archivo, ruta_inicial) == -1) {
            codigo_salida = 1;
        } else {
            Cadena aviso;
            cadena_iniciar(&aviso);
            cadena_printf(&aviso, "Archivo '%s' abierto: fd=%d, %d linea(s), %ld byte(s).\n",
                          archivo.ruta, archivo.fd, archivo.total, (long)archivo.tamano);
            if (aviso.datos) escribir_todo(STDOUT_FILENO, aviso.datos, aviso.longitud);
            cadena_liberar(&aviso);
        }
    }

    if (codigo_salida == 0) {
        char linea[MAX_LINEA_COMANDO];
        while (1) {
            if (interactivo) escribir_todo(STDOUT_FILENO, "editor> ", 8);
            if (!fgets(linea, sizeof linea, stdin)) break;   // EOF o error

            char *salida = NULL;
            ResultadoCli resultado = cli_ejecutar_comando(&archivo, &portapapeles,
                                                          linea, &salida);
            if (salida) {
                escribir_todo(STDOUT_FILENO, salida, strlen(salida));
                free(salida);
            }
            if (resultado == CLI_SALIR) break;
        }
    }

    // --- CALL SYSTEM: close() --- y liberacion de toda la memoria dinamica:
    // el comando q debe salir sin dejar fugas ni descriptores abiertos.
    archivo_cerrar(&archivo);
    portapapeles_limpiar(&portapapeles);
    return codigo_salida;
}
