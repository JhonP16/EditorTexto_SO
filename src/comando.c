#include "comando.h"
#include "terminal.h"
#include "cli.h"      // Despachador de comandos compartido con el modo tubería
#include "cadena.h"   // Buffer dinámico para leer /proc sin usar stdio
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>    // open() y sus flags
#include <stdio.h>    // Sólo para printf/fgets/getchar sobre STDIN y STDOUT

// ============================================================================
// FUNCIONES DE UI PROPIAS DE COMANDOS
// ============================================================================

/**
 * Pausa el modo crudo (raw) de la terminal y muestra una pantalla interactiva de registros (logs).
 * 
 * Se utiliza después de operaciones pesadas o sensibles (como guardar el archivo) para
 * informarle al usuario el resultado o volcar el estado del sistema.
 * Restaura el estado original (modo raw) al presionar ENTER.
 * 
 * - titulo: El título en la cabecera de la pantalla.
 * - log: La cadena de texto detallada con toda la información.
 */
void comando_mostrar_log_sistema(const char *titulo, const char *log) {
    editor_pantalla_limpiar();
    terminal_desactivar_modo_raw();
    printf("\x1b[1;36m==== %s ====\x1b[0m\n\n", titulo);
    printf("%s\n", log);
    printf("\n\x1b[1;33mPresione ENTER para continuar...\x1b[0m");
    fflush(stdout);
    getchar();
    terminal_activar_modo_raw();
}

/**
 * Dibuja en pantalla el menú modal de Ayuda con los atajos disponibles.
 * Escribe directamente a la salida estándar usando códigos ANSI.
 */
static void comando_dibujar_ayuda(void) {
    char *texto =
        "===============================================================\r\n"
        "                  AYUDA DEL EDITOR EAFIT                       \r\n"
        "===============================================================\r\n\r\n"
        "\x1b[1;36mCOMANDOS DEL EDITOR\x1b[0m  (cada uno pide los datos que necesite)\r\n"
        "  Ctrl + O : o [archivo]    Abrir o crear un archivo     (open)\r\n"
        "  Ctrl + V : p [n]          Ver una línea                (lseek + read)\r\n"
        "  Ctrl + A : a [texto]      Añadir línea al final        (lseek + write)\r\n"
        "  Ctrl + D : d [n]          Borrar una línea             (write + ftruncate)\r\n"
        "  Ctrl + N : i [n] [texto]  Insertar en la línea n       (read + lseek + write)\r\n"
        "  Ctrl + F : s [palabra]    Buscar y saltar al resultado\r\n"
        "  Ctrl + G : m              Metadatos del archivo        (fstat)\r\n"
        "  Ctrl + Y : y [n]          Copiar línea al portapapeles\r\n"
        "  Ctrl + U : x [n]          Pegar el portapapeles\r\n"
        "  Ctrl + Q : q              Salir liberando todo         (close)\r\n\r\n"
        "  \x1b[1;33mLos atajos con número de línea usan la del cursor si pulsas ENTER.\x1b[0m\r\n\r\n"
        "\x1b[1;36mENTORNO VISUAL\x1b[0m\r\n"
        "  Ctrl + L : Línea de comandos libre (escribe cualquier comando: yl, yc, h...)\r\n"
        "  Ctrl + S : Guardar el documento actual\r\n"
        "  Ctrl + H : Mostrar / Ocultar esta ayuda\r\n"
        "  Ctrl + E : Mostrar Estructura de Datos\r\n"
        "  Ctrl + P : Monitor de memoria del proceso\r\n"
        "  Flechas  : Navegar por el texto\r\n"
        "  W A S D  : Navegación alternativa\r\n\r\n"
        "  \x1b[1;33mEl modo CLI clásico ('editor> ') se abre con:  ./editor -c archivo\x1b[0m\r\n\r\n"
        "===============================================================\r\n"
        "Presiona 'Ctrl + H' para volver a la edición...\r\n";
    write(STDOUT_FILENO, texto, strlen(texto));
}

/**
 * Dibuja un volcado (dump) visual de la estructura de datos subyacente.
 * 
 * Itera la lista enlazada de líneas y sus correspondientes listas de palabras, 
 * imprimiendo los tamaños y las direcciones de memoria de los nodos en tiempo real
 * para entender cómo se estructuraa el archivo internamente.
 * 
 * - e: Puntero al estado del editor.
 */
