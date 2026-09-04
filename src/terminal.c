#include "terminal.h"
#include <termios.h>   // Para las estructuras y funciones de control de la terminal (tcgetattr, tcsetattr)
#include <unistd.h>    // Para constantes de descriptores de archivos del estándar POSIX (STDIN_FILENO, etc.)
#include <stdlib.h>    // Para utilidades estándar, como registrar funciones de limpieza al salir (atexit)
#include <sys/ioctl.h> // Para la llamada al sistema ioctl() que permite consultar dimensiones de la ventana (TIOCGWINSZ)

// Guardamos el estado original de la terminal para restaurarlo al salir.
struct termios termios_original;

/**
 * ESTRATEGIA CON SYSTEM CALLS:
 * Cuando usamos `tcsetattr`, hacemos una llamada al sistema para reconfigurar el 
 * controlador del dispositivo TTY en el Kernel.
 */
/* =========================================================================
 * CONCEPTOS CLAVE: TTY (Teletype) y File Descriptors
 * =========================================================================
 * 1. ¿Qué es un TTY?
 *    La sigla viene históricamente de 'Teletypewriter' (máquinas de escribir
 *    electromecánicas). Hoy en día en Linux, TTY es el subsistema del Kernel
 *    que controla tu terminal virtual. Por defecto, el TTY opera en modo 
 *    'Cooked' (cocinado), lo que significa que el SO estructuraa en un buffer 
 *    todo lo que escribes hasta que presionas ENTER, y además procesa señales 
 *    especiales como Ctrl+C (matar) antes de que el programa las reciba.
 *
 * 2. ¿Qué son STDIN_FILENO y STDOUT_FILENO?
 *    En la filosofía Unix/Linux 'Todo es un archivo'. Cuando arranca tu programa,
 *    el Sistema Operativo le conecta automáticamente 3 archivos abiertos (descriptores):
 *      - STDIN_FILENO  (Valor 0): Entrada Estándar. Es el TTY recibiendo del teclado.
 *      - STDOUT_FILENO (Valor 1): Salida Estándar. Es el TTY dibujando en pantalla.
 *      - STDERR_FILENO (Valor 2): Salida de Errores (para diagnósticos separados).
 * ========================================================================= */

void terminal_desactivar_modo_raw() {
    // Restauramos los atributos originales de la entrada estándar (teclado).
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_original);
}

void terminal_activar_modo_raw() {
    // 1. Obtener la configuración actual del Kernel
    tcgetattr(STDIN_FILENO, &termios_original);
    
    // 2. Registrar la función de restauración para que se ejecute si el programa termina
    atexit(terminal_desactivar_modo_raw);

    struct termios raw = termios_original;
    
    // 3. Modificar las banderas (flags) del controlador:
    // ICANON: Desactiva el modo canónico (esperar a que el usuario presione Enter).
    // ECHO: Evita que las teclas presionadas se impriman automáticamente en pantalla.
    // ISIG: Desactiva señales como Ctrl+C (SIGINT) y Ctrl+Z (SIGTSTP).
    // IXON: Desactiva el control de flujo de software (Ctrl+S / Ctrl+Q heredados).
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    
    // 4. Configurar el comportamiento de la llamada al sistema `read()`:
    raw.c_cc[VMIN] = 1;  // read() se bloqueará hasta que llegue al menos 1 byte
    raw.c_cc[VTIME] = 0; // Sin límite de tiempo (espera infinita)

    // 5. Aplicar la nueva configuración al Kernel
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/**
 * ESTRATEGIA CON IOCTL:
 * Input/Output Control (ioctl) es una llamada al sistema "comodín" que permite 
 * enviar comandos de hardware a los drivers de dispositivos (en este caso, la terminal visual).
 */
int terminal_obtener_tamano(int *filas, int *columnas) {
    struct winsize ws;
    // TIOCGWINSZ = Terminal I/O Control Get Window Size
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1; // Falló la llamada al sistema
    } else {
        *columnas = ws.ws_col;
        *filas = ws.ws_row;
        return 0;  // Éxito
    }
}
