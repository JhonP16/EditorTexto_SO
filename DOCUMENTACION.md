# Documentación Arquitectónica: Editor de Texto

Este documento explica el diseño interno, la arquitectura y el uso general de los componentes de nuestro editor de texto para terminales POSIX.

---

## 1. Diseño General y Arquitectura

El editor aplica el principio de "Responsabilidad Única", repartiendo el código en ocho módulos:

**Capa de sistema (los que hablan con el kernel)**

* **`archivo.c`**: la capa de disco. Mantiene el descriptor abierto y ejecuta todas las llamadas al sistema que manipulan bytes del archivo: `open`, `read`, `write`, `lseek`, `ftruncate`, `fstat`, `close`. Es el corazón del proyecto.
* **`terminal.c`**: capa de abstracción entre el editor y el driver TTY del kernel. Configura el modo de la terminal.

**Capa de datos**

* **`estructura.c`**: gestiona la lista doblemente enlazada de líneas (y su lista de palabras) que representa el documento en RAM.
* **`portapapeles.c`**: lista enlazada simple con las líneas copiadas por el comando `y`.
* **`cadena.c`**: buffer de texto dinámico (`malloc`/`realloc`) que sustituye a los arreglos fijos de la versión anterior.

**Capa de aplicación e interfaz**

* **`cli.c`**: el intérprete de comandos. Analiza la línea escrita, despacha el comando y devuelve el resultado en un buffer.
* **`editor.c`**: el "front-end" visual. Coordenadas del cursor, inserción de texto, dibujo de filas y barra de estado.
* **`comando.c`**: intercepta los atajos `Ctrl+X` y dibuja las pantallas modales de diagnóstico.
* **`main.c`**: punto de entrada; decide qué interfaz arrancar.

---

## 2. La decisión de diseño central: un despachador, dos interfaces

El enunciado pide un editor operado por comandos "inspirado en `ed` o `vi`", y nosotros ya teníamos construido un editor visual a pantalla completa. En vez de escribir dos programas, escribimos **una sola implementación de cada comando** y dos formas de invocarla:

```text
                  archivo.c
        (open/read/write/lseek/ftruncate/fstat)
                      |
          cli_ejecutar_comando("d 2")     <-- unico despachador
             /                    \
   MODO VISUAL                     MODO CLI
   pantalla completa;              interprete de linea ('editor> ')
   cada comando tiene su           donde se escriben los comandos
   atajo Ctrl y pide los           tal cual: "d 2", "i 1 hola", "q"
   argumentos por pantalla
```

Ningún atajo del modo visual reimplementa nada: construye la misma cadena de texto que se escribiría en el modo CLI y se la pasa al despachador.

```c
// comando.c: Ctrl+D con el cursor en la linea 3
cadena_printf(&orden, "%s %s", "d", "3");   // -> "d 3"
comando_procesar_orden(e, orden.datos);     // -> cli_ejecutar_comando(...)
```

### Cómo se elige el modo

`main.c` decide en dos pasos:

1. La bandera `-c` / `--cli` fuerza el modo CLI.
2. Si no se pasó, **`isatty(STDIN_FILENO)`** le pregunta al sistema operativo si el descriptor 0 está conectado a un TTY. Con teclado arranca el visual; con una tubería, el CLI.

Ese segundo paso es el que hace al editor automatizable: el banco de pruebas alimenta comandos con `printf ... | ./editor archivo` y verifica los bytes que quedan en disco, y un shell podrá invocarlo igual sin trabajo adicional.

Las ventajas de resolverlo así:

1. **Cero duplicación**: la lógica de `d 2` existe una sola vez, en `cli.c` y `archivo.c`. Añadir un comando nuevo lo hace aparecer en los dos modos.
2. **Cada interfaz hace lo que sabe hacer**: el modo CLI da los comandos exactos del enunciado; el visual da la comodidad de operar sobre la línea del cursor sin teclear su número.
3. **Automatizable de punta a punta**: incluso los atajos del modo visual se prueban, enviando pulsaciones reales a un pseudo-terminal.

### Dos notas sobre las teclas

* El prompt libre se abre con `Ctrl+L`, no con `:`. Este editor no tiene modos —todo lo que se escribe se inserta en el texto—, así que dedicar la tecla `:` a los comandos impediría escribir dos puntos en el documento. El prompt que aparece sigue siendo `:`.
* El comando `p` va en `Ctrl+V` porque `Ctrl+P` ya era el monitor de memoria. `Ctrl+M`, `Ctrl+I` y `Ctrl+J` no se pueden usar: la terminal los entrega como ENTER y TAB.

