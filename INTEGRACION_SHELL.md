# Integración del Editor con el Shell de Clase

**Decisión: se creó una categoría nueva, `aplicaciones`.**
Este documento explica la arquitectura del shell, por qué ninguna categoría existente servía, y qué se implementó.

Shell base: [SO2026B/shell](https://github.com/evalenciEAFIT/SO2026B/tree/main/shell), commit `fb0daa0`. La copia con nuestros cambios está en [`shell/`](shell/).

---

## 1. Cómo está construido el shell de clase

El shell es **dirigido por tabla**. Cada comando es una entrada de un arreglo global en `main.c`:

```c
typedef struct {
    const char *name;        /* lo que el usuario escribe */
    const char *category;    /* "datos", "memoria", "monitoreo", "utilidades" */
    const char *usage;
    const char *description;
    const char *syscalls;    /* se muestra en la ayuda */
    int (*handler)(int argc, char **argv);
} Command;
```

El bucle principal lee una línea con `fgets`, la tokeniza (respetando comillas dobles) y recorre la tabla buscando una coincidencia por nombre. Si no la encuentra, sugiere usar `p_exec`.

La **categoría no es decorativa**: organiza `help`, que tiene tres modos (`help`, `help <categoría>`, `help <comando>`). Cada categoría vive además en su propio archivo `cat_*.c`. Es decir, la categoría es la unidad de organización del proyecto, y por eso el enunciado pide justificar en cuál entra el editor.

Las cuatro categorías originales y su contrato implícito:

| Categoría | Comandos | Qué tienen en común |
| :--- | :--- | :--- |
| `datos` | `d_create`, `d_read`, `d_info`, `d_copy` | Demuestran una syscall de archivos y **retornan de inmediato**. |
| `memoria` | `m_sbrk`, `m_mmap`, `m_info` | Manipulan el espacio de direcciones **del propio shell**. |
| `monitoreo` | `p_fork`, `p_exec`, `p_kill`, `p_monitor` | Observan y manipulan **procesos y señales**. |
| `utilidades` | `saludar`, `hora`, `fecha`, `despedir` | Consultas triviales al sistema. |

---

## 2. La pregunta: ¿dónde encaja un editor de texto?

### Descartado: `datos`

Es el encaje tentador, porque el editor usa `open`, `read`, `write`, `lseek`, `ftruncate` — las mismas syscalls que `d_read` o `d_copy`.

Pero clasificar por *qué syscall usa* confunde la parte con el todo. Todo comando de esa categoría comparte un contrato: **ejecuta, imprime su traza, y devuelve el prompt**. `d_read` abre un archivo, lo vuelca y termina. El editor no hace eso: se queda vivo indefinidamente, borra la pantalla, se apodera del teclado y sólo devuelve el control cuando el usuario decide salir.

Si `editor` apareciera en `help datos`, el estudiante que ha visto `d_info` esperaría un volcado de información y se encontraría con una aplicación a pantalla completa de la que no sabe cómo salir. La categoría dejaría de describir a sus miembros.

### Descartado: `monitoreo`

Aquí el argumento es más sutil, porque el **mecanismo** que usamos para lanzar el editor —`fork` + `exec` + `waitpid`— es literalmente el de `p_exec`.

Pero el mecanismo no es el propósito. `monitoreo` agrupa comandos *sobre procesos*: crear uno para verlo (`p_fork`), enviarle una señal (`p_kill`), medir sus recursos (`p_monitor`). El objeto de estudio es el proceso mismo. En nuestro caso el proceso es sólo el vehículo: lo que le interesa al usuario es editar un archivo, no observar un `fork`. Clasificar por mecanismo llevaría a meter en `monitoreo` cualquier programa externo que el shell llegue a lanzar, vaciando la categoría de significado.

### Descartado: no tocar el shell y usar `p_exec`

Merece analizarse porque **ya funciona hoy, sin escribir una línea**:

```
eafitOS> p_exec ../editor notas.txt
```

Es una solución legítima, y la dejamos documentada como alternativa de emergencia. No la elegimos por tres razones:

1. **No es integración, es coincidencia.** Nada en el shell menciona al editor. `help` no lo lista, `help editor` no existe. El usuario tiene que saber de antemano dónde está el binario.
2. **No protege la terminal.** `p_exec` no guarda ni repone el estado del TTY (ver §4). Si el editor muere sin restaurarlo, el shell queda inutilizable.
3. **No aprovecha el diseño del editor.** El editor sabe recibir órdenes por su entrada estándar; `p_exec` no tiene forma de alimentárselas.

### Elegido: categoría nueva, `aplicaciones`

> **`aplicaciones`** — Programas externos interactivos que el shell lanza como procesos hijo, **cediéndoles la terminal** hasta que terminan, y recuperándola después.

La razón decisiva no es temática sino arquitectónica: esta clase de comando le impone al shell un **protocolo que ninguna categoría existente necesita**: guardar el estado del terminal, ceder el control, esperar, y restaurar. Una categoría, en este shell, es un contrato sobre *cómo el shell trata al comando*. Ese contrato es nuevo, así que la categoría también.

Y generaliza, que es lo que pide el enunciado al hablar de "este tipo de aplicaciones": mañana un visor de logs, un `top` propio o un depurador entrarían aquí sin discusión, porque comparten el mismo protocolo.

---

## 3. Qué se implementó

Archivo nuevo **`shell/cat_aplicaciones.c`**, con dos comandos:

### `editor [-c] [archivo]` — uso interactivo

```
eafitOS> editor notas.txt
[syscall] access("../editor", X_OK) ... = 0
[syscall] tcgetattr(STDIN_FILENO, &estado) ... = 0
Cediendo la terminal a '../editor' sobre el archivo notas.txt...
[syscall] fork() ... = 4711
   ... el editor toma la pantalla; el usuario edita y sale con Ctrl+Q ...
[syscall] waitpid(4711, &status, 0) ... = 4711
[syscall] tcsetattr(STDIN_FILENO, TCSAFLUSH, &estado) ... = 0
[Padre] El editor terminó correctamente. Código: 0
```

Syscalls: `access`, `tcgetattr`, `fork`, `execv`, `waitpid`, `tcsetattr`.

El shell **no interpreta las banderas del editor: se las reenvía tal cual**, como hace cualquier shell con el programa que lanza. Eso da acceso a las tres formas de usar el editor sin que el shell tenga que conocerlas:

| Desde el shell | Qué abre |
| :--- | :--- |
| `editor notas.txt` | El editor visual, a pantalla completa. |
| `editor -c notas.txt` | El intérprete de línea del editor (`editor> `), el de los comandos del enunciado. |
| `editor --help` | La ayuda del propio editor. |

`cmd_editor` se limita a construir el vector `{ruta, argv[1], ..., NULL}` que espera `execv`.

### `editor_cmd <archivo> "<órdenes separadas por ;>"` — uso guionizado

```
eafitOS> editor_cmd notas.txt "a linea nueva; d 2; p"
[syscall] pipe(tuberia[2]) ... = 0
  tuberia[0]=3 (lectura, irá al editor)  tuberia[1]=4 (escritura, la usa el shell)
[syscall] fork() ... = 4712
Archivo 'notas.txt' abierto: fd=3, 3 linea(s), 21 byte(s).
Linea 4 anadida al final.
Linea 2 borrada. Quedan 3 linea(s).
[syscall] write(4, ordenes, 24) ... = 24
[syscall] close(4) ... = 0
[syscall] waitpid(4712, &status, 0) ... = 4712
[Padre] El editor terminó correctamente. Código: 0
```

Syscalls: `pipe`, `fork`, `dup2`, `execv`, `write`, `close`, `waitpid`.

**Por qué dos comandos y no uno.** El editor detecta con `isatty(STDIN_FILENO)` si su entrada es un teclado o una tubería, y cambia de interfaz solo. `editor` le entrega la terminal y arranca a pantalla completa; `editor_cmd` le enchufa una tubería en el descriptor 0 con `dup2` y el editor entra por su cuenta al intérprete de comandos. La misma decisión de diseño que tomamos en el editor —que su modo dependiera de `isatty`— es la que permite que el shell lo automatice sin ninguna bandera especial.

Un detalle bonito: **`editor_cmd` no necesita terminar sus órdenes con `q`**. Al cerrar el extremo de escritura de la tubería, el editor recibe fin de entrada, su bucle sale limpiamente, libera la memoria y cierra el descriptor. El fin de archivo *es* la orden de salir.

### Localización del binario

`resolver_editor()` busca en este orden, comprobando cada candidato con `access(ruta, X_OK)`:

1. `$EDITOR_SO` (variable de entorno, tiene prioridad)
2. `./editor`
3. `../editor` (el caso normal: `shell/` es subdirectorio del proyecto del editor)
4. `../editorTexto/editor`

Comprobarlo en el padre permite dar un mensaje claro; si se dejara para el hijo, el fallo aparecería después de `fork`, cuando ya no se puede informar cómodamente. Se usa `execv` y no `execvp` a propósito: la ruta ya está resuelta y verificada, y no queremos que el `PATH` del usuario decida qué binario se ejecuta.

---

## 4. El problema técnico central: la terminal es compartida

Es lo que hace a esta categoría distinta, y merece la pena explicarlo en la sustentación.

El editor pone el TTY en **modo crudo** con `tcsetattr`: apaga el eco, el modo canónico y las señales. Ese estado **no pertenece al proceso, pertenece al terminal**, que padre e hijo comparten. Si el editor termina normalmente lo restaura él mismo (tiene un `atexit`). Pero si muere por una señal —un fallo de segmentación, un `kill` desde otra terminal— nadie lo restaura, y el shell hereda una terminal sin eco, sin Ctrl+C y sin línea canónica: inservible.

Por eso `cmd_editor` hace:

```c
struct termios estado_terminal;
tcgetattr(STDIN_FILENO, &estado_terminal);   /* 1. guardar antes de ceder */
    /* fork + execv + waitpid */
tcsetattr(STDIN_FILENO, TCSAFLUSH, &estado_terminal);  /* 2. reponer pase lo que pase */
```

La restauración va **después del `waitpid` y fuera de cualquier condicional de éxito**: se ejecuta tanto si el editor salió con código 0 como si lo mató una señal. Cuando detectamos muerte por señal, el shell lo dice explícitamente, para que se entienda por qué la salvaguarda existe.

Este es también el argumento decisivo contra la otra alternativa que consideramos: **incorporar el editor como código dentro del shell** (un `builtin` que llamara directamente a nuestras funciones). Se descartó porque compartirían espacio de direcciones: un desbordamiento en el editor corrompería el shell, un `exit()` del editor mataría el shell entero, y ambos competirían por el mismo estado del TTY sin ninguna frontera. `fork` + `exec` da aislamiento de memoria, aislamiento de descriptores y un código de salida que el padre puede inspeccionar. Es el mismo motivo por el que un shell real no enlaza `vim` dentro de sí mismo.

---

## 5. Cambios exactos sobre el shell original

| Archivo | Cambio |
| :--- | :--- |
| `cat_aplicaciones.c` | **Nuevo.** La categoría y sus dos comandos. |
| `shell.h` | Prototipos de `cmd_editor` y `cmd_editor_cmd`; se añade `"aplicaciones"` al comentario de categorías válidas. |
| `main.c` | Dos entradas nuevas en la tabla `commands[]`; `print_help` anuncia la categoría y la acepta como argumento válido. |
| `Makefile` | `cat_aplicaciones.c` añadido a `SRCS`; nuevo target `compilar` (construye sin lanzar el shell, necesario para el guion de pruebas); corregido `clean`, que borraba `$(TARGET)`, una variable que no existe — el binario nunca se eliminaba. |

Ningún comando ni categoría original fue modificado: la integración es **aditiva**.

> Nota sobre `print_help`: la lista de categorías está escrita a mano en dos sitios (el listado de `help` y el `strcmp` que valida `help <categoría>`), así que añadir una categoría obliga a tocar los dos. Lo hicimos así para no alterar la estructura del shell de clase; una mejora natural sería derivar la lista recorriendo la tabla `commands[]`.

---

## 6. Cómo ejecutarlo

```bash
# 1. Compilar el editor (desde la raíz del proyecto)
make

# 2. Compilar y lanzar el shell
cd shell
make            # compila y arranca eafitOS

# 3. Dentro del shell
eafitOS> help aplicaciones
eafitOS> editor notas.txt                     # editor visual
eafitOS> editor -c notas.txt                  # interprete de linea del editor
eafitOS> editor_cmd notas.txt "a hola; p; m"  # ordenes por tuberia, sin interaccion
```

Si el editor está en otra ruta:

```bash
EDITOR_SO=/ruta/al/editor ./eafitOS
```

---

## 7. Verificación

El banco de pruebas del proyecto (`bash pruebas.sh`) incluye una sección de integración que compila el shell y comprueba, entre otras cosas, que:

* la categoría `aplicaciones` aparece en `help` y se acepta en `help aplicaciones`;
* `help editor` detalla sus syscalls;
* el shell localiza el editor con `access()` sin ayuda del usuario;
* `editor_cmd` modifica **los bytes reales del archivo** en disco;
* la traza muestra `pipe` y los descriptores de la tubería;
* el editor termina solo al cerrar la tubería, con código 0;
* el shell reenvía las banderas al editor (`editor --help`, `editor -z`);
* `editor -c` abre el intérprete de línea del editor desde el shell, sobre un pseudo-terminal;
* `EDITOR_SO` tiene prioridad sobre las rutas por defecto;
* sin binario a la vista, el error es claro y el shell no revienta.

El lanzamiento interactivo (`editor archivo`) se verificó a mano y con un pseudo-terminal: se lanza el editor desde el shell, se borra una línea con `Ctrl+D`, se sale con `Ctrl+Q` y se comprueba que el shell recupera la terminal y sigue aceptando comandos con normalidad.
