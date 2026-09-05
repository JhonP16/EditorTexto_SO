#include "shell.h"
#include <unistd.h>     /* fork, execv, dup2, pipe, close, write, access, isatty */
#include <sys/types.h>
#include <sys/wait.h>   /* waitpid, WIFEXITED, WEXITSTATUS */
#include <termios.h>    /* tcgetattr, tcsetattr: salvaguarda del estado del TTY */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * ====================================================================================
 * CATEGORÍA: APLICACIONES  (cat_aplicaciones.c)
 * ====================================================================================
 * Esta categoría no existía en el shell original. Agrupa los comandos que lanzan
 * PROGRAMAS EXTERNOS INTERACTIVOS: aplicaciones con vida propia a las que el shell
 * les cede la terminal y espera hasta que terminan.
 *
 * ¿Por qué una categoría nueva y no 'datos' o 'monitoreo'?
 *
 *   - Las categorías existentes agrupan comandos que DEMUESTRAN una syscall y
 *     retornan de inmediato: d_read abre, lee, imprime y vuelve al prompt. El
 *     editor no es la demostración de una syscall: es un programa completo, de
 *     duración indefinida, que además se apodera de la terminal poniéndola en
 *     modo crudo (raw).
 *
 *   - Clasificarlo en 'datos' rompería el contrato de esa categoría: 'help datos'
 *     promete comandos que muestran metadatos o vuelcan un archivo, no una
 *     aplicación a pantalla completa que secuestra el teclado.
 *
 *   - Clasificarlo en 'monitoreo' confundiría el MECANISMO con el PROPÓSITO. Es
 *     cierto que lo lanzamos con fork+execv+waitpid, igual que p_exec, pero
 *     'monitoreo' agrupa comandos para observar y manipular procesos y señales.
 *     El editor no observa procesos; fork/exec es simplemente cómo se lanza
 *     cualquier binario externo.
 *
 *   - La razón decisiva es que esta clase de comando le impone al shell un
 *     PROTOCOLO NUEVO: ceder la terminal y recuperarla después. El hijo cambia
 *     los atributos del TTY con tcsetattr y podría morir sin restaurarlos (un
 *     fallo, un kill). Si el shell no guarda y repone ese estado, el proceso
 *     padre queda con la terminal inservible: sin eco, sin Ctrl+C, sin línea
 *     canónica. Ninguna categoría existente necesita ese cuidado.
 *
 * Una categoría es un contrato sobre cómo el shell trata al comando. Este
 * contrato -- ceder terminal, esperar, restaurar -- es nuevo, y por eso la
 * categoría también lo es.
 */

/* Rutas donde se buscará el binario del editor, en orden de preferencia. */
static const char *RUTAS_CANDIDATAS[] = {
    "./editor",          /* el editor está junto al shell */
    "../editor",         /* el shell vive en un subdirectorio del editor */
    "../editorTexto/editor"
};

/**
 * Localiza el ejecutable del editor.
 *
 * La variable de entorno EDITOR_SO tiene prioridad, de modo que el usuario
 * puede apuntar a cualquier ruta sin recompilar el shell.
 *
 * Usa access(2) con X_OK, que le pregunta al kernel si el proceso tiene permiso
 * de ejecución sobre esa ruta. Comprobarlo aquí permite dar un error claro en
 * el padre, en vez de descubrirlo en el hijo cuando execv ya falló.
 */
static const char *resolver_editor(void) {
    const char *desde_entorno = getenv("EDITOR_SO");

    if (desde_entorno != NULL && desde_entorno[0] != '\0') {
        LOG_SYSCALL("access", "\"%s\", X_OK", desde_entorno);
        if (access(desde_entorno, X_OK) == 0) {
            LOG_SYSCALL_RESULT(0);
            return desde_entorno;
        }
        LOG_SYSCALL_ERROR(strerror(errno));
    }

    for (size_t i = 0; i < sizeof(RUTAS_CANDIDATAS) / sizeof(RUTAS_CANDIDATAS[0]); i++) {
        LOG_SYSCALL("access", "\"%s\", X_OK", RUTAS_CANDIDATAS[i]);
        if (access(RUTAS_CANDIDATAS[i], X_OK) == 0) {
            LOG_SYSCALL_RESULT(0);
            return RUTAS_CANDIDATAS[i];
        }
        LOG_SYSCALL_ERROR(strerror(errno));
    }
    return NULL;
}

