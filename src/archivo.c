#include "archivo.h"
#include <fcntl.h>     // open() y sus flags O_RDWR, O_CREAT
#include <unistd.h>    // read, write, lseek, ftruncate, close
#include <sys/stat.h>  // fstat, struct stat, macros S_IS*
#include <stdlib.h>    // malloc, realloc, free
#include <string.h>    // strlen, strstr, memcpy
#include <stdio.h>     // snprintf, perror
#include <errno.h>     // EINTR
#include <time.h>      // localtime, strftime

#define BLOQUE 4096    // Tamano del bloque de lectura pedido al kernel

/* ==========================================================================
 * ENVOLTORIOS DE E/S
 * ==========================================================================
 * El kernel NO garantiza que un write() de N bytes escriba los N bytes: puede
 * escribir menos (disco lleno, tuberia, senal). Ignorar eso es un bug clasico,
 * asi que todo el modulo escribe y lee a traves de estos dos envoltorios.
 */
ssize_t escribir_todo(int fd, const void *buffer, size_t n) {
    const char *p = (const char *)buffer;
    size_t escritos = 0;
    while (escritos < n) {
        ssize_t r = write(fd, p + escritos, n - escritos);
        if (r == -1) {
            if (errno == EINTR) continue;   // interrumpido por una senal: reintentar
            return -1;
        }
        if (r == 0) break;
        escritos += (size_t)r;
    }
    return (ssize_t)escritos;
}

