#include "editor.h"
#include "terminal.h"
#include "comando.h" // Incluimos el nuevo manejador de comandos
#include <unistd.h> // Para llamadas al sistema POSIX como read(), write() y constantes STDIN_FILENO, STDOUT_FILENO
#include <stdlib.h> // Para funciones estándar del sistema como malloc(), free(), exit()
#include <stdio.h>  // Sólo para E/S estándar sobre STDIN/STDOUT (printf, getchar, snprintf)
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
// justo en el centro de la terminal durante un instante al arrancar.
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
    
    // Pie con la pista de las dos formas de dar órdenes al editor.
    char *pista = "Ctrl+H: ayuda con todos los comandos   |   modo CLI clasico: ./editor -c";
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y + num_lineas + 1,
             (e->columnasPantalla / 2) - ((int)strlen(pista) / 2));
    write(STDOUT_FILENO, buf, strlen(buf));
    write(STDOUT_FILENO, pista, strlen(pista));

    // Restauramos el color original por defecto y vaciamos el buffer a la terminal
    write(STDOUT_FILENO, "\x1b[0m", 4);

    // --- CALL SYSTEM: sleep() ---
    // Le pide al Sistema Operativo que suspenda la ejecución de este hilo
    // antes de continuar. Un segundo basta para ver la presentación sin
    // volverla molesta cada vez que se abre el editor.
    sleep(1);
}

static void configurar_archivo(EstadoEditor *e, const char* nombreArchivo) {
    // Sin argumento se trabaja sobre archivo.txt, que open() creará si no existe.
    const char *ruta = nombreArchivo ? nombreArchivo : "archivo.txt";
    e->nombreArchivo = duplicar_cadena(ruta);

    // Abrimos el archivo con open(O_RDWR|O_CREAT): a partir de aquí los
    // comandos de la línea Ctrl+L pueden operar sobre los bytes reales del disco.
    if (archivo_abrir(&e->archivo, ruta) == -1) return;

    char *log_syscall = malloc(65536);
    if (!log_syscall) {
        perror("malloc (log de carga)");
        return;
    }
    log_syscall[0] = '\0';

    if (estructura_cargar_archivo(e->estructura, ruta, log_syscall) == 0 &&
        log_syscall[0] != '\0') {
        comando_mostrar_log_sistema("REPORTE DETALLADO (CARGAR ARCHIVO)", log_syscall);
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
    e->desplazamientoFila = 0;
    e->mostrarAyuda = 0;
    e->mostrarEstructura = 0;
    e->mostrarMemoria = 0;
    e->modificado = 0;
    e->salir = 0;
    e->nombreArchivo = NULL;
    archivo_iniciar(&e->archivo);
    portapapeles_iniciar(&e->portapapeles);

    if (terminal_obtener_tamano(&e->filasPantalla, &e->columnasPantalla) == -1) {
        e->filasPantalla = 24; e->columnasPantalla = 80;
    }

    e->estructura = estructura_crear();
    if (!e->estructura) {
        // Sin memoria para el documento no hay editor posible: salimos limpio.
        e->salir = 1;
        return;
    }

    configurar_archivo(e, nombreArchivo);
    posicionar_cursor_inicial(e);
}

void editor_liberar(EstadoEditor *e) {
    if (!e) return;
    estructura_destruir(e->estructura);
    e->estructura = NULL;
    archivo_cerrar(&e->archivo);          // close() del descriptor + índice
    portapapeles_limpiar(&e->portapapeles);
    free(e->nombreArchivo);
    e->nombreArchivo = NULL;
}

/**
 * Recarga la lista enlazada desde el archivo en disco.
 * Se llama tras cada comando de la línea Ctrl+L, porque esos comandos escriben
 * directamente sobre los bytes del archivo: en ese instante el disco va por
 * delante de la vista en memoria y hay que volver a sincronizarlas.
 */
void editor_recargar_desde_disco(EstadoEditor *e) {
    if (!e || !archivo_esta_abierto(&e->archivo)) return;

    // Si el usuario cambió de archivo con ':o', la vista lo sigue.
    if (!e->nombreArchivo || strcmp(e->nombreArchivo, e->archivo.ruta) != 0) {
        free(e->nombreArchivo);
        e->nombreArchivo = duplicar_cadena(e->archivo.ruta);
    }

    // Recordamos en qué fila estaba el usuario: tras un Ctrl+D o un Ctrl+Y
    // resulta desconcertante que el cursor salte al final del archivo.
    int fila_previa = e->cursorY;

    estructura_cargar_archivo(e->estructura, e->archivo.ruta, NULL);

    // Reubicamos el cursor lo más cerca posible de donde estaba, recortando
    // si el archivo encogió (por ejemplo, si se borró la última línea).
    int destino = fila_previa;
    if (destino >= e->estructura->totalLineas) destino = e->estructura->totalLineas - 1;
    if (destino < 0) destino = 0;

    e->lineaActual = e->estructura->cabeza;
    for (int i = 0; i < destino && e->lineaActual && e->lineaActual->siguiente; i++)
        e->lineaActual = e->lineaActual->siguiente;

    e->cursorY = destino;
    e->cursorX = 0;
    e->desplazamientoFila = 0;
    e->modificado = 0;
}

/**
 * Vuelca la lista enlazada al disco (lo que hace Ctrl+S) y reindexa el
 * descriptor, porque el guardado reescribe el archivo por otra vía.
 */
int editor_guardar_en_disco(EstadoEditor *e, char *log_opcional) {
    if (!e || !e->nombreArchivo) return -1;
    if (estructura_guardar_archivo(e->estructura, e->nombreArchivo, log_opcional) == -1)
        return -1;

    e->modificado = 0;
    if (archivo_esta_abierto(&e->archivo)) archivo_indexar(&e->archivo);
    return 0;
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
    char *comandos = " Ctrl-H(Ayuda) | Ctrl-L(Comandos) | Ctrl-D(Borrar) | Ctrl-A(Anadir) | Ctrl-G(Meta) | Ctrl-S(Guardar) | Ctrl-Q(Salir)";

    // Preparamos la cadena de identificación (Editor, Archivo y Cursor) unida a los comandos
    int len_estado = snprintf(estado, sizeof(estado),
        " [Editor EAFIT] Archivo: %s%s | Fila: %d, Col: %d |%s",
        e->nombreArchivo ? e->nombreArchivo : "[Nuevo]",
        e->modificado ? " *" : "",
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
        if (!e->lineaActual) return;                 // sin memoria: no movemos el cursor
        e->cursorX = 0; e->cursorY++;
        e->modificado = 1;
    } else if (c == 127) {
        if (e->cursorX > 0) {
            linea_eliminar_caracter(e->lineaActual, e->cursorX - 1); e->cursorX--;
            e->modificado = 1;
        }
    } else if (c >= 32 && c <= 126) {
        linea_insertar_caracter(e->lineaActual, e->cursorX, c); e->cursorX++;
        e->modificado = 1;
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
    if (read(STDIN_FILENO, &c, 1) != 1) return;   // -1 (error) o 0 (fin de entrada)

    // Delegar el procesamiento de comandos de control al nuevo módulo
    if (comando_manejar_teclado(c, e)) return;

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return;
        if (seq[0] == '[') editor_mover_cursor(e, seq[1]);
        return;
    }
    manejar_escritura(c, e);
}
