#ifndef COMANDO_H
#define COMANDO_H

#include "editor.h"

/**
 * Macro para decodificar las teclas presionadas junto con la tecla Ctrl.
 * 
 * En la terminal, presionar Ctrl + [letra] genera un código ASCII equivalente 
 * a la letra pero con los 3 bits más significativos apagados (es decir, 
 * hace un AND a nivel de bits con 0x1f o 31 en decimal).
 * 
 * - k: El carácter a evaluar (por ejemplo 'q' para Ctrl+Q).
 */
#define TECLA_CTRL(k) ((k) & 0x1f)

/**
 * Función principal que procesa y enruta los comandos de control del usuario.
 * 
 * Actúa como un "dispatcher". Identifica si el carácter introducido corresponde 
 * a un atajo de teclado (Ctrl+X) e invoca la función modular correspondiente.
 * 
 * - c: El carácter presionado por el usuario.
 * - e: Puntero al estado global del editor.
 * - Retorna: 1 si el carácter fue un comando (se procesó), 0 si es texto normal.
 */
int comando_manejar_teclado(char c, EstadoEditor *e);

/**
 * Dibuja un cuadro de registro (log) del sistema que bloquea la pantalla 
 *        hasta que el usuario presione ENTER.
 * 
 * Utilizada para mostrar retroalimentación de operaciones críticas del sistema, 
 * como los resultados de guardar o cargar archivos, llamadas al sistema (syscalls), etc.
 * 
 * - titulo: El título de la pantalla de log.
 * - log: La cadena con el contenido del log a imprimir.
 */
void comando_mostrar_log_sistema(const char *titulo, const char *log);

/**
 * Determina y dibuja la interfaz modal activa (Ayuda, Estructura o Memoria).
 * 
 * Si el usuario activó una de las pantallas especiales (con Ctrl+H, Ctrl+E o Ctrl+P), 
 * esta función se encarga de renderizar su contenido, evadiendo el renderizado 
 * normal del texto.
 * 
 * - e: Puntero al estado global del editor.
 * - Retorna: 1 si se dibujó un modal, 0 si no hay ningún modal activo.
 */
int comando_dibujar_modal(EstadoEditor *e);

#endif