ssize_t leer_todo(int fd, void *buffer, size_t n) {
    char *p = (char *)buffer;
    size_t leidos = 0;
    while (leidos < n) {
        ssize_t r = read(fd, p + leidos, n - leidos);
        if (r == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;                  // fin de archivo
        leidos += (size_t)r;
    }
    return (ssize_t)leidos;
}

/* ==========================================================================
 * CICLO DE VIDA
 * ========================================================================== */

void archivo_iniciar(Archivo *a) {
    if (!a) return;
    a->fd = -1;
    a->ruta = NULL;
    a->inicio = NULL;
    a->largo = NULL;
    a->total = 0;
    a->capacidad = 0;
    a->termina_sin_salto = 0;
    a->tamano = 0;
}

int archivo_esta_abierto(const Archivo *a) {
    return a && a->fd != -1;
}

void archivo_cerrar(Archivo *a) {
    if (!a) return;
    if (a->fd != -1) {
        // --- CALL SYSTEM: close() ---
        // Devuelve el descriptor a la tabla de FDs del proceso. Sin esto,
        // un editor de larga vida agotaria el limite de descriptores.
        if (close(a->fd) == -1) perror("close");
        a->fd = -1;
    }
    free(a->ruta);
    free(a->inicio);
    free(a->largo);
    archivo_iniciar(a);
}

int archivo_abrir(Archivo *a, const char *ruta) {
    if (!a || !ruta || ruta[0] == '\0') return -1;

    // --- CALL SYSTEM: open() ---
    // O_RDWR : necesitamos leer Y escribir sobre el mismo descriptor.
    // O_CREAT: si el archivo no existe, el kernel lo crea.
    // 0644   : permisos rw-r--r-- (filtrados por el umask del proceso).
    int fd = open(ruta, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    char *copia = duplicar_cadena(ruta);
    if (!copia) {
        close(fd);
        return -1;
    }

    archivo_cerrar(a);          // cierra un archivo previo, si lo habia
    a->fd = fd;
    a->ruta = copia;

    if (archivo_indexar(a) == -1) {
        archivo_cerrar(a);
        return -1;
    }
    return 0;
}

/**
 * Agrega una entrada al indice, duplicando la capacidad de los arreglos
 * cuando se agotan (misma estrategia amortizada que Cadena).
 */
static int indice_agregar(Archivo *a, off_t inicio, size_t largo) {
    if ((size_t)a->total == a->capacidad) {
        size_t nueva = a->capacidad ? a->capacidad * 2 : 64;

        off_t *nuevos_inicios = (off_t *)realloc(a->inicio, nueva * sizeof(off_t));
        if (!nuevos_inicios) {
            perror("realloc (indice de lineas)");
            return -1;
        }
        a->inicio = nuevos_inicios;

        size_t *nuevos_largos = (size_t *)realloc(a->largo, nueva * sizeof(size_t));
        if (!nuevos_largos) {
            perror("realloc (indice de lineas)");
            return -1;
        }
        a->largo = nuevos_largos;
        a->capacidad = nueva;
    }
    a->inicio[a->total] = inicio;
    a->largo[a->total] = largo;
    a->total++;
    return 0;
}

/**
 * Reconstruye el indice recorriendo el archivo BYTE A BYTE hasta cada salto de
 * linea, que es literalmente lo que pide el enunciado del comando p.
 * Se llama al abrir y despues de cada mutacion.
 */
int archivo_indexar(Archivo *a) {
    if (!archivo_esta_abierto(a)) return -1;

    a->total = 0;
    a->termina_sin_salto = 0;

    // --- CALL SYSTEM: lseek() --- reposiciona el cursor del archivo al byte 0
    if (lseek(a->fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek (indexar)");
        return -1;
    }

    char bloque[BLOQUE];
    ssize_t leidos;
    off_t posicion = 0;        // offset absoluto del inicio del bloque actual
    off_t inicio_linea = 0;
    char ultimo_byte = '\n';
    int hubo_bytes = 0;

    // --- CALL SYSTEM: read() --- el kernel trae los bytes del disco a RAM
    while ((leidos = read(a->fd, bloque, sizeof bloque)) > 0) {
        hubo_bytes = 1;
        for (ssize_t i = 0; i < leidos; i++) {
            ultimo_byte = bloque[i];
            if (bloque[i] == '\n') {
                off_t fin = posicion + i;
                if (indice_agregar(a, inicio_linea, (size_t)(fin - inicio_linea)) == -1)
                    return -1;
                inicio_linea = fin + 1;
            }
        }
        posicion += leidos;
    }
    if (leidos == -1) {
        perror("read (indexar)");
        return -1;
    }

    a->tamano = posicion;

    // Caso borde: el archivo no termina en salto de linea. Esa cola tambien
    // es una linea y debe quedar indexada.
    if (hubo_bytes && ultimo_byte != '\n') {
        a->termina_sin_salto = 1;
        if (indice_agregar(a, inicio_linea, (size_t)(posicion - inicio_linea)) == -1)
            return -1;
    }
    return 0;
}

/* ==========================================================================
 * LECTURA
 * ========================================================================== */

char *archivo_leer_linea(Archivo *a, int n, size_t *largo_out) {
    if (!archivo_esta_abierto(a)) return NULL;
    if (n < 1 || n > a->total) return NULL;

    size_t largo = a->largo[n - 1];
    char *buffer = (char *)malloc(largo + 1);
    if (!buffer) {
        perror("malloc (leer_linea)");
        return NULL;
    }

    if (largo > 0) {
        // Saltamos directo al byte donde arranca la linea, sin leer todo el archivo.
        if (lseek(a->fd, a->inicio[n - 1], SEEK_SET) == (off_t)-1) {
            perror("lseek (leer_linea)");
            free(buffer);
            return NULL;
        }
        if (leer_todo(a->fd, buffer, largo) != (ssize_t)largo) {
            perror("read (leer_linea)");
            free(buffer);
            return NULL;
        }
    }
    buffer[largo] = '\0';
    if (largo_out) *largo_out = largo;
    return buffer;
}

int archivo_imprimir_linea(Archivo *a, int n, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return -1;
    }
    if (n < 1 || n > a->total) {
        cadena_printf(salida, "Error: la linea %d no existe (el archivo tiene %d lineas).\n",
                      n, a->total);
        return -1;
    }
    size_t largo = 0;
    char *texto = archivo_leer_linea(a, n, &largo);
    if (!texto) return -1;

    cadena_printf(salida, "%4d | ", n);
    cadena_anexar(salida, texto, largo);
    cadena_anexar_str(salida, "\n");
    free(texto);
    return 0;
}

/**
 * Imprime el archivo completo recorriendo sus bytes hasta cada salto de linea
 * y numerando las lineas por el camino.
 */
int archivo_imprimir_todo(Archivo *a, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return -1;
    }
    if (a->total == 0) {
        cadena_anexar_str(salida, "(archivo vacio)\n");
        return 0;
    }
    if (lseek(a->fd, 0, SEEK_SET) == (off_t)-1) {
        perror("lseek (imprimir_todo)");
        return -1;
    }

    char bloque[BLOQUE];
    char encabezado[16];
    ssize_t leidos;
    int numero = 1;
    int en_inicio_de_linea = 1;

    while ((leidos = read(a->fd, bloque, sizeof bloque)) > 0) {
        for (ssize_t i = 0; i < leidos; i++) {
            if (en_inicio_de_linea) {
                snprintf(encabezado, sizeof encabezado, "%4d | ", numero);
                if (cadena_anexar_str(salida, encabezado) == -1) return -1;
                en_inicio_de_linea = 0;
            }
            if (cadena_anexar(salida, &bloque[i], 1) == -1) return -1;
            if (bloque[i] == '\n') {
                numero++;
                en_inicio_de_linea = 1;
            }
        }
    }
    if (leidos == -1) {
        perror("read (imprimir_todo)");
        return -1;
    }
    // La ultima linea puede no traer salto: se lo agregamos solo a la salida.
    if (!en_inicio_de_linea) cadena_anexar_str(salida, "\n");
    return 0;
}

int archivo_buscar(Archivo *a, const char *palabra, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return -1;
    }
    if (!palabra || palabra[0] == '\0') {
        cadena_anexar_str(salida, "Uso: s [palabra]\n");
        return -1;
    }

    int coincidencias = 0;
    for (int n = 1; n <= a->total; n++) {
        size_t largo = 0;
        char *texto = archivo_leer_linea(a, n, &largo);
        if (!texto) return -1;

        char *posicion = strstr(texto, palabra);
        if (posicion) {
            cadena_printf(salida, "%4d | col %d | %s\n",
                          n, (int)(posicion - texto) + 1, texto);
            coincidencias++;
        }
        free(texto);
    }

    if (coincidencias == 0)
        cadena_printf(salida, "La palabra '%s' no aparece en el archivo.\n", palabra);
    else
        cadena_printf(salida, "-- %d coincidencia(s) para '%s' --\n", coincidencias, palabra);
    return coincidencias;
}

