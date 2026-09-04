#include "editor.h"
#include "terminal.h"
#include "cli.h"
#include <unistd.h>  // isatty(), STDIN_FILENO
#include <string.h>  // strcmp
#include <stdio.h>   // printf, fprintf

/**
 * ========================================================
 * PUNTO DE ENTRADA PRINCIPAL
 * ========================================================
 * El editor tiene dos caras, y las dos ejecutan exactamente los mismos
 * comandos (src/cli.c); sólo cambia cómo se piden y cómo se muestran:
 *
 *   MODO VISUAL   Pantalla completa. Cada comando del enunciado tiene su
 *                 atajo de teclado (Ctrl+D borrar, Ctrl+A añadir, ...) que
 *                 pide por pantalla los argumentos que necesite; Ctrl+L abre
 *                 además el prompt libre para escribir cualquier comando.
 *
 *   MODO CLI      Intérprete de línea clásico, al estilo de 'ed': un prompt
 *                 donde se escriben los comandos tal cual los pide el
 *                 enunciado ("p 2", "d 2", "i 1 hola", "q").
 *
 * Cuál de los dos arranca se decide así:
 *   1. La bandera -c / --cli fuerza el modo CLI.
 *   2. Si no, isatty(STDIN_FILENO) le pregunta al kernel si el descriptor 0
 *      está conectado a un TTY. Con teclado -> visual. Con tubería -> CLI,
 *      lo que hace al editor automatizable desde un guión o desde un shell.
 */

static void imprimir_uso(const char *programa) {
    printf("Uso: %s [-c|--cli] [archivo]\n\n", programa);
    printf("  (sin banderas)   Editor visual a pantalla completa. Cada comando\n");
    printf("                   tiene su atajo Ctrl y pide los argumentos que\n");
    printf("                   necesite; Ctrl+L abre la línea de comandos libre.\n\n");
    printf("  -c, --cli        Intérprete de comandos de línea, al estilo de 'ed'.\n\n");
    printf("Si la entrada estándar no es un teclado sino una tubería, se usa el\n");
    printf("modo CLI automáticamente, lo que permite automatizar el editor:\n\n");
    printf("    printf 'a hola\\np\\nq\\n' | %s notas.txt\n\n", programa);
    printf("%s", cli_texto_ayuda());
}

int main(int argc, char *argv[]) {
    const char *ruta = NULL;
    int forzar_cli = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            imprimir_uso(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--cli") == 0) {
            forzar_cli = 1;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "Opción desconocida: %s\n", argv[i]);
            fprintf(stderr, "Pruebe '%s --help'.\n", argv[0]);
            return 1;
        }
        ruta = argv[i];
    }

    // --- CALL SYSTEM: isatty() ---
    // Le pregunta al kernel si el descriptor 0 está conectado a un dispositivo
    // TTY. Es la misma consulta que usan 'ls' o 'grep' para decidir si colorean.
    if (forzar_cli || !isatty(STDIN_FILENO)) {
        return cli_repl(ruta);
    }

    // 1. Interacción con el Sistema Operativo (Kernel)
    // Apagamos el procesamiento por defecto de la terminal para
    // tener control absoluto de cada byte que ingresa y sale.
    terminal_activar_modo_raw();

    // 2. Inicialización del Estado (Memoria, disco y UI)
    EstadoEditor editor;
    editor_inicializar(&editor, ruta);

    // 2.5. Presentación (Pantalla de Bienvenida)
    editor_pantalla_bienvenida(&editor);

    // 3. El Bucle Principal (Game Loop Pattern)
    // Itera escuchando eventos y re-dibujando la pantalla hasta que el usuario
    // pide salir. A diferencia de un while(1) con exit(), salir por aquí deja
    // que el paso 4 libere toda la memoria: el requisito del comando q.
    while (!editor.salir) {
        editor_refrescar_pantalla(&editor);
        editor_procesar_tecla(&editor);
    }

    // 4. Limpieza: cierre de descriptores y liberación de memoria RAM
    editor_pantalla_limpiar();
    editor_liberar(&editor);
    return 0;
}
