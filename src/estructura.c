#include "estructura.h"
#include "archivo.h" // escribir_todo(): write() robusto ante escrituras parciales
#include "cadena.h"  // Buffer dinámico que sustituye a los arreglos fijos
#include <stdlib.h> // Para asignación y liberación de memoria dinámica (malloc, free)
#include <string.h> // Para manipulación y operaciones con cadenas de texto (strlen, strstr, memcpy, etc.)
#include <fcntl.h>  // Para opciones de control de archivos (flags de open como O_WRONLY, O_CREAT, etc.)
#include <unistd.h> // Para acceso a la API POSIX del sistema operativo (system calls: open, read, write, close)
#include <stdio.h>  // Para funciones de entrada y salida formateadas (snprintf, perror)

#define SAFE_PRINT(log_out, offset, max_sz, ...) \
    do { \
        if (log_out && offset < max_sz) { \
            int cb = snprintf(log_out + offset, max_sz - offset, __VA_ARGS__); \
            if (cb > 0) offset += cb; \
            if (offset >= max_sz) offset = max_sz - 1; \
        } \
    } while(0)


EstructuraTexto* estructura_crear() {
    EstructuraTexto *alm = (EstructuraTexto*)malloc(sizeof(EstructuraTexto));
    if (!alm) {
        perror("malloc (EstructuraTexto)");
        return NULL;
    }
    alm->cabeza = NULL; alm->cola = NULL; alm->totalLineas = 0;
    if (!estructura_insertar_linea(alm, NULL)) {
        free(alm);
        return NULL;
    }
    return alm;
}

static void liberar_palabras(NodoLinea *linea) {
    NodoPalabra *actual = linea->palabras_cabeza;
    while (actual) {
        NodoPalabra *sig = actual->siguiente;
        free(actual->texto); free(actual);
        actual = sig;
    }
    linea->palabras_cabeza = NULL; linea->palabras_cola = NULL; linea->longitud = 0;
}

void estructura_destruir(EstructuraTexto *alm) {
    if (!alm) return;
    NodoLinea *actual = alm->cabeza;
    while (actual) {
        NodoLinea *sig = actual->siguiente;
        liberar_palabras(actual);
        free(actual);
        actual = sig;
    }
    free(alm);
}

static void enlazar_nueva_linea(EstructuraTexto *alm, NodoLinea *nueva_linea, NodoLinea *despues_de) {
    if (despues_de == NULL) {
        nueva_linea->siguiente = alm->cabeza; nueva_linea->anterior = NULL;
        if (alm->cabeza) alm->cabeza->anterior = nueva_linea;
        alm->cabeza = nueva_linea;
        if (!alm->cola) alm->cola = nueva_linea;
    } else {
        nueva_linea->siguiente = despues_de->siguiente; nueva_linea->anterior = despues_de;
        despues_de->siguiente = nueva_linea;
        if (nueva_linea->siguiente) nueva_linea->siguiente->anterior = nueva_linea;
        else alm->cola = nueva_linea;
    }
}

NodoLinea* estructura_insertar_linea(EstructuraTexto *alm, NodoLinea *despues_de) {
    NodoLinea *nueva_linea = (NodoLinea*)malloc(sizeof(NodoLinea));
    if (!nueva_linea) {
        perror("malloc (NodoLinea)");
        return NULL;
    }
    nueva_linea->palabras_cabeza = NULL; nueva_linea->palabras_cola = NULL; nueva_linea->longitud = 0;
    enlazar_nueva_linea(alm, nueva_linea, despues_de);
    alm->totalLineas++;
    return nueva_linea;
}

void estructura_eliminar_linea(EstructuraTexto *alm, NodoLinea *linea) {
    if (!alm || !linea) return;
    if (linea->anterior) linea->anterior->siguiente = linea->siguiente;
    else alm->cabeza = linea->siguiente;
    if (linea->siguiente) linea->siguiente->anterior = linea->anterior;
    else alm->cola = linea->anterior;
    liberar_palabras(linea); free(linea); alm->totalLineas--;
}