/**
 * Traduce el campo st_mode del inodo a la notacion rwxrwxrwx de ls -l.
 */
static void formatear_permisos(mode_t modo, char destino[11]) {
    destino[0] = S_ISDIR(modo) ? 'd' : (S_ISREG(modo) ? '-' : '?');
    destino[1] = (modo & S_IRUSR) ? 'r' : '-';
    destino[2] = (modo & S_IWUSR) ? 'w' : '-';
    destino[3] = (modo & S_IXUSR) ? 'x' : '-';
    destino[4] = (modo & S_IRGRP) ? 'r' : '-';
    destino[5] = (modo & S_IWGRP) ? 'w' : '-';
    destino[6] = (modo & S_IXGRP) ? 'x' : '-';
    destino[7] = (modo & S_IROTH) ? 'r' : '-';
    destino[8] = (modo & S_IWOTH) ? 'w' : '-';
    destino[9] = (modo & S_IXOTH) ? 'x' : '-';
    destino[10] = '\0';
}

/**
 * Comando m: metadatos del inodo mediante fstat().
 * fstat opera sobre el DESCRIPTOR ya abierto (no sobre la ruta), asi que
 * informa exactamente del inodo que estamos editando, aunque el archivo
 * hubiera sido renombrado mientras tanto.
 */