static void explicar_editor_no_encontrado(void) {
    fprintf(stderr, COLOR_ERROR "No se encontró el binario del editor.\n" COLOR_RESET);
    fprintf(stderr, COLOR_INFO
            "Compílelo con 'make' en el directorio del editor y ejecute el shell\n"
            "desde allí, o indique la ruta exacta:\n" COLOR_RESET);
    fprintf(stderr, COLOR_PARAM "    EDITOR_SO=/ruta/al/editor ./eafitOS\n" COLOR_RESET);
}

/**
 * Informa cómo terminó el proceso hijo, distinguiendo salida normal de muerte
 * por señal. Es la información que un shell de verdad guarda en $?.
 */
static void informar_estado(int status, const char *que) {
    if (WIFEXITED(status)) {
        int codigo = WEXITSTATUS(status);
        if (codigo == 0) {
            printf(COLOR_PROMPT "[Padre]" COLOR_RESET " %s terminó correctamente. Código: "
                   COLOR_RESULT "0" COLOR_RESET "\n", que);
        } else {
            printf(COLOR_PROMPT "[Padre]" COLOR_RESET " %s terminó con código: "
                   COLOR_ERROR "%d" COLOR_RESET "\n", que, codigo);
        }
    } else if (WIFSIGNALED(status)) {
        printf(COLOR_PROMPT "[Padre]" COLOR_RESET " %s fue terminado por la señal: "
               COLOR_ERROR "%d" COLOR_RESET "\n", que, WTERMSIG(status));
        printf(COLOR_INFO "  (por eso el shell repone el estado de la terminal: el editor\n"
               "   murió sin poder restaurarla él mismo)\n" COLOR_RESET);
    }
}

/**
 * ====================================================================================
 * COMANDO: editor [opciones] [archivo]
 * ====================================================================================
 * Lanza el editor de texto en modo interactivo, cediéndole la terminal.
 *
 * Todo lo que el usuario escriba después de 'editor' se le reenvía TAL CUAL al
 * programa, siguiendo la convención de cualquier shell: el shell no interpreta
 * las banderas del binario que lanza, sólo se las entrega.
 *
 *     editor notas.txt        -> el editor visual, a pantalla completa
 *     editor -c notas.txt     -> el intérprete de línea del editor ('editor> ')
 *     editor --help           -> la ayuda del editor
 *
 * Secuencia completa:
 *   1. access()    - localizar el binario y comprobar permiso de ejecución.
 *   2. tcgetattr() - GUARDAR el estado del TTY antes de entregarlo.
 *   3. fork()      - duplicar el proceso.
 *   4. execv()     - en el hijo, reemplazar la imagen por la del editor.
 *   5. waitpid()   - en el padre, esperar (el shell queda detenido, sin competir
 *                    por el teclado con su propio hijo).
 *   6. tcsetattr() - RESTAURAR el TTY pase lo que pase con el hijo.
 */