static void comando_dibujar_estructura(EstadoEditor *e) {
    char buffer_linea[256];
    char *encabezado = "\x1b[1;33m==== ESTRUCTURA DE DATOS (LISTA LIGADA DE LÍNEAS/PALABRAS) ====\x1b[0m\r\n\r\n";
    write(STDOUT_FILENO, encabezado, strlen(encabezado));
    
    NodoLinea *actual = e->estructura->cabeza;
    int i = 0;
    while (actual != NULL && i < e->filasPantalla - 5) {
        snprintf(buffer_linea, sizeof(buffer_linea), 
            "\x1b[1;32m[%p]\x1b[0m \x1b[1;37mLínea %d\x1b[0m -> len: \x1b[36m%zu\x1b[0m\r\n",
            (void*)actual, i, actual->longitud);
        write(STDOUT_FILENO, buffer_linea, strlen(buffer_linea));
        i++;
        
        NodoPalabra *pal = actual->palabras_cabeza;
        while(pal && i < e->filasPantalla - 5) {
            snprintf(buffer_linea, sizeof(buffer_linea), 
                "   -> \x1b[1;34mPalabra:\x1b[0m '\x1b[35m%s\x1b[0m' (len: \x1b[36m%zu\x1b[0m)\r\n", 
                pal->texto, pal->longitud);
            write(STDOUT_FILENO, buffer_linea, strlen(buffer_linea));
            pal = pal->siguiente;
            i++;
        }
        actual = actual->siguiente;
    }
    char *pie = "\r\n\x1b[1;33mPresiona 'Ctrl + E' para volver a la edición...\x1b[0m\r\n";
    write(STDOUT_FILENO, pie, strlen(pie));
}

/**
 * Lee un archivo completo a memoria usando ÚNICAMENTE llamadas al sistema.
 *
 * El parcial prohíbe fopen/fread/fclose, así que aquí no se usa stdio ni
 * siquiera para los archivos virtuales del kernel: open() consigue el
 * descriptor, read() trae los bytes por bloques y una Cadena dinámica los
 * acumula sin límite fijo (los archivos de /proc no reportan su tamaño real
 * con fstat, así que hay que leer hasta que read() devuelva 0).
 *
 * Devuelve un buffer con malloc que el llamador libera, o NULL si falla.
 */
static char *leer_archivo_completo(const char *ruta) {
    // --- CALL SYSTEM: open() ---
    int fd = open(ruta, O_RDONLY);
    if (fd == -1) {
        perror("open (/proc)");
        return NULL;
    }

    Cadena contenido;
    cadena_iniciar(&contenido);

    char bloque[4096];
    ssize_t leidos;
    // --- CALL SYSTEM: read() ---
    while ((leidos = read(fd, bloque, sizeof bloque)) > 0) {
        if (cadena_anexar(&contenido, bloque, (size_t)leidos) == -1) break;
    }
    if (leidos == -1) perror("read (/proc)");

    // --- CALL SYSTEM: close() ---
    if (close(fd) == -1) perror("close (/proc)");

    return cadena_entregar(&contenido);
}

/**
 * Recorre un buffer de texto entregando una línea por iteración.
 * Reemplaza el salto por '\0' dentro del propio buffer (que es nuestro), así
 * que no hace falta reservar memoria extra por línea.
 *
 * Uso:  char *sig = buffer;
 *       char *linea;
 *       while ((linea = siguiente_linea(&sig)) != NULL) { ... }
 */
static char *siguiente_linea(char **cursor) {
    if (!cursor || !*cursor || **cursor == '\0') return NULL;
    char *inicio = *cursor;
    char *fin = strchr(inicio, '\n');
    if (fin) {
        *fin = '\0';
        *cursor = fin + 1;
    } else {
        *cursor = inicio + strlen(inicio);
    }
    return inicio;
}

/**
 * Muestra un diagnóstico avanzado y educativo sobre la memoria del proceso.
 *
 * Lee información crítica del sistema directamente desde los archivos virtuales
 * del kernel de Linux (`/proc/self/status` y `/proc/self/maps`). Explica al usuario
 * los segmentos (HEAP, STACK, TEXT) y su estado actual de consumo.
 */
