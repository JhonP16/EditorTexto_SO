#include "editor.h"
#include "terminal.h"
#include "comando.h" // Incluimos el nuevo manejador de comandos
#include <unistd.h> // Para llamadas al sistema POSIX como read(), write() y constantes STDIN_FILENO, STDOUT_FILENO
#include <stdlib.h> // Para funciones estándar del sistema como malloc(), free(), exit()
#include <stdio.h>  // Para operaciones de E/S, buffers y archivos (printf, getchar, fopen, fgets, sscanf)
#include <string.h> // Para manipulación de strings y bloques de memoria (strlen, strcpy, strncmp, strchr, etc.)

void editor_pantalla_limpiar(void) {
    // 1. Limpiar pantalla completa y colocar cursor al inicio
    // =========================================================================
    // --- CALL SYSTEM (LLAMADA AL SISTEMA): write() ---
    // 'write' le instruye al Kernel que tome los bytes de nuestra memoria RAM
    // y los envíe al periférico asociado al descriptor STDOUT_FILENO (1), es 
    // decir, la pantalla de la terminal.
    // =========================================================================
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    // 2. Aplicar colores del tema actual
    write(STDOUT_FILENO, TEMA_FONDO, strlen(TEMA_FONDO));
    write(STDOUT_FILENO, TEMA_TEXTO, strlen(TEMA_TEXTO));
}

// =========================================================================
// PANTALLA DE BIENVENIDA (SPLASH SCREEN CON ASCII ART)
// =========================================================================
// Muestra el mensaje "Editor EAFIT" estilizado como ASCII Art
// justo en el centro de la terminal por 5 segundos.
void editor_pantalla_bienvenida(EstadoEditor *e) {
    editor_pantalla_limpiar();
    
    char *ascii_art[] = {
        "███████╗██████╗ ██╗████████╗ ██████╗ ██████╗ ",
        "██╔════╝██╔══██╗██║╚══██╔══╝██╔═══██╗██╔══██╗",
        "█████╗  ██║  ██║██║   ██║   ██║   ██║██████╔╝",
        "██╔══╝  ██║  ██║██║   ██║   ██║   ██║██╔══██╗",
        "███████╗██████╔╝██║   ██║   ╚██████╔╝██║  ██║",
        "╚══════╝╚═════╝ ╚═╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝",
        "                                             ",
        "      ███████╗ █████╗ ███████╗██╗████████╗   ",
        "      ██╔════╝██╔══██╗██╔════╝██║╚══██╔══╝   ",
        "      █████╗  ███████║█████╗  ██║   ██║      ",
        "      ██╔══╝  ██╔══██║██╔══╝  ██║   ██║      ",
        "      ███████╗██║  ██║██║     ██║   ██║      ",
        "      ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝   ╚═╝      "
    };
    
    int num_lineas = 13;
    int ancho_visual = 45; // Ancho visual en columnas (fijo, sin contar bytes UTF-8 extra)
    
    // Calculamos las coordenadas para el bloque de 13x45 caracteres
    int start_y = (e->filasPantalla / 2) - (num_lineas / 2);
    int start_x = (e->columnasPantalla / 2) - (ancho_visual / 2);
    
    char buf[64];
    
    // Escribimos el mensaje usando color (Cyan brillante \x1b[1;36m)
    write(STDOUT_FILENO, "\x1b[1;36m", 7);
    
    for (int i = 0; i < num_lineas; i++) {
        // --- CALL SYSTEM: write() ---
        // Posicionamos el cursor fila por fila en la coordenada calculada
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y + i, start_x);
        write(STDOUT_FILENO, buf, strlen(buf));
        
        // Escribimos la línea de ASCII Art (strlen se calcula en bytes dinámicamente)
        write(STDOUT_FILENO, ascii_art[i], strlen(ascii_art[i]));
    }
    
    // Restauramos el color original por defecto y vaciamos el buffer a la terminal
    write(STDOUT_FILENO, "\x1b[0m", 4);
    
    // --- CALL SYSTEM: sleep() ---
    // Le pide al Sistema Operativo que suspenda la ejecución de este hilo 
    // exactamente por 5 segundos antes de continuar.
    sleep(5);
}