---

## 3. La segunda decisión: el disco es la fuente de verdad

Conviven dos representaciones del documento y cada una tiene un papel definido:

| Representación | Dónde vive | Para qué sirve |
| :--- | :--- | :--- |
| Lista doblemente enlazada (`estructura.c`) | RAM | Dibujar la pantalla y aceptar la escritura con el teclado. Es la **vista**. |
| Descriptor + índice de líneas (`archivo.c`) | Disco | Ejecutar los comandos `o p a d i s m y x`. Es la **fuente de verdad**. |

La regla es simple: **los comandos escriben en el disco y después la vista se recarga desde el disco.**

```text
Ctrl+L -> d 2
   1. Si habia cambios sin guardar, se persisten primero (editor_guardar_en_disco)
   2. archivo_borrar_linea() aplica el cambio con lseek/write/ftruncate
   3. archivo_indexar() reconstruye el indice de offsets
   4. editor_recargar_desde_disco() vuelve a leer la lista enlazada
```

Por qué esta dirección y no la contraria:

* La tabla del enunciado asocia `d [n]` a `lseek()` y `ftruncate()`. Si el documento sólo viviera en RAM y se guardara entero al final, esas dos llamadas nunca se ejercitarían de verdad; el borrado sería un simple juego de punteros.
* Con una única fuente de verdad **no hay estados divergentes** que sincronizar. La alternativa (mantener RAM y disco actualizados en paralelo) duplica la lógica y multiplica los casos borde.
* El coste es releer el archivo tras cada comando, despreciable en archivos de texto plano y a cambio de una garantía fuerte: lo que ves es lo que hay en el disco.

---

## 4. El Subsistema de la Terminal (`terminal.c`)

En Linux, la terminal normalmente opera en **Modo Cooked (Cocinado)**: el kernel retiene lo que escribes y sólo se lo entrega al programa cuando presionas `Enter`.

Para un editor en tiempo real hay que acceder a la API de `termios.h`:

* **Modo Raw (Crudo)**: se usa `tcsetattr` para desactivar el procesamiento del kernel (se apagan `ECHO`, `ICANON`, y las señales como `SIGINT`). El programa recibe *byte a byte* al instante.
* **Secuencias ANSI**: enviando códigos de escape como `\x1b[2J` o `\x1b[39m` al descriptor `STDOUT_FILENO` se controlan colores, borrado de pantalla y posición del cursor.

Ambas llamadas verifican su retorno con `perror()`: si el programa se ejecuta con la entrada redirigida, `tcgetattr` falla y hay que saberlo en vez de seguir a ciegas.

---

## 5. Modelo de Datos

### 5.1 En RAM: lista doblemente enlazada (`estructura.c`)

No guardamos el texto entero en un solo buffer plano. Cada línea es un `NodoLinea` conectado al anterior y al siguiente, y cada línea contiene a su vez una lista de `NodoPalabra`.

Si el usuario pulsa ENTER a mitad de un archivo de 10.000 líneas, no hay que desplazar 5.000 líneas en memoria: se cambian dos punteros. `editor.c` recorre esta lista desde un desplazamiento (scroll) dado y pinta línea por línea.

### 5.2 En disco: índice de offsets (`archivo.c`)

Para no releer el archivo entero en cada comando, `archivo.c` mantiene dos arreglos dinámicos con el inicio y el largo de cada línea, construidos recorriendo los bytes hasta cada `\n`:

```text
archivo.txt:  "hola\nmundo\n"
               ^     ^
inicio[] =    { 0,    5 }
largo[]  =    { 4,    5 }      (sin contar el salto de linea)
```

Con eso, `p 7` es un `lseek` directo al byte donde empieza la línea 7, en vez de un recorrido desde el principio.

### 5.3 Buffers dinámicos (`cadena.c`)

La versión anterior armaba texto sobre arreglos fijos `char buffer[4096]` de la pila. Cualquier línea de más de 4095 bytes los desbordaba y corrompía la pila. Ahora todo texto de tamaño no acotado se construye sobre una `Cadena` que duplica su capacidad con `realloc` según hace falta, y las líneas se materializan con un `malloc` del tamaño exacto. El banco de pruebas incluye un caso con una línea de 8.000 bytes.

---

## 6. Uso del Editor y Comandos (`comando.c`)

Las teclas combinadas con Control se interceptan con una máscara de bits: `#define TECLA_CTRL(k) ((k) & 0x1f)`.

