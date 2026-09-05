# Mapa de Llamadas al Sistema

Este documento es la referencia rápida de **qué llamada al sistema resuelve cada comando y en qué línea de código vive**. Sirve de insumo directo para el documento de sustentación y para el video.

Restricción del parcial: *"Queda prohibido el uso de las funciones de alto nivel de la biblioteca estándar de C (como fopen, fread, fwrite o fclose) para la manipulación del archivo de texto. Solo se permite I/O estándar (printf, scanf, fgets) para la lectura de los comandos en STDIN y la impresión de la consola en STDOUT."*

**Estado de cumplimiento:** el binario no contiene ni una sola llamada a `fopen`, `fread`, `fwrite` o `fclose`. Ni siquiera para leer los archivos virtuales del kernel en `/proc` (ver más abajo). `printf`, `fgets` y `getchar` se usan **exclusivamente** sobre STDIN y STDOUT, que es lo que el enunciado permite.

---

## 1. Comandos base (Sección 3 del enunciado)

| Comando | Atajo visual | Llamadas al sistema | Dónde |
| :--- | :--- | :--- | :--- |
| `o [archivo]` | `Ctrl+O` | `open(ruta, O_RDWR \| O_CREAT, 0644)` | `archivo_abrir()` — `src/archivo.c` |
| `p` | — | `lseek(SEEK_SET)` + `read()` por bloques, recorriendo bytes hasta cada `\n` | `archivo_imprimir_todo()` — `src/archivo.c` |
| `p [n]` | `Ctrl+V` | `lseek()` al offset indexado + `read()` del largo exacto | `archivo_leer_linea()` / `archivo_imprimir_linea()` |
| `a [texto]` | `Ctrl+A` | `lseek(0, SEEK_END)` + `write()` | `archivo_anexar()` — `src/archivo.c` |
| `d [n]` | `Ctrl+D` | `read()` de la cola → `lseek()` → `write()` → **`ftruncate()`** | `archivo_borrar_linea()` — `src/archivo.c` |
| `q` | `Ctrl+Q` | `close()` + liberación de toda la memoria dinámica | `archivo_cerrar()` / `editor_liberar()` |

## 2. Requisitos acumulativos de equipos de 2

| Comando | Atajo visual | Llamadas al sistema | Dónde |
| :--- | :--- | :--- | :--- |
| `i [n] [texto]` | `Ctrl+N` | `read()` de la cola a un buffer `malloc` → `lseek()` → `write()` del texto + la cola | `archivo_insertar_linea()` — `src/archivo.c` |
| `s [palabra]` | `Ctrl+F` | `lseek()` + `read()` línea a línea, comparando con `strstr` | `archivo_buscar()` — `src/archivo.c` |

## 3. Requisitos acumulativos de equipos de 3

| Comando | Atajo visual | Llamadas al sistema | Dónde |
| :--- | :--- | :--- | :--- |
| `m` | `Ctrl+G` | **`fstat(fd, &st)`** → `st_size`, `st_mode`, `st_ino`, `st_mtime`, `st_nlink`, `st_uid` | `archivo_metadatos()` — `src/archivo.c` |
| `y [n]` | `Ctrl+Y` | `lseek()` + `read()` de la línea, y `malloc` para encolarla | `archivo_leer_linea()` + `portapapeles_agregar()` |
| `x [n]` | `Ctrl+U` | Un `archivo_insertar_linea()` por cada línea del portapapeles | `comando_pegar()` — `src/cli.c` |

## 4. Llamadas de soporte (fuera de la tabla del enunciado)

| Función | Llamada | Para qué |
| :--- | :--- | :--- |
| `main()` | `isatty(STDIN_FILENO)` | Decidir si hay un teclado (modo visual) o una tubería (modo CLI), salvo que `-c`/`--cli` lo fuerce. |
| `terminal_activar_modo_raw()` | `tcgetattr` / `tcsetattr` | Apagar el modo cocinado del TTY para leer byte a byte. |
| `terminal_obtener_tamano()` | `ioctl(TIOCGWINSZ)` | Consultar el tamaño de la ventana de la terminal. |
| `editor_pantalla_limpiar()` | `write(STDOUT_FILENO, ...)` | Enviar secuencias de escape ANSI a la pantalla. |
| `editor_pantalla_bienvenida()` | `sleep()` | Suspender el proceso durante la presentación. |
| `leer_archivo_completo()` | `open` + `read` + `close` | Leer `/proc/self/status` y `/proc/self/maps` **sin usar stdio**. |

---

## 5. Integración con el shell (categoría `aplicaciones`)

| Comando del shell | Llamadas al sistema | Dónde |
| :--- | :--- | :--- |
| `editor [archivo]` | `access`, `tcgetattr`, `fork`, `execv`, `waitpid`, `tcsetattr` | `cmd_editor()` — `shell/cat_aplicaciones.c` |
| `editor_cmd <archivo> "<ordenes>"` | `pipe`, `fork`, `dup2`, `execv`, `write`, `close`, `waitpid` | `cmd_editor_cmd()` — `shell/cat_aplicaciones.c` |

`tcgetattr`/`tcsetattr` no son decorativos: el editor deja el TTY en modo crudo y podría morir sin restaurarlo, así que el shell guarda ese estado antes de ceder la terminal y lo repone al recuperarla. Ver [INTEGRACION_SHELL.md](INTEGRACION_SHELL.md).

---

## 6. El detalle que importa: cómo borra `d [n]`

El enunciado pide, literalmente, *"desplazando los bytes posteriores y truncando"*. Así se implementa en `archivo_borrar_linea()`:

```text
Archivo antes:   u n o \n d o s \n t r e s \n
                 ^       ^
                 |       |
inicio[1] = 4 ---+       +--- fin de la linea 2 = inicio[2] = 8

1. leer_cola(fd, desde=8)   -> malloc con "tres\n"       (read)
2. lseek(fd, 4, SEEK_SET)                                 (lseek)
3. write(fd, "tres\n", 5)                                 (write)

Archivo ahora:   u n o \n t r e s \n o s \n     <- sobran 4 bytes basura
                                     ^^^^^^

4. ftruncate(fd, 13 - 4 = 9)                              (ftruncate)

Archivo final:   u n o \n t r e s \n
```

Sin el paso 4 el archivo conservaría al final los bytes de la línea vieja. Ése es el motivo exacto por el que `ftruncate` aparece en la tabla del enunciado.

---

## 7. Verificación de errores

La rúbrica pide *"Verificación de retornos (ej. si open devuelve -1), usando perror() apropiadamente"*. En este proyecto:


* **Toda** llamada al sistema comprueba su retorno y reporta con `perror()` indicando entre paréntesis la operación en curso (`perror("lseek (borrar)")`), de modo que el mensaje del kernel llega acompañado del contexto.
* `write()` y `read()` pasan por los envoltorios `escribir_todo()` y `leer_todo()` (`src/archivo.c`), que reintentan ante escrituras/lecturas parciales y ante interrupciones por señal (`EINTR`). Suponer que un `write()` de N bytes escribe siempre N bytes es un error clásico que este proyecto evita por construcción.
* Todo `malloc` y `realloc` comprueba si devolvió `NULL`.
* Una apertura fallida hace que el editor termine con código de salida distinto de cero, verificable desde un shell.