static void comando_dibujar_memoria(void) {
    char *encabezado = 
        "\x1b[1;36m==== MONITOR DIDÁCTICO DE MEMORIA Y PROCESOS ====\x1b[0m\r\n\r\n"
        "\x1b[1;33mGUÍA TEÓRICA (El Modelo de Memoria de Linux):\x1b[0m\r\n"
        "  * El Kernel asigna a cada programa un espacio de \x1b[1;37mMemoria Virtual\x1b[0m exclusivo.\r\n"
        "  * \x1b[1;35mSTACK (Pila):\x1b[0m Crece hacia abajo. Guarda variables locales (ej. int fd) y retornos de funcion.\r\n"
        "  * \x1b[1;31mHEAP (Monticulo):\x1b[0m Crece hacia arriba. Para memoria dinamica (tus mallocs de nodos).\r\n"
        "  * \x1b[1;37mTEXT (Codigo):\x1b[0m Memoria de solo lectura donde viven las instrucciones binarias.\r\n"
        "  * \x1b[1;32mPermisos:\x1b[0m r=Leer, w=Escribir, x=Ejecutar, p=Privado.\r\n\r\n"
        "\x1b[1;36m==== ESTADÍSTICAS DEL KERNEL (/proc/self/status) ====\x1b[0m\r\n"
        "\x1b[1;33mDEFINICION E IMPORTANCIA DE LAS VARIABLES:\x1b[0m\r\n"
        "  * \x1b[1;36mVmSize/Peak:\x1b[0m Memoria Virtual reservada total. Fundamental para prever desbordamientos.\r\n"
        "  * \x1b[1;32mVmRSS:\x1b[0m Memoria Fisica (RAM real) consumida. El indicador mas importante de Memory Leaks.\r\n"
        "  * \x1b[1;33mVmData:\x1b[0m Sube en cada malloc(). Si crece sin parar, significa que te falta usar free().\r\n"
        "  * \x1b[1;35mVmStk:\x1b[0m Sube si abusas de la recursividad o declaras arrays locales masivos.\r\n"
        "  * \x1b[1;37mVmExe:\x1b[0m Peso de las instrucciones de tu ejecutable cargadas en la memoria.\r\n\r\n";
        
    write(STDOUT_FILENO, encabezado, strlen(encabezado));
    
    char *contenido_status = leer_archivo_completo("/proc/self/status");
    if (!contenido_status) {
        char *error = "\x1b[1;31mError al abrir /proc/self/status\x1b[0m\r\n";
        write(STDOUT_FILENO, error, strlen(error));
        return;
    }

    char out_buf[1024];
    char *cursor = contenido_status;
    char *linea;
    while ((linea = siguiente_linea(&cursor)) != NULL) {
        if (strncmp(linea, "VmPeak:", 7) == 0 ||
            strncmp(linea, "VmSize:", 7) == 0 ||
            strncmp(linea, "VmRSS:", 6) == 0 ||
            strncmp(linea, "VmData:", 7) == 0 ||
            strncmp(linea, "VmStk:", 6) == 0 ||
            strncmp(linea, "VmExe:", 6) == 0) {
            
            char *colon = strchr(linea, ':');
            if (colon) {
                *colon = '\0';
                char *explicacion = "";
                
                if (strcmp(linea, "VmPeak") == 0) explicacion = " \x1b[36m(Pico maximo de memoria virtual alcanzado)\x1b[0m";
                else if (strcmp(linea, "VmSize") == 0) explicacion = " \x1b[36m(Tamano virtual total asignado por el OS)\x1b[0m";
                else if (strcmp(linea, "VmRSS") == 0) explicacion = " \x1b[1;32m(Uso REAL en chips de memoria RAM fisica)\x1b[0m";
                else if (strcmp(linea, "VmData") == 0) explicacion = " \x1b[33m(Segmento BSS + HEAP: mallocs de los nodos)\x1b[0m";
                else if (strcmp(linea, "VmStk") == 0) explicacion = " \x1b[35m(STACK: Memoria de las variables locales)\x1b[0m";
                else if (strcmp(linea, "VmExe") == 0) explicacion = " \x1b[37m(TEXT: Instrucciones compiladas de este editor)\x1b[0m";
                
                snprintf(out_buf, sizeof(out_buf), 
                    "  \x1b[1;37m%-8s:\x1b[0m \x1b[1;32m%-10s\x1b[0m %s\r\n", linea, colon + 1, explicacion);
                write(STDOUT_FILENO, out_buf, strlen(out_buf));
            }
        }
    }
    free(contenido_status);

    char *map_enc =
        "\r\n\x1b[1;36m==== MAPA DE SEGMENTOS VIRTUALES (/proc/self/maps) ====\x1b[0m\r\n"
        "\x1b[1;37mRango de Direcciones (Hex)          Permisos   Segmento        Concepto de S.O.\x1b[0m\r\n"
        "---------------------------------------------------------------------------------------\r\n";
    write(STDOUT_FILENO, map_enc, strlen(map_enc));
    
    char *contenido_maps = leer_archivo_completo("/proc/self/maps");
    if (contenido_maps) {
        int line_count = 0;
        char *cursor_maps = contenido_maps;
        char *linea_mapa;
        while ((linea_mapa = siguiente_linea(&cursor_maps)) != NULL && line_count < 15) {
            if (strstr(linea_mapa, "[heap]") || strstr(linea_mapa, "[stack]") || strstr(linea_mapa, "editor")) {
                char addr[64]="", perms[16]="", offset[32]="", dev[16]="", inode[32]="", path[256]="";
                int vars = sscanf(linea_mapa, "%63s %15s %31s %15s %31s %255s", addr, perms, offset, dev, inode, path);
                
                if (vars >= 6) {
                    char color_code[16] = "\x1b[37m"; 
                    char *explicacion = "";
                    
                    char *base_path = strrchr(path, '/');
                    if (base_path) base_path++; else base_path = path;
                    
                    if (strstr(path, "[heap]")) {
                        strcpy(color_code, "\x1b[1;31m");
                        explicacion = "<= HEAP (Nodos Malloc)";
                    } else if (strstr(path, "[stack]")) {
                        strcpy(color_code, "\x1b[1;35m");
                        explicacion = "<= STACK (Locales C)";
                    } else {
                        strcpy(color_code, "\x1b[1;36m");
                        explicacion = "<= TEXT (Codigo Bin)";
                    }
                    
                    snprintf(out_buf, sizeof(out_buf), 
                        "  %s%-35s\x1b[0m \x1b[1;32m%-10s\x1b[0m %s%-15s\x1b[0m \x1b[1;33m%s\x1b[0m\r\n", 
                        color_code, addr, perms, color_code, base_path, explicacion);
                    write(STDOUT_FILENO, out_buf, strlen(out_buf));
                    line_count++;
                }
            }
        }
        free(contenido_maps);
    }

    char *pie = "\r\n\x1b[1;33mPresiona 'Ctrl + P' para volver a la edicion...\x1b[0m\r\n";
    write(STDOUT_FILENO, pie, strlen(pie));
}