static NodoPalabra* crear_nodo_palabra(const char *texto, size_t len) {
    NodoPalabra *nodo = (NodoPalabra*)malloc(sizeof(NodoPalabra));
    if (!nodo) {
        perror("malloc (NodoPalabra)");
        return NULL;
    }
    nodo->longitud = len; nodo->capacidad = len + 1;
    nodo->texto = (char*)malloc(nodo->capacidad);
    if (!nodo->texto) {
        perror("malloc (texto de NodoPalabra)");
        free(nodo);
        return NULL;
    }
    if (len > 0) memcpy(nodo->texto, texto, len);
    nodo->texto[len] = '\0';
    nodo->anterior = NULL; nodo->siguiente = NULL;
    return nodo;
}

static void agregar_palabra(NodoLinea *linea, const char *texto, size_t len) {
    if (len == 0) return;
    NodoPalabra *nodo = crear_nodo_palabra(texto, len);
    if (!nodo) return;
    if (!linea->palabras_cabeza) {
        linea->palabras_cabeza = nodo; linea->palabras_cola = nodo;
    } else {
        linea->palabras_cola->siguiente = nodo;
        nodo->anterior = linea->palabras_cola;
        linea->palabras_cola = nodo;
    }
    linea->longitud += len;
}

static const char* agrupar_espacios(NodoLinea *linea, const char *p) {
    const char *inicio = p;
    while (*p == ' ') p++;
    agregar_palabra(linea, inicio, p - inicio); return p;
}

static const char* agrupar_letras(NodoLinea *linea, const char *p) {
    const char *inicio = p;
    while (*p != '\0' && *p != ' ') p++;
    agregar_palabra(linea, inicio, p - inicio); return p;
}

static void reconstruir_linea(NodoLinea *linea, const char *str) {
    liberar_palabras(linea);
    const char *p = str;
    while (*p != '\0') {
        if (*p == ' ') p = agrupar_espacios(linea, p);
        else p = agrupar_letras(linea, p);
    }
}

/**
 * Devuelve el texto completo de una línea en un bloque de memoria pedido con
 * malloc, con 'bytes_extra' de holgura para lo que vaya a añadirse después.
 *
 * Antes esto se hacía sobre un 'char buffer[4096]' de la pila: cualquier línea
 * de más de 4095 bytes lo desbordaba y corrompía la pila. Al reservar el
 * tamaño exacto de la línea el problema desaparece por construcción.
 *
 * El llamador debe liberar el bloque con free().
 */
static char* linea_obtener_texto(NodoLinea *linea, size_t bytes_extra) {
    char *texto = (char*)malloc(linea->longitud + bytes_extra + 1);
    if (!texto) {
        perror("malloc (texto de linea)");
        return NULL;
    }
    size_t offset = 0;
    NodoPalabra *actual = linea->palabras_cabeza;
    while (actual) {
        memcpy(texto + offset, actual->texto, actual->longitud);
        offset += actual->longitud;
        actual = actual->siguiente;
    }
    texto[offset] = '\0';
    return texto;
}

void linea_insertar_caracter(NodoLinea *linea, size_t pos, char c) {
    if (pos > linea->longitud) pos = linea->longitud;
    char *texto = linea_obtener_texto(linea, 1);   // 1 byte extra para el nuevo carácter
    if (!texto) return;

    memmove(&texto[pos + 1], &texto[pos], linea->longitud - pos + 1);
    texto[pos] = c;
    reconstruir_linea(linea, texto);
    free(texto);
}

void linea_eliminar_caracter(NodoLinea *linea, size_t pos) {
    if (pos >= linea->longitud) return;
    char *texto = linea_obtener_texto(linea, 0);
    if (!texto) return;

    memmove(&texto[pos], &texto[pos + 1], linea->longitud - pos);
    reconstruir_linea(linea, texto);
    free(texto);
}