* **Atajos de comando** (`Ctrl+O` abrir, `Ctrl+V` ver línea, `Ctrl+A` añadir, `Ctrl+D` borrar, `Ctrl+N` insertar, `Ctrl+F` buscar, `Ctrl+G` metadatos, `Ctrl+Y` copiar, `Ctrl+U` pegar): piden por pantalla los argumentos que necesiten, con la línea del cursor como valor por defecto, y delegan en el despachador.
* **`Ctrl + L` (Línea de comandos)**: abre el prompt `:` para escribir cualquier comando completo.
* **`Ctrl + S` (Guardar)**: recorre la lista enlazada, arma el texto y lo escribe con `write()`. Muestra un log interactivo del proceso y reindexa el descriptor.
* **`Ctrl + Q` (Salir)**: marca la bandera de salida. El bucle de `main()` termina y **entonces** se libera la memoria y se cierra el descriptor. (Antes esta función llamaba a `exit(0)` directamente y dejaba fugas: el requisito del comando `q` pide salir "sin dejar fugas de memoria".)
* **`Ctrl + F` (Buscar)**: pide una palabra con `fgets()`, la busca en la estructura y recoloca el cursor en la coincidencia.

### Pantallas de Diagnóstico Modales

* **`Ctrl + H` (Ayuda)**: atajos y tabla de comandos.
* **`Ctrl + E` (Estructura)**: volcado de la memoria de las estructuras enlazadas, con los punteros físicos.
* **`Ctrl + P` (Monitor de Memoria)**: lee `/proc/self/status` y `/proc/self/maps` para mostrar en vivo cuánta RAM física y cuánta memoria virtual (Heap/Stack) consume el editor. **Esta lectura también se hace con `open`/`read`/`close`**: el proyecto no usa `fopen` en ninguna parte, ni siquiera para archivos que no son el documento editado.

---

## 7. Manejo de errores

* Toda llamada al sistema verifica su retorno y reporta con `perror()` con el contexto entre paréntesis: `perror("lseek (borrar)")`.
* `write()` y `read()` pasan por `escribir_todo()` y `leer_todo()`, que reintentan ante operaciones parciales y ante `EINTR`.
* Todo `malloc`/`realloc` comprueba `NULL`.
* Los comandos validan sus argumentos antes de tocar el disco: número de línea fuera de rango, argumento no numérico, archivo sin abrir, portapapeles vacío.
* Una apertura fallida termina con código de salida distinto de cero.

---

## 8. Integración con el shell de clase

El editor se integra al shell de la asignatura mediante una **categoría nueva**, `aplicaciones`, que agrupa los programas externos interactivos a los que el shell cede la terminal. Aporta dos comandos:

* `editor [-c] [archivo]` — lanza el editor cediéndole la terminal (`access`, `tcgetattr`, `fork`, `execv`, `waitpid`, `tcsetattr`). Las banderas se reenvían tal cual, así que `editor -c` abre el intérprete de línea del editor desde el shell.
* `editor_cmd <archivo> "<órdenes;...>"` — le inyecta órdenes por una tubería (`pipe`, `fork`, `dup2`, `execv`, `write`, `close`, `waitpid`).

El segundo funciona precisamente por la decisión de la sección 2: como el editor elige su interfaz con `isatty()`, al recibir una tubería en vez de un teclado entra solo en modo intérprete.

El análisis completo —por qué no encajaba en `datos` ni en `monitoreo`, por qué no basta `p_exec`, y por qué el shell debe guardar y reponer el estado del TTY— está en **[INTEGRACION_SHELL.md](INTEGRACION_SHELL.md)**. El código del shell modificado está en [`shell/`](shell/).

---

## 9. Pruebas

`pruebas.sh` (invocable con `make test`) ejercita los 68 escenarios del proyecto: cada comando, cada requisito acumulativo, la selección de interfaz (`-c`, `--cli`, `isatty`) la integración con el shell de clase y los casos borde (archivo vacío, archivo sin salto final, línea de 8 KB, índices inválidos, comandos sin archivo abierto, apertura imposible). Verifica tanto lo que el editor imprime como los bytes que quedan realmente en el disco.

Los atajos del modo visual también se prueban: como sólo arrancan si `isatty()` ve una terminal, el script usa `script(1)` para darles un pseudo-terminal y les envía pulsaciones reales (`` para `Ctrl+D`, etc.). Hacen falta pausas entre teclas porque cada vuelta al modo raw usa `TCSAFLUSH`, que descarta la entrada pendiente. Se omiten con `SIN_VISUAL=1`.