/**
 * Función enrutadora del renderizado de modales.
 * Llama a la función de dibujo correspondiente al modal activo.
 * - e: Estado del editor.
 * - Retorna: 1 si se ha dibujado un modal, 0 si no.
 */
int comando_dibujar_modal(EstadoEditor *e) {
    if (e->mostrarAyuda) { comando_dibujar_ayuda(); return 1; }
    if (e->mostrarEstructura) { comando_dibujar_estructura(e); return 1; }
    if (e->mostrarMemoria) { comando_dibujar_memoria(); return 1; }
    return 0;
}

// ============================================================================
// FUNCIONES MANEJADORAS INDIVIDUALES (Módulos de Comando)
// ============================================================================

/**
 * Conmuta (activa/desactiva) la visibilidad del modal de Ayuda.
 * Si se activa, apaga cualquier otro modal abierto para evitar superposiciones.
 */
static void comando_toggle_ayuda(EstadoEditor *e) {
    e->mostrarAyuda = !e->mostrarAyuda;
    e->mostrarEstructura = 0;
    e->mostrarMemoria = 0;
}

/**
 * Conmuta la visibilidad del modal de Estructura de Datos.
 */
static void comando_toggle_estructura(EstadoEditor *e) {
    e->mostrarEstructura = !e->mostrarEstructura;
    e->mostrarAyuda = 0;
    e->mostrarMemoria = 0;
}