int estructura_guardar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out) {
    // --- CALL SYSTEM: open() ---
    int fd = open(nombre_archivo, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open (guardar)");
        return -1;
    }

    size_t off = 0;
    size_t max_sz = 65000;
    SAFE_PRINT(log_out, off, max_sz, "==== INICIANDO GUARDADO A DISCO ====\r\n");
    SAFE_PRINT(log_out, off, max_sz, "1. Llamada: open(\"%s\", O_WRONLY | O_CREAT | O_TRUNC, 0644) => File Descriptor (fd): %d\r\n\r\n", nombre_archivo, fd);
    SAFE_PRINT(log_out, off, max_sz, "2. TRAZA DE CONSTRUCCION DEL BUFFER DE TEXTO (Linea por Linea, Palabra por Palabra):\r\n");

    int n_linea = 1;
    int resultado = 0;
    NodoLinea *actual = alm->cabeza;

    while (actual != NULL) {
        SAFE_PRINT(log_out, off, max_sz, "\r\n[Línea %d]:\r\n", n_linea);

        // Traza didáctica: cómo se va armando la línea palabra por palabra.
        Cadena parcial;
        cadena_iniciar(&parcial);
        NodoPalabra *pal = actual->palabras_cabeza;
        while (pal) {
            cadena_anexar(&parcial, pal->texto, pal->longitud);
            SAFE_PRINT(log_out, off, max_sz, "   -> + Iterando NodoPalabra('%s') => Buffer RAM parcial: \"%s\"\r\n",
                       pal->texto, parcial.datos ? parcial.datos : "");
            pal = pal->siguiente;
        }
        cadena_liberar(&parcial);

        // Texto real de la línea, con un byte extra para el salto de línea.
        char *texto = linea_obtener_texto(actual, 1);
        if (!texto) { resultado = -1; break; }

        size_t len = actual->longitud;
        texto[len] = '\n';

        SAFE_PRINT(log_out, off, max_sz, "   -> FIN DE LÍNEA: Se anexa el salto de linea ('\\n') al buffer resultante.\r\n");
        SAFE_PRINT(log_out, off, max_sz, "   -> Llamada: write(fd: %d, buffer, %zu bytes) enviada al kernel.\r\n", fd, len + 1);

        // --- CALL SYSTEM: write() (Escritura en Disco) ---
        // El kernel toma los bytes de RAM y controla el hardware del disco
        // para persistir físicamente la información.
        if (escribir_todo(fd, texto, len + 1) != (ssize_t)(len + 1)) {
            perror("write (guardar)");
            free(texto);
            resultado = -1;
            break;
        }
        free(texto);

        n_linea++;
        actual = actual->siguiente;
    }

    // --- CALL SYSTEM: close() ---
    if (close(fd) == -1) {
        perror("close (guardar)");
        resultado = -1;
    }
    SAFE_PRINT(log_out, off, max_sz, "\r\n3. Llamada final: close(%d)\r\n", fd);
    SAFE_PRINT(log_out, off, max_sz, "\r\n==== GUARDADO COMPLETADO ====\r\n");

    return resultado;
}

static void limpiar_estructura_para_cargar(EstructuraTexto *alm) {
    NodoLinea *actual = alm->cabeza;
    while (actual != NULL) {
        NodoLinea *siguiente = actual->siguiente;
        liberar_palabras(actual);
        free(actual);
        actual = siguiente;
    }
    alm->cabeza = NULL; alm->cola = NULL; alm->totalLineas = 0;
}

