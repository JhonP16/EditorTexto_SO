#include "editor.h"
#include "terminal.h"
#include <stdio.h> // Para constantes y tipos básicos estándar como NULL

/**
 * ========================================================
 * PUNTO DE ENTRADA PRINCIPAL
 * ========================================================
 * Este archivo orquesta la arquitectura del editor, separando
 * la configuración del Sistema Operativo de la Lógica del Editor.
 */
int main(int argc, char *argv[]) {
    // 1. Interacción con el Sistema Operativo (Kernel)
    // Apagamos el procesamiento por defecto de la terminal para
    // tener control absoluto de cada byte que ingresa y sale.
    terminal_activar_modo_raw();
    
    // 2. Inicialización del Estado (Memoria y UI)
    EstadoEditor editor;
    const char* archivo_a_abrir = (argc >= 2) ? argv[1] : NULL;
    editor_inicializar(&editor, archivo_a_abrir);

    // 2.5. Presentación (Pantalla de Bienvenida)
    editor_pantalla_bienvenida(&editor);

    // 3. El Bucle Principal (Game Loop Pattern)
    // En arquitecturas reactivas, el programa itera infinitamente
    // escuchando eventos y re-dibujando la pantalla.
    while (1) {
        // Renderiza el modelo de datos en la terminal visual
        editor_refrescar_pantalla(&editor);
        
        // Bloquea el hilo esperando el siguiente byte del teclado (Llamada 'read')
        editor_procesar_tecla(&editor);
    }

    // 4. Limpieza (Liberación de memoria RAM) al recibir señal de salida
    editor_liberar(&editor);
    return 0;
}