/**
 * Conmuta la visibilidad del modal del Monitor de Memoria.
 */
static void comando_toggle_memoria(EstadoEditor *e) {
    e->mostrarMemoria = !e->mostrarMemoria;
    e->mostrarAyuda = 0;
    e->mostrarEstructura = 0;
}

/**
 * Pide al bucle principal que termine.
 *
 * Antes esta función llamaba a exit(0) directamente, lo que dejaba sin
 * ejecutar la liberación de memoria y el close() del descriptor. Ahora sólo
 * levanta la bandera: main() sale del bucle y llama a editor_liberar(), que es
 * lo que exige el enunciado del comando q ("sin dejar fugas de memoria").
 */
static void comando_salir(EstadoEditor *e) {
    // Restaurar tema original de la terminal del usuario antes de salir
    write(STDOUT_FILENO, TEMA_RESTAURAR, strlen(TEMA_RESTAURAR));
    e->salir = 1;
}

/**
 * Ejecuta el proceso de guardado del documento en el disco duro.
 * Extrae los registros (syscall logs) generados por el almacén durante
 * el proceso y se los muestra al usuario.
 */
static void comando_guardar(EstadoEditor *e) {
    char *log_syscall = malloc(65536);
    if (!log_syscall) {
        perror("malloc (log de guardado)");
        return;
    }
    log_syscall[0] = '\0';

    if (editor_guardar_en_disco(e, log_syscall) == -1)
        comando_mostrar_log_sistema("ERROR AL GUARDAR", "No se pudo escribir el archivo.");
    else
        comando_mostrar_log_sistema("REPORTE DETALLADO (GUARDAR ARCHIVO)", log_syscall);

    free(log_syscall);
}

/* ============================================================================
 * PUENTE ENTRE EL MODO VISUAL Y EL INTÉRPRETE DE COMANDOS
 * ============================================================================
 * El modo visual no reimplementa ningún comando: construye la misma cadena de
 * texto que se escribiría en el modo CLI ("d 2", "i 4 hola") y se la entrega a
 * cli_ejecutar_comando(). Hay una sola implementación de cada comando, y la
 * pantalla completa es simplemente otra forma de escribirlos.
 */

/**
 * Ejecuta una orden ya construida y muestra su resultado.
 * PRECONDICIÓN: el modo raw debe estar desactivado (lo restaura el llamador).
 */
static void comando_procesar_orden(EstadoEditor *e, const char *orden) {
    char *salida = NULL;
    ResultadoCli resultado = cli_ejecutar_comando(&e->archivo, &e->portapapeles,
                                                  orden, &salida);

    if (resultado == CLI_SALIR) {
        free(salida);
        e->salir = 1;
        return;
    }

    if (salida && salida[0] != '\0') {
        printf("\n%s", salida);
        printf("\n\x1b[1;33mPresione ENTER para volver al editor...\x1b[0m");
        fflush(stdout);
        getchar();
    }
    free(salida);

    // El disco es la fuente de verdad: la vista se recarga desde él.
    editor_recargar_desde_disco(e);
}

/**
 * Prepara la pantalla para pedirle algo al usuario.
 *
 * Los comandos trabajan sobre los bytes del disco, así que lo que sólo vive
 * en la lista enlazada (lo escrito a mano y aún sin guardar) se persiste
 * primero; de lo contrario el comando operaría sobre texto viejo.
 */
static void comando_preparar_prompt(EstadoEditor *e, const char *titulo) {
    if (e->modificado) editor_guardar_en_disco(e, NULL);
    editor_pantalla_limpiar();
    terminal_desactivar_modo_raw();
    printf("\x1b[1;36m==== %s ====\x1b[0m\n\n", titulo);
}

/**
 * Lee una línea del usuario en 'destino'. Devuelve 1 si leyó algo, 0 si hubo
 * fin de entrada. La cadena vuelve sin el salto de línea final.
 */