int estructura_cargar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out) {
    // --- CALL SYSTEM: open() ---
    int fd = open(nombre_archivo, O_RDONLY);
    if (fd == -1) {
        perror("open (cargar)");
        if (log_out) snprintf(log_out, 256, "open(\"%s\", O_RDONLY) => Falló (El archivo no existe)\r\n", nombre_archivo);
        return -1;
    }
    limpiar_estructura_para_cargar(alm);

    size_t off = 0;
    size_t max_sz = 65000;
    SAFE_PRINT(log_out, off, max_sz, "==== INICIANDO CARGA DE ARCHIVO A MEMORIA (ESTRUCTURA DE DATOS) ====\r\n");
    SAFE_PRINT(log_out, off, max_sz, "1. Llamada: open(\"%s\", O_RDONLY) => File Descriptor (fd): %d\r\n\r\n", nombre_archivo, fd);
    SAFE_PRINT(log_out, off, max_sz, "2. TRAZA DE CREACION DE NODOS Y TOKENIZACION (Línea por Línea, Palabra por Palabra):\r\n");

    NodoLinea *linea_cargando = estructura_insertar_linea(alm, NULL);
    if (!linea_cargando) {
        close(fd);
        return -1;
    }

    char buffer_leido[4096];
    Cadena linea_actual;              // Buffer dinámico: admite líneas de cualquier largo
    cadena_iniciar(&linea_actual);
    ssize_t bytes_leidos;
    int n_linea = 1;

    // --- CALL SYSTEM: read() (Lectura de Disco) ---
    // El kernel mueve los cabezales del disco (o consulta la celda de la SSD),
    // extrae los bytes y los deposita en el array 'buffer_leido' en RAM.
    while ((bytes_leidos = read(fd, buffer_leido, sizeof(buffer_leido))) > 0) {
        SAFE_PRINT(log_out, off, max_sz, "   [Llamada: read() retorno bloque de %zd bytes desde el disco]\r\n", bytes_leidos);

        for (ssize_t i = 0; i < bytes_leidos; i++) {
            char c = buffer_leido[i];
            if (c == '\n') {
                const char *texto = linea_actual.datos ? linea_actual.datos : "";
                SAFE_PRINT(log_out, off, max_sz, "\r\n-> [Línea %d detectada por('\\n')]: Extraida del disco: \"%s\"\r\n", n_linea, texto);

                reconstruir_linea(linea_cargando, texto);

                NodoPalabra *p = linea_cargando->palabras_cabeza;
                while(p) {
                    SAFE_PRINT(log_out, off, max_sz, "   -> + Se alojo dinámicamente un NodoPalabra('%s')\r\n", p->texto);
                    p = p->siguiente;
                }
                SAFE_PRINT(log_out, off, max_sz, "   -> La Línea %d completa fue enlazada en la memoria.\r\n", n_linea);

                linea_cargando = estructura_insertar_linea(alm, linea_cargando);
                if (!linea_cargando) {
                    cadena_liberar(&linea_actual);
                    close(fd);
                    return -1;
                }
                linea_actual.longitud = 0;                       // reiniciar sin liberar
                if (linea_actual.datos) linea_actual.datos[0] = '\0';
                n_linea++;
            } else if (c != '\r') {
                cadena_anexar(&linea_actual, &c, 1);
            }
        }
    }
    if (bytes_leidos == -1) perror("read (cargar)");

    if (linea_actual.longitud > 0) {
        SAFE_PRINT(log_out, off, max_sz, "\r\n-> [Línea %d (fin de archivo sin salto)]: \"%s\"\r\n", n_linea, linea_actual.datos);
        reconstruir_linea(linea_cargando, linea_actual.datos);
        NodoPalabra *p = linea_cargando->palabras_cabeza;
        while(p) {
            SAFE_PRINT(log_out, off, max_sz, "   -> + Se alojo dinámicamente un NodoPalabra('%s')\r\n", p->texto);
            p = p->siguiente;
        }
    }
    cadena_liberar(&linea_actual);

    if (linea_cargando->longitud == 0 && alm->totalLineas > 1) {
        estructura_eliminar_linea(alm, linea_cargando);
    }

    // --- CALL SYSTEM: close() ---
    if (close(fd) == -1) perror("close (cargar)");
    SAFE_PRINT(log_out, off, max_sz, "\r\n3. Llamada final: close(%d)\r\n", fd);
    SAFE_PRINT(log_out, off, max_sz, "\r\n==== CARGA COMPLETADA ====\r\n");

    return 0;
}

int estructura_buscar_palabra(EstructuraTexto *alm, const char *palabra, NodoLinea **linea_encontrada, int *pos_x, int *pos_y) {
    if (!alm || !palabra || strlen(palabra) == 0) return 0;
    NodoLinea *actual = alm->cabeza; int y = 0;
    while (actual != NULL) {
        char *texto = linea_obtener_texto(actual, 0);
        if (!texto) return 0;

        char *coincidencia = strstr(texto, palabra);
        if (coincidencia != NULL) {
            if (linea_encontrada) *linea_encontrada = actual;
            if (pos_x) *pos_x = (int)(coincidencia - texto);
            if (pos_y) *pos_y = y;
            free(texto);
            return 1;
        }
        free(texto);
        actual = actual->siguiente; y++;
    }
    return 0;
}