static void configurar_archivo(EstadoEditor *e, const char* nombreArchivo) {
    char *log_syscall = malloc(65536);
    log_syscall[0] = '\0';
    
    if (nombreArchivo) {
        e->nombreArchivo = malloc(strlen(nombreArchivo) + 1);
        strcpy(e->nombreArchivo, nombreArchivo);
        estructura_cargar_archivo(e->estructura, nombreArchivo, log_syscall);
        if (strlen(log_syscall) > 0) {
            comando_mostrar_log_sistema("REPORTE DETALLADO (CARGAR ARCHIVO)", log_syscall);
        }
    } else {
        e->nombreArchivo = malloc(strlen("archivo.txt") + 1);
        strcpy(e->nombreArchivo, "archivo.txt");
    }
    free(log_syscall);
}

static void posicionar_cursor_inicial(EstadoEditor *e) {
    e->lineaActual = e->estructura->cola;
    e->cursorY = e->estructura->totalLineas > 0 ? e->estructura->totalLineas - 1 : 0;
    e->cursorX = e->lineaActual ? e->lineaActual->longitud : 0;
}

void editor_inicializar(EstadoEditor *e, const char* nombreArchivo) {
    e->cursorX = 0;
    e->cursorY = 0;
    e->estructura = estructura_crear();
    e->desplazamientoFila = 0;
    e->mostrarAyuda = 0;
    e->mostrarEstructura = 0;
    e->mostrarMemoria = 0;
    configurar_archivo(e, nombreArchivo);
    posicionar_cursor_inicial(e);
    if (terminal_obtener_tamano(&e->filasPantalla, &e->columnasPantalla) == -1) {
        e->filasPantalla = 24; e->columnasPantalla = 80;
    }
}

void editor_liberar(EstadoEditor *e) {
    estructura_destruir(e->estructura);
    if (e->nombreArchivo) free(e->nombreArchivo);
}

static void pantalla_ajustar_scroll(EstadoEditor *e) {
    if (e->cursorY < e->desplazamientoFila) e->desplazamientoFila = e->cursorY;
    if (e->cursorY >= e->desplazamientoFila + e->filasPantalla - 1) {
        e->desplazamientoFila = e->cursorY - e->filasPantalla + 2;
    }
}

static void pantalla_dibujar_linea(NodoLinea *linea) {
    NodoPalabra *pal = linea->palabras_cabeza;
    while (pal) { write(STDOUT_FILENO, pal->texto, pal->longitud); pal = pal->siguiente; }
}

static void pantalla_dibujar_todas_las_filas(EstadoEditor *e) {
    NodoLinea *actual = e->estructura->cabeza;
    for (int i = 0; i < e->desplazamientoFila && actual != NULL; i++) actual = actual->siguiente;
    for (int y = 0; y < e->filasPantalla - 1; y++) {
        if (actual) { pantalla_dibujar_linea(actual); actual = actual->siguiente; } 
        else { write(STDOUT_FILENO, TEMA_LINEA_VACIA, strlen(TEMA_LINEA_VACIA)); }
        write(STDOUT_FILENO, "\r\n", 2);
    }
}

