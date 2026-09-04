#include "comando.h"
#include "terminal.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // Para printf, fgets, etc.

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
        "=================================================\r\n"
        "            AYUDA DE COMANDOS DEL EDITOR         \r\n"
        "=================================================\r\n\r\n"
        "  Ctrl + S : Guardar el documento actual\r\n"
        "  Ctrl + Q : Salir del editor\r\n"
        "  Ctrl + H : Mostrar / Ocultar esta ayuda\r\n"
        "  Ctrl + E : Mostrar Estructura de Datos\r\n"
        "  Ctrl + F : Buscar palabra\r\n\r\n"
        "  Flechas  : Navegar por el texto\r\n"
        "  W A S D  : Navegación alternativa\r\n\r\n"
        "=================================================\r\n"
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
    
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) {
        char *error = "\x1b[1;31mError al abrir /proc/self/status\x1b[0m\r\n";
        write(STDOUT_FILENO, error, strlen(error));
        return;
    }
    
    char linea[512];
    char out_buf[1024];
    while (fgets(linea, sizeof(linea), fp)) {
        if (strncmp(linea, "VmPeak:", 7) == 0 ||
            strncmp(linea, "VmSize:", 7) == 0 ||
            strncmp(linea, "VmRSS:", 6) == 0 ||
            strncmp(linea, "VmData:", 7) == 0 ||
            strncmp(linea, "VmStk:", 6) == 0 ||
            strncmp(linea, "VmExe:", 6) == 0) {
            
            linea[strcspn(linea, "\n")] = '\0';
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
    fclose(fp);
    
    char *map_enc = 
        "\r\n\x1b[1;36m==== MAPA DE SEGMENTOS VIRTUALES (/proc/self/maps) ====\x1b[0m\r\n"
        "\x1b[1;37mRango de Direcciones (Hex)          Permisos   Segmento        Concepto de S.O.\x1b[0m\r\n"
        "---------------------------------------------------------------------------------------\r\n";
    write(STDOUT_FILENO, map_enc, strlen(map_enc));
    
    FILE *fpm = fopen("/proc/self/maps", "r");
    if (fpm) {
        int line_count = 0;
        while (fgets(linea, sizeof(linea), fpm) && line_count < 15) {
            if (strstr(linea, "[heap]") || strstr(linea, "[stack]") || strstr(linea, "editor")) {
                char addr[64]="", perms[16]="", offset[32]="", dev[16]="", inode[32]="", path[256]="";
                int vars = sscanf(linea, "%63s %15s %31s %15s %31s %255s", addr, perms, offset, dev, inode, path);
                
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
        fclose(fpm);
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
 * Termina la ejecución del programa de forma segura.
 * Restaura el estilo de colores original de la terminal del usuario.
 */
static void comando_salir(void) {
    // Restaurar tema original de la terminal del usuario antes de salir
    write(STDOUT_FILENO, TEMA_RESTAURAR, strlen(TEMA_RESTAURAR));
    editor_pantalla_limpiar(); 
    exit(0);
}

/**
 * Ejecuta el proceso de guardado del documento en el disco duro.
 * Extrae los registros (syscall logs) generados por el almacén durante 
 * el proceso y se los muestra al usuario.
 */
static void comando_guardar(EstadoEditor *e) {
    char *log_syscall = malloc(65536);
    log_syscall[0] = '\0';
    estructura_guardar_archivo(e->estructura, e->nombreArchivo, log_syscall);
    comando_mostrar_log_sistema("REPORTE DETALLADO (GUARDAR ARCHIVO)", log_syscall);
    free(log_syscall);
}

/**
 * Detiene la edición fluida para iniciar un prompt de búsqueda interactiva.
 * Solicita una palabra al usuario, la busca en el almacén y, de encontrarla,
 * reposiciona el cursor exactamente sobre ella.
 */
static void comando_buscar(EstadoEditor *e) {
    editor_pantalla_limpiar();
    terminal_desactivar_modo_raw();
    printf("Buscar palabra: "); fflush(stdout);
    char termino[128];
    if (fgets(termino, sizeof(termino), stdin) != NULL) {
        size_t len = strlen(termino);
        if (len > 0 && termino[len-1] == '\n') termino[len-1] = '\0';
        NodoLinea *encontrada = NULL;
        int posX = 0, posY = 0;
        if (estructura_buscar_palabra(e->estructura, termino, &encontrada, &posX, &posY)) {
            e->lineaActual = encontrada; e->cursorX = posX; e->cursorY = posY;
        } else {
            printf("\nLa palabra '%s' no se encuentra.\nPresione ENTER para continuar...", termino);
            fflush(stdout); getchar(); 
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
    
    // 2. Manejo de comandos de edición (Solo operan sobre el documento)
    switch (c) {
        case TECLA_CTRL('q'): comando_salir();       return 1;
        case TECLA_CTRL('s'): comando_guardar(e);    return 1;
        case TECLA_CTRL('f'): comando_buscar(e);     return 1;
    }
    
    // Retornamos 0 si el carácter ingresado no es un comando registrado
    return 0;
}