static int comando_leer_entrada(char *destino, size_t tam) {
    if (!fgets(destino, tam, stdin)) return 0;
    size_t n = strlen(destino);
    while (n > 0 && (destino[n - 1] == '\n' || destino[n - 1] == '\r'))
        destino[--n] = '\0';
    return 1;
}

/**
 * Atajo de un comando SIN argumentos (m).
 */
static void atajo_directo(EstadoEditor *e, const char *titulo, const char *orden) {
    comando_preparar_prompt(e, titulo);
    comando_procesar_orden(e, orden);
    terminal_activar_modo_raw();
}

/**
 * Atajo de un comando CON un argumento (o, p, a, d, s, y, x).
 *
 * Es la generalización del prompt que ya usaba Ctrl+F: se pide el argumento
 * por pantalla y se arma la orden. Si 'defecto' no es NULL, pulsar ENTER en
 * vacío lo acepta, de modo que Ctrl+D + ENTER borra la línea del cursor.
 */
static void atajo_con_argumento(EstadoEditor *e, const char *titulo,
                                const char *verbo, const char *etiqueta,
                                const char *defecto) {
    comando_preparar_prompt(e, titulo);

    printf("%s", etiqueta);
    if (defecto) printf(" [\x1b[1;33m%s\x1b[0m]", defecto);
    printf(": ");
    fflush(stdout);

    char entrada[4096];
    if (comando_leer_entrada(entrada, sizeof entrada)) {
        const char *argumento = (entrada[0] == '\0' && defecto) ? defecto : entrada;

        Cadena orden;
        cadena_iniciar(&orden);
        cadena_printf(&orden, "%s %s", verbo, argumento);
        if (orden.datos) comando_procesar_orden(e, orden.datos);
        cadena_liberar(&orden);
    }
    terminal_activar_modo_raw();
}

/**
 * Atajo del comando i, el único que necesita DOS argumentos: pide primero la
 * línea (con la del cursor por defecto) y después el texto.
 */
static void atajo_insertar(EstadoEditor *e) {
    char defecto[16];
    snprintf(defecto, sizeof defecto, "%d", e->cursorY + 1);

    comando_preparar_prompt(e, "INSERTAR LÍNEA (comando i)");

    printf("Insertar en la línea [\x1b[1;33m%s\x1b[0m]: ", defecto);
    fflush(stdout);

    char numero[64];
    if (comando_leer_entrada(numero, sizeof numero)) {
        const char *linea_destino = (numero[0] == '\0') ? defecto : numero;

        printf("Texto: ");
        fflush(stdout);

        char texto[4096];
        if (comando_leer_entrada(texto, sizeof texto)) {
            Cadena orden;
            cadena_iniciar(&orden);
            cadena_printf(&orden, "i %s %s", linea_destino, texto);
            if (orden.datos) comando_procesar_orden(e, orden.datos);
            cadena_liberar(&orden);
        }
    }
    terminal_activar_modo_raw();
}

/**
 * Abre la LÍNEA DE COMANDOS libre (el prompt ':' al estilo de vi).
 *
 * Sirve para escribir cualquier comando completo, incluidos los que no tienen
 * atajo propio (yl, yc, h) y los que se quieran dirigir a una línea distinta
 * de la del cursor.
 */
static void comando_linea_comandos(EstadoEditor *e) {
    comando_preparar_prompt(e, "LÍNEA DE COMANDOS");
    printf("(h = ayuda, ENTER vacío = volver al editor)\n\n:");
    fflush(stdout);

    char linea[4096];
    if (comando_leer_entrada(linea, sizeof linea))
        comando_procesar_orden(e, linea);

    terminal_activar_modo_raw();
}

/**
 * Detiene la edición fluida para iniciar un prompt de búsqueda interactiva.
 * Solicita una palabra al usuario, la busca en el almacén y, de encontrarla,
 * reposiciona el cursor exactamente sobre ella.
 */