// Función que dibuja la barra inferior (Status Bar) del editor.
// Muestra la identificación del programa, el archivo actual, la posición del cursor y los atajos.
static void pantalla_dibujar_barra_estado(EstadoEditor *e) {
    char estado[1024];
    char *comandos = " Ctrl-S(Guarda) | Ctrl-Q(Salir) | Ctrl-H(Ayuda) | Ctrl-E(Estruct) | Ctrl-P(Mem) | Ctrl-F(Buscar)";
    
    // Preparamos la cadena de identificación (Editor, Archivo y Cursor) unida a los comandos
    int len_estado = snprintf(estado, sizeof(estado), 
        " [Editor EAFIT] Archivo: %s | Fila: %d, Col: %d |%s", 
        e->nombreArchivo ? e->nombreArchivo : "[Nuevo]", 
        e->cursorY + 1, e->cursorX + 1, comandos);
    
    // 1. Aplicamos el color de la barra de estado definida en el Tema (ej. Invertido)
    write(STDOUT_FILENO, TEMA_BARRA_ESTADO, strlen(TEMA_BARRA_ESTADO));
    
    int len = len_estado;
    int max_col = e->columnasPantalla;
    if (len > max_col) len = max_col; // Truncamos si la terminal es más estrecha que el texto
    
    // 2. Dibujamos la cadena de estado construida
    write(STDOUT_FILENO, estado, len);
    
    // 3. Rellenamos el resto de la fila con espacios para que la barra cruce toda la pantalla
    while (len < max_col) { write(STDOUT_FILENO, " ", 1); len++; }
    
    // 4. Restauramos el formato original para no afectar el dibujo de la consola original al salir
    write(STDOUT_FILENO, TEMA_RESTAURAR, strlen(TEMA_RESTAURAR));
    write(STDOUT_FILENO, TEMA_FONDO, strlen(TEMA_FONDO)); // Mantenemos el fondo del tema
    write(STDOUT_FILENO, TEMA_TEXTO, strlen(TEMA_TEXTO)); // Mantenemos el texto del tema
}

static void pantalla_posicionar_cursor(EstadoEditor *e) {
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (e->cursorY - e->desplazamientoFila) + 1, e->cursorX + 1);
    write(STDOUT_FILENO, buf, strlen(buf));
}

void editor_refrescar_pantalla(EstadoEditor *e) {
    editor_pantalla_limpiar();
    
    // Si se activó alguna pantalla modal (comandos), cedemos el control a comando.c y abortamos el dibujado
    if (comando_dibujar_modal(e)) return;
    
    pantalla_ajustar_scroll(e);
    pantalla_dibujar_todas_las_filas(e);
    pantalla_dibujar_barra_estado(e);
    pantalla_posicionar_cursor(e);
}

void editor_mover_cursor(EstadoEditor *e, char tecla) {
    switch (tecla) {
        case 'A': case 'w':
            if (e->cursorY > 0 && e->lineaActual->anterior) {
                e->cursorY--; e->lineaActual = e->lineaActual->anterior;
                if ((size_t)e->cursorX > e->lineaActual->longitud) e->cursorX = e->lineaActual->longitud;
            } break;
        case 'B': case 's':
            if (e->lineaActual->siguiente) {
                e->cursorY++; e->lineaActual = e->lineaActual->siguiente;
                if ((size_t)e->cursorX > e->lineaActual->longitud) e->cursorX = e->lineaActual->longitud;
            } break;
        case 'C': case 'd':
            if ((size_t)e->cursorX < e->lineaActual->longitud) e->cursorX++;
            break;
        case 'D': case 'a':
            if (e->cursorX > 0) e->cursorX--;
            break;
    }
}

static void manejar_escritura(char c, EstadoEditor *e) {
    if (c == '\r' || c == '\n') {
        e->lineaActual = estructura_insertar_linea(e->estructura, e->lineaActual);
        e->cursorX = 0; e->cursorY++;
    } else if (c == 127) { 
        if (e->cursorX > 0) { linea_eliminar_caracter(e->lineaActual, e->cursorX - 1); e->cursorX--; }
    } else if (c >= 32 && c <= 126) {
        linea_insertar_caracter(e->lineaActual, e->cursorX, c); e->cursorX++;
    }
}

void editor_procesar_tecla(EstadoEditor *e) {
    char c = '\0';
    // =========================================================================
    // --- CALL SYSTEM (LLAMADA AL SISTEMA): read() ---
    // 'read' es la interfaz directa entre nuestro programa y el Kernel del OS.
    // Pide al SO que suspenda el programa hasta que el hardware (teclado)
    // envíe datos por el descriptor STDIN_FILENO (0).
    // =========================================================================
    if (read(STDIN_FILENO, &c, 1) == -1) return;
    
    // Delegar el procesamiento de comandos de control al nuevo módulo
    if (comando_manejar_teclado(c, e)) return;
    
    if (c == '\x1b') { 
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) == 0) return;
        if (read(STDIN_FILENO, &seq[1], 1) == 0) return;
        if (seq[0] == '[') editor_mover_cursor(e, seq[1]);
        return;
    } 
    manejar_escritura(c, e);
}