int cmd_editor(int argc, char **argv) {
    const char *ruta_editor = resolver_editor();
    if (ruta_editor == NULL) {
        explicar_editor_no_encontrado();
        return 1;
    }

    /* Vector de argumentos para execv: el nombre del programa seguido de todo
     * lo que el usuario escribió después de 'editor', y el NULL final que exige
     * la convención POSIX. */
    char **args = malloc((size_t)(argc + 1) * sizeof(*args));
    if (args == NULL) {
        fprintf(stderr, COLOR_ERROR "Sin memoria para preparar los argumentos.\n" COLOR_RESET);
        return 1;
    }
    args[0] = (char *)ruta_editor;
    for (int i = 1; i < argc; i++) args[i] = argv[i];
    args[argc] = NULL;

    /* --- Salvaguarda del terminal -------------------------------------------
     * El editor pondrá el TTY en modo crudo. Si muere sin restaurarlo, el shell
     * heredaría una terminal sin eco ni modo canónico. Guardamos el estado
     * ahora para reponerlo cuando el hijo termine, sea como sea que termine. */
    struct termios estado_terminal;
    int terminal_guardada = 0;
    if (isatty(STDIN_FILENO)) {
        LOG_SYSCALL("tcgetattr", "STDIN_FILENO, &estado");
        if (tcgetattr(STDIN_FILENO, &estado_terminal) == -1) {
            LOG_SYSCALL_ERROR(strerror(errno));
        } else {
            LOG_SYSCALL_RESULT(0);
            terminal_guardada = 1;
        }
    }

    printf(COLOR_INFO "Cediendo la terminal a '%s'" COLOR_RESET, ruta_editor);
    for (int i = 1; i < argc; i++) printf(COLOR_PARAM " %s" COLOR_RESET, argv[i]);
    printf(COLOR_INFO "...\n" COLOR_RESET);

    LOG_SYSCALL("fork", "");

    /* Vaciar los búferes de stdio ANTES de bifurcar, por dos razones:
     *  1. Si quedara texto sin escribir, el hijo heredaría una copia del
     *     búfer y ese texto saldría impreso dos veces.
     *  2. El shell traza con printf (con búfer) mientras el editor escribe
     *     directo al terminal; sin vaciar, la traza del shell aparecería
     *     después de la salida del hijo, en desorden. */
    fflush(NULL);

    pid_t pid = fork();
    if (pid == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        free(args);
        return 1;
    }

    if (pid == 0) {
        /* ---------------- PROCESO HIJO ---------------- */
        execv(ruta_editor, args);

        /* Sólo se llega aquí si execv falló. */
        fprintf(stderr, COLOR_ERROR "\nNo se pudo ejecutar '%s': %s\n" COLOR_RESET,
                ruta_editor, strerror(errno));
        /* _exit y no exit: el hijo comparte los búferes heredados de stdio y
         * exit() los vaciaría por segunda vez, duplicando salida del padre. */
        _exit(127);
    }

    /* ---------------- PROCESO PADRE ---------------- */
    LOG_SYSCALL_RESULT(pid);
    fflush(stdout);
    free(args);   /* el hijo ya tiene su propia copia del vector */

    int status = 0;
    LOG_SYSCALL("waitpid", "%d, &status, 0", pid);
    pid_t esperado = waitpid(pid, &status, 0);
    if (esperado == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
    } else {
        LOG_SYSCALL_RESULT(esperado);
    }

    /* Recuperar la terminal, haya salido bien o mal el editor. */
    if (terminal_guardada) {
        LOG_SYSCALL("tcsetattr", "STDIN_FILENO, TCSAFLUSH, &estado");
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &estado_terminal) == -1)
            LOG_SYSCALL_ERROR(strerror(errno));
        else
            LOG_SYSCALL_RESULT(0);
    }

    if (esperado != -1) informar_estado(status, "El editor");
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: editor_cmd <archivo> "<comandos separados por ;>"
 * ====================================================================================
 * Ejecuta órdenes del editor SIN interacción, canalizándolas por su entrada
 * estándar. Ejemplo:
 *
 *     editor_cmd notas.txt "a primera linea; a segunda linea; p"
 *
 * Esto funciona porque el editor consulta isatty(STDIN_FILENO) al arrancar: al
 * encontrarse con una tubería en vez de un teclado entra solo en su modo de
 * intérprete de comandos. Es el mismo mecanismo con el que el shell puede
 * automatizar cualquier programa bien diseñado.
 *
 * Secuencia:
 *   1. pipe()    - crear el canal de comunicación (dos descriptores).
 *   2. fork()    - duplicar el proceso.
 *   3. dup2()    - en el hijo, ENCHUFAR el extremo de lectura en STDIN.
 *   4. execv()   - convertir al hijo en el editor.
 *   5. write()   - en el padre, inyectar los comandos por el extremo de escritura.
 *   6. close()   - cerrar la escritura: el editor recibe EOF y termina solo.
 *   7. waitpid() - recoger el estado de salida.
 *
 * No hace falta terminar los comandos con 'q': el cierre del extremo de
 * escritura produce el fin de entrada, y el bucle del editor sale limpiamente
 * liberando su memoria y cerrando su descriptor.
 */
