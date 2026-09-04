#ifndef TERMINAL_H
#define TERMINAL_H

/**
 * Módulo: Terminal
 * Propósito: Aislar la interacción de bajo nivel con el Sistema Operativo (Linux).
 * 
 * En un curso de Sistemas Operativos, es fundamental entender que C estándar 
 * (printf, scanf) utiliza buffers gestionados por la librería de C. Para crear
 * un editor interactivo, necesitamos comunicarnos directamente con el Kernel 
 * mediante System Calls (Llamadas al Sistema).
 */

// Activa el "Raw Mode" (Modo Crudo) en la terminal.
void terminal_activar_modo_raw();

// Desactiva el "Raw Mode" restaurando la terminal a su estado original.
void terminal_desactivar_modo_raw();

// Utiliza la System Call ioctl para preguntar al SO por las dimensiones de la ventana.
int terminal_obtener_tamano(int *filas, int *columnas);

#endif