int archivo_metadatos(Archivo *a, Cadena *salida) {
    if (!archivo_esta_abierto(a)) {
        cadena_anexar_str(salida, "Error: no hay ningun archivo abierto (use 'o [archivo]').\n");
        return -1;
    }

    struct stat st;
    // --- CALL SYSTEM: fstat() --- copia la metadata del inodo a nuestra RAM
    if (fstat(a->fd, &st) == -1) {
        perror("fstat");
        return -1;
    }

    char permisos[11];
    formatear_permisos(st.st_mode, permisos);

    char fecha[64] = "(desconocida)";
    time_t modificado = st.st_mtime;
    struct tm *tiempo = localtime(&modificado);
    if (tiempo) strftime(fecha, sizeof fecha, "%Y-%m-%d %H:%M:%S", tiempo);

    cadena_printf(salida, "==== METADATOS DEL ARCHIVO (fstat sobre fd=%d) ====\n\n", a->fd);
    cadena_printf(salida, "  Ruta            : %s\n", a->ruta);
    cadena_printf(salida, "  Tamano          : %ld bytes\n", (long)st.st_size);
    cadena_printf(salida, "  Permisos        : %s (octal %04o)\n",
                  permisos, (unsigned)(st.st_mode & 07777));
    cadena_printf(salida, "  Numero de inodo : %lu\n", (unsigned long)st.st_ino);
    cadena_printf(salida, "  Ultima modific. : %s\n", fecha);
    cadena_printf(salida, "  Enlaces duros   : %lu\n", (unsigned long)st.st_nlink);
    cadena_printf(salida, "  Propietario     : UID %lu / GID %lu\n",
                  (unsigned long)st.st_uid, (unsigned long)st.st_gid);
    cadena_printf(salida, "  Bloques de 512B : %ld\n", (long)st.st_blocks);
    cadena_printf(salida, "  Lineas indexadas: %d%s\n", a->total,
                  a->termina_sin_salto ? " (la ultima no termina en salto de linea)" : "");
    return 0;
}

/* ==========================================================================
 * MUTACION
 * ========================================================================== */

/**
 * Comando a: anade el texto como nueva linea al final del archivo.
 * Si el archivo no terminaba en salto de linea, primero se lo agregamos para
 * no pegar el texto nuevo al final de la ultima linea existente.
 */
int archivo_anexar(Archivo *a, const char *texto) {
    if (!archivo_esta_abierto(a)) return -1;
    if (!texto) texto = "";

    // --- CALL SYSTEM: lseek(SEEK_END) --- cursor al final del archivo
    if (lseek(a->fd, 0, SEEK_END) == (off_t)-1) {
        perror("lseek (anexar)");
        return -1;
    }

    if (a->termina_sin_salto) {
        if (escribir_todo(a->fd, "\n", 1) != 1) {
            perror("write (anexar salto previo)");
            return -1;
        }
    }

    size_t largo = strlen(texto);
    // --- CALL SYSTEM: write() ---
    if (largo > 0 && escribir_todo(a->fd, texto, largo) != (ssize_t)largo) {
        perror("write (anexar)");
        return -1;
    }
    if (escribir_todo(a->fd, "\n", 1) != 1) {
        perror("write (anexar salto)");
        return -1;
    }
    return archivo_indexar(a);
}

/**
 * Lee en un buffer dinamico todos los bytes desde 'desde' hasta el final.
 * Es la "cola" que hay que reescribir desplazada al insertar o borrar.
 */
static char *leer_cola(Archivo *a, off_t desde, size_t *largo_out) {
    size_t largo = (size_t)(a->tamano - desde);
    *largo_out = largo;
    if (largo == 0) return NULL;

    char *cola = (char *)malloc(largo);
    if (!cola) {
        perror("malloc (cola del archivo)");
        return NULL;
    }
    if (lseek(a->fd, desde, SEEK_SET) == (off_t)-1) {
        perror("lseek (leer cola)");
        free(cola);
        return NULL;
    }
    if (leer_todo(a->fd, cola, largo) != (ssize_t)largo) {
        perror("read (leer cola)");
        free(cola);
        return NULL;
    }
    return cola;
}