static void comando_buscar(EstadoEditor *e) {
    comando_preparar_prompt(e, "BUSCAR PALABRA (comando s)");
    printf("Buscar palabra: ");
    fflush(stdout);

    char termino[256];
    if (comando_leer_entrada(termino, sizeof termino) && termino[0] != '\0') {
        // 1. El comando 's' lista todas las coincidencias con línea y columna.
        Cadena orden;
        cadena_iniciar(&orden);
        cadena_printf(&orden, "s %s", termino);
        if (orden.datos) comando_procesar_orden(e, orden.datos);
        cadena_liberar(&orden);

        // 2. Además, y esto es propio del modo visual, llevamos el cursor
        //    hasta la primera coincidencia para poder seguir editando ahí.
        NodoLinea *encontrada = NULL;
        int posX = 0, posY = 0;
        if (estructura_buscar_palabra(e->estructura, termino, &encontrada, &posX, &posY)) {
            e->lineaActual = encontrada;
            e->cursorX = posX;
            e->cursorY = posY;
        }
    }
    terminal_activar_modo_raw();
}

// ============================================================================
// ENRUTADOR PRINCIPAL DE TECLAS (Dispatcher)
// ============================================================================

/**
 * Lee el carácter detectado en la terminal y determina si equivale a un 
 * comando especial (Ctrl + Tecla). Si es así, despacha la orden.
 * 
 * - c: El carácter a evaluar (puede ser imprimible o no imprimible).
 * - e: Puntero al estado global del editor.
 * - Retorna: 1 si el carácter fue consumido como comando, 0 si no.
 */
int comando_manejar_teclado(char c, EstadoEditor *e) {
    
    // 1. Manejo de vistas informativas (Toggles)
    switch (c) {
        case TECLA_CTRL('h'): comando_toggle_ayuda(e);      return 1;
        case TECLA_CTRL('e'): comando_toggle_estructura(e); return 1;
        case TECLA_CTRL('p'): comando_toggle_memoria(e);    return 1;
    }
    
    // Bloquear el resto de comandos si hay una vista modal abierta
    if (e->mostrarAyuda || e->mostrarEstructura || e->mostrarMemoria) {
        return 1;
    }
    
    // 2. Cada comando del enunciado tiene su atajo. Los que llevan argumentos
    //    los piden por pantalla; los de número de línea traen como valor por
    //    defecto la línea donde está el cursor, de modo que pulsar ENTER en
    //    vacío actúa sobre "esta línea".
    char linea_cursor[16];
    snprintf(linea_cursor, sizeof linea_cursor, "%d", e->cursorY + 1);

    switch (c) {
        // Atajos propios del entorno visual
        case TECLA_CTRL('q'): comando_salir(e);           return 1;  // comando q
        case TECLA_CTRL('s'): comando_guardar(e);         return 1;
        case TECLA_CTRL('l'): comando_linea_comandos(e);  return 1;

        // Comandos del enunciado
        case TECLA_CTRL('o'):
            atajo_con_argumento(e, "ABRIR ARCHIVO (comando o)",
                                "o", "Ruta del archivo", NULL);
            return 1;
        case TECLA_CTRL('v'):
            atajo_con_argumento(e, "VER LÍNEA (comando p)",
                                "p", "Número de línea", linea_cursor);
            return 1;
        case TECLA_CTRL('a'):
            atajo_con_argumento(e, "AÑADIR LÍNEA AL FINAL (comando a)",
                                "a", "Texto", NULL);
            return 1;
        case TECLA_CTRL('d'):
            atajo_con_argumento(e, "BORRAR LÍNEA (comando d)",
                                "d", "Número de línea", linea_cursor);
            return 1;
        case TECLA_CTRL('n'):
            atajo_insertar(e);                                       // comando i
            return 1;
        case TECLA_CTRL('f'):
            comando_buscar(e);                                       // comando s
            return 1;
        case TECLA_CTRL('g'):
            atajo_directo(e, "METADATOS DEL ARCHIVO (comando m)", "m");
            return 1;
        case TECLA_CTRL('y'):
            atajo_con_argumento(e, "COPIAR LÍNEA (comando y)",
                                "y", "Número de línea", linea_cursor);
            return 1;
        case TECLA_CTRL('u'):
            atajo_con_argumento(e, "PEGAR PORTAPAPELES (comando x)",
                                "x", "Insertar en la línea", linea_cursor);
            return 1;
    }
    
    // Retornamos 0 si el carácter ingresado no es un comando registrado
    return 0;
}