int cmd_editor_cmd(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, COLOR_ERROR
                "Uso: editor_cmd <archivo> \"<comandos separados por ;>\"\n" COLOR_RESET);
        fprintf(stderr, COLOR_INFO
                "Ejemplo: editor_cmd notas.txt \"a hola mundo; p; m\"\n" COLOR_RESET);
        return 1;
    }

    const char *ruta_editor = resolver_editor();
    if (ruta_editor == NULL) {
        explicar_editor_no_encontrado();
        return 1;
    }

    const char *archivo = argv[1];
    const char *guion = argv[2];

    /* Traducimos el guion a la forma que espera el editor: una orden por línea.
     * El separador es ';' porque el tokenizador del shell ya usa las comillas
     * para agrupar y los espacios para separar argumentos. */
    size_t largo = strlen(guion);
    char *ordenes = malloc(largo + 2);
    if (ordenes == NULL) {
        fprintf(stderr, COLOR_ERROR "Sin memoria para preparar las órdenes.\n" COLOR_RESET);
        return 1;
    }
    for (size_t i = 0; i < largo; i++)
        ordenes[i] = (guion[i] == ';') ? '\n' : guion[i];
    ordenes[largo] = '\n';           /* la última orden también necesita su salto */
    ordenes[largo + 1] = '\0';
    size_t total_bytes = largo + 1;

    /* 1. Canal de comunicación entre el shell y el editor. */
    int tuberia[2];
    LOG_SYSCALL("pipe", "tuberia[2]");
    if (pipe(tuberia) == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        free(ordenes);
        return 1;
    }
    LOG_SYSCALL_RESULT(0);
    printf(COLOR_INFO "  tuberia[0]=%d (lectura, irá al editor)  "
           "tuberia[1]=%d (escritura, la usa el shell)\n" COLOR_RESET,
           tuberia[0], tuberia[1]);

    LOG_SYSCALL("fork", "");

    /* Vaciar antes de bifurcar: evita duplicar el búfer heredado y mantiene
     * la traza del shell en orden respecto a la salida del editor. */
    fflush(NULL);

    pid_t pid = fork();
    if (pid == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        close(tuberia[0]);
        close(tuberia[1]);
        free(ordenes);
        return 1;
    }

    if (pid == 0) {
        /* ---------------- PROCESO HIJO ---------------- */
        close(tuberia[1]);                    /* el hijo no escribe en la tubería */

        /* Sustituimos la entrada estándar del hijo por el extremo de lectura.
         * A partir de aquí, todo lo que el editor lea de STDIN vendrá del shell. */
        if (dup2(tuberia[0], STDIN_FILENO) == -1) {
            fprintf(stderr, COLOR_ERROR "dup2 falló: %s\n" COLOR_RESET, strerror(errno));
            _exit(126);
        }
        close(tuberia[0]);                    /* ya está duplicado en el fd 0 */

        char *args[3];
        args[0] = (char *)ruta_editor;
        args[1] = (char *)archivo;
        args[2] = NULL;

        execv(ruta_editor, args);
        fprintf(stderr, COLOR_ERROR "\nNo se pudo ejecutar '%s': %s\n" COLOR_RESET,
                ruta_editor, strerror(errno));
        _exit(127);
    }

    /* ---------------- PROCESO PADRE ---------------- */
    LOG_SYSCALL_RESULT(pid);
    fflush(stdout);
    close(tuberia[0]);                        /* el padre no lee de la tubería */

    LOG_SYSCALL("write", "%d, ordenes, %zu", tuberia[1], total_bytes);
    ssize_t escritos = 0;
    while ((size_t)escritos < total_bytes) {
        ssize_t n = write(tuberia[1], ordenes + escritos, total_bytes - (size_t)escritos);
        if (n == -1) {
            if (errno == EINTR) continue;     /* interrumpido por una señal */
            break;
        }
        if (n == 0) break;
        escritos += n;
    }
    if (escritos < 0 || (size_t)escritos != total_bytes)
        LOG_SYSCALL_ERROR(strerror(errno));
    else
        LOG_SYSCALL_RESULT(escritos);

    /* Cerrar la escritura le da el fin de entrada al editor: así termina solo. */
    LOG_SYSCALL("close", "%d", tuberia[1]);
    if (close(tuberia[1]) == -1) LOG_SYSCALL_ERROR(strerror(errno));
    else LOG_SYSCALL_RESULT(0);

    free(ordenes);

    int status = 0;
    LOG_SYSCALL("waitpid", "%d, &status, 0", pid);
    pid_t esperado = waitpid(pid, &status, 0);
    if (esperado == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(esperado);

    informar_estado(status, "El editor");
    return 0;
}