/**
 * Comando i: inserta 'texto' como nueva linea n, desplazando el resto.
 * Estrategia (la que pide el reto tecnico de los equipos de 2):
 *   1. Guardar en un buffer dinamico todo lo que hay desde la linea n.
 *   2. Volver con lseek al inicio de la linea n.
 *   3. Escribir la linea nueva y, detras, la cola guardada.
 * Insertar mas alla del final degrada a un anexado normal.
 */
int archivo_insertar_linea(Archivo *a, int n, const char *texto) {
    if (!archivo_esta_abierto(a)) return -1;
    if (!texto) texto = "";
    if (n < 1) return -1;
    if (n > a->total) return archivo_anexar(a, texto);

    off_t desplazamiento = a->inicio[n - 1];
    size_t cola_largo = 0;
    char *cola = leer_cola(a, desplazamiento, &cola_largo);
    if (cola_largo > 0 && !cola) return -1;

    int resultado = 0;
    if (lseek(a->fd, desplazamiento, SEEK_SET) == (off_t)-1) {
        perror("lseek (insertar)");
        resultado = -1;
    }

    size_t largo = strlen(texto);
    if (resultado == 0 && largo > 0 &&
        escribir_todo(a->fd, texto, largo) != (ssize_t)largo) {
        perror("write (insertar texto)");
        resultado = -1;
    }
    if (resultado == 0 && escribir_todo(a->fd, "\n", 1) != 1) {
        perror("write (insertar salto)");
        resultado = -1;
    }
    if (resultado == 0 && cola_largo > 0 &&
        escribir_todo(a->fd, cola, cola_largo) != (ssize_t)cola_largo) {
        perror("write (reescribir cola)");
        resultado = -1;
    }

    free(cola);
    if (resultado == -1) return -1;
    return archivo_indexar(a);
}

/**
 * Comando d: borra la linea n desplazando los bytes posteriores y truncando.
 *   1. Leer la cola posterior a la linea en un buffer dinamico.
 *   2. lseek al inicio de la linea y reescribir alli la cola.
 *   3. ftruncate para recortar los bytes sobrantes del final.
 * Sin el paso 3 quedaria basura repetida al final del archivo.
 */
int archivo_borrar_linea(Archivo *a, int n) {
    if (!archivo_esta_abierto(a)) return -1;
    if (n < 1 || n > a->total) return -1;

    off_t inicio_linea = a->inicio[n - 1];
    // El final de la linea n es el inicio de la n+1 (asi se lleva su salto);
    // si es la ultima linea, el final es el final del archivo.
    off_t fin_linea = (n < a->total) ? a->inicio[n] : a->tamano;
    size_t bytes_borrados = (size_t)(fin_linea - inicio_linea);

    size_t cola_largo = 0;
    char *cola = leer_cola(a, fin_linea, &cola_largo);
    if (cola_largo > 0 && !cola) return -1;

    int resultado = 0;
    if (lseek(a->fd, inicio_linea, SEEK_SET) == (off_t)-1) {
        perror("lseek (borrar)");
        resultado = -1;
    }
    if (resultado == 0 && cola_largo > 0 &&
        escribir_todo(a->fd, cola, cola_largo) != (ssize_t)cola_largo) {
        perror("write (desplazar cola)");
        resultado = -1;
    }
    free(cola);
    if (resultado == -1) return -1;

    // --- CALL SYSTEM: ftruncate() ---
    // El archivo aun conserva al final los bytes viejos de la linea borrada.
    // ftruncate le ordena al kernel fijar el tamano exacto y liberar el resto.
    if (ftruncate(a->fd, a->tamano - (off_t)bytes_borrados) == -1) {
        perror("ftruncate");
        return -1;
    }
    return archivo_indexar(a);
}
