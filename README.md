# Manual de Usuario - Editor de Texto

Bienvenido al **Editor de Texto**: un editor ágil y minimalista que opera directamente en modo texto (CLI), construido para funcionar desde la consola de Linux sin requerir interfaces gráficas pesadas.

---

## 1. Compilación e Inicio

Antes de utilizar el editor por primera vez, compila el código fuente con `make`:

```bash
make clean
make
```

---

## 2. Los dos modos del editor

El editor ofrece las mismas funciones de dos maneras distintas. **Los dos modos ejecutan exactamente los mismos comandos**; sólo cambia cómo se los pides.

```bash
./editor demo.txt        # MODO VISUAL: pantalla completa, con atajos de teclado
./editor -c demo.txt     # MODO CLI: intérprete de línea clásico, al estilo de 'ed'
```

Si no pasas ningún archivo, el editor trabaja sobre `archivo.txt`, creándolo si no existe.

### Modo CLI (`-c` o `--cli`)

Un prompt donde escribes los comandos tal cual, uno por línea:

```
$ ./editor -c demo.txt
Archivo 'demo.txt' abierto: fd=3, 3 linea(s), 13 byte(s).
editor> p
   1 | uno
   2 | dos
   3 | tres
editor> d 2
Linea 2 borrada. Quedan 2 linea(s).
editor> i 1 encabezado
Texto insertado como linea 1. Ahora hay 3 linea(s).
editor> q
```

### Modo visual (sin banderas)

Pantalla completa con el texto a la vista. Cada comando tiene su atajo de teclado y, si necesita datos, **te los pide por pantalla**. Por ejemplo, `Ctrl+D`:

```
==== BORRAR LÍNEA (comando d) ====

Número de línea [3]:
```

El número entre corchetes es la línea donde está tu cursor: pulsar ENTER en vacío borra *esa* línea. Si quieres otra, escribes su número.

### Modo automático

Si la entrada estándar no es un teclado sino una tubería, el editor entra solo al modo CLI. Eso permite guionizarlo y, más adelante, invocarlo desde un shell:

```bash
printf 'a hola\np\nq\n' | ./editor demo.txt
```

---

## 3. Tabla de comandos

| Comando | Atajo visual | Qué hace |
| :--- | :--- | :--- |
| `o [archivo]` | `Ctrl + O` | Abre un archivo. Si no existe, lo crea. |
| `p` | — | Imprime todo el archivo numerado. |
| `p [n]` | `Ctrl + V` | Imprime la línea *n*. |
| `a [texto]` | `Ctrl + A` | Añade el texto como nueva última línea. |
| `d [n]` | `Ctrl + D` | Borra la línea *n* y recorta el archivo. |
| `i [n] [texto]` | `Ctrl + N` | Inserta el texto como línea *n*, desplazando el resto. |
| `s [palabra]` | `Ctrl + F` | Busca la palabra y lista todas las coincidencias con línea y columna. En modo visual, además lleva el cursor a la primera. |
| `m` | `Ctrl + G` | Metadatos: tamaño, permisos, inodo, fecha de modificación. |
| `y [n]` | `Ctrl + Y` | Copia la línea *n* al portapapeles (se van acumulando). |
| `y` | — | Muestra el contenido del portapapeles. |
| `yc` | — | Vacía el portapapeles. |
| `x [n]` | `Ctrl + U` | Pega **todo** el portapapeles a partir de la línea *n*. |
| `h` | `Ctrl + H` | Ayuda. |
| `q` | `Ctrl + Q` | Cierra el archivo, libera la memoria y sale. |

Los comandos sin atajo propio (`p` completo, `y` sin argumento, `yc`) se escriben en la **línea de comandos libre**, que abre `Ctrl + L`: aparece un prompt `:` donde puedes teclear cualquier comando completo.

En los atajos que piden un número de línea, pulsar ENTER en vacío usa la línea del cursor.

### Cómo funciona el portapapeles

Es **secuencial**: cada `y` encola una línea más, y `x` las pega todas juntas en el orden en que las copiaste.

```
y 3      -> el portapapeles tiene 1 línea (la vieja línea 3)
y 1      -> el portapapeles tiene 2 líneas (la 3 y luego la 1)
x 5      -> inserta ambas a partir de la línea 5, en ese mismo orden
yc       -> lo vacía para empezar de nuevo
```

---

## 4. El entorno visual

### Barra de Estado

En la fila inferior verás el nombre del archivo (con un `*` si tienes cambios escritos a mano sin guardar), la posición del cursor en **Fila** y **Columna**, y un recordatorio de los atajos principales.

### Navegación y edición

Para editar, simplemente empieza a escribir. Puedes moverte con:

* **Teclas de dirección**: flechas Arriba, Abajo, Izquierda, Derecha.
* **W, A, S, D**: atajo alternativo para no mover la mano del centro del teclado.
* **Backspace / Enter**: borran hacia atrás o dividen líneas, como es habitual.

### Atajos del entorno (además de los de la tabla)

| Atajo | Acción |
| :--- | :--- |
| `Ctrl + L` | Línea de comandos libre: escribe cualquier comando completo. |
| `Ctrl + S` | Guarda el documento y muestra el registro de las llamadas al sistema. |
| `Ctrl + H` | Muestra u oculta la ayuda. |
| `Ctrl + E` | Visor de la estructura de datos en memoria. |
| `Ctrl + P` | Monitor de memoria del proceso. |

> `Ctrl + M`, `Ctrl + I` y `Ctrl + J` no se usan como atajos: la terminal los envía como ENTER y TAB, así que no se pueden distinguir de esas teclas.

### Sobre los cambios sin guardar

Los comandos trabajan sobre los **bytes reales del archivo en disco**. Por eso, si tienes cambios escritos a mano que aún no has guardado, el editor los persiste automáticamente antes de ejecutar el comando y recarga la vista después, dejando el cursor lo más cerca posible de donde estaba. Así nunca ves una pantalla que no corresponda con el archivo.

---

## 5. Herramientas de Diagnóstico (Modales)

Pantallas que se superponen al texto. Para salir de ellas, **vuelve a presionar el mismo atajo que las abrió**.

* **`Ctrl + H` (Ayuda)**: la lista maestra de atajos y comandos.
* **`Ctrl + E` (Visor de Estructura)**: muestra cómo están organizadas físicamente las líneas y palabras en la RAM, con las direcciones de los nodos de las listas enlazadas.
* **`Ctrl + P` (Monitor de Memoria)**: lee `/proc/self/status` y `/proc/self/maps` con `open()`/`read()` y te muestra cuánta memoria consume el editor en los segmentos *Heap* y *Stack*.

---

## 6. Lanzarlo desde el shell de la asignatura

El editor se integra al shell de clase en la categoría `aplicaciones`:

```bash
make                # 1. compilar el editor (desde la raiz del proyecto)
cd shell && make    # 2. compilar el shell; su Makefile lo arranca enseguida
```

Ya dentro del shell:

```
eafitOS> editor notas.txt                     # editor visual
eafitOS> editor -c notas.txt                  # interprete de linea del editor
eafitOS> editor_cmd notas.txt "a hola; p; m"  # ordenes por tuberia, sin interaccion
```

El detalle de la integracion esta en [INTEGRACION_SHELL.md](INTEGRACION_SHELL.md).

---

## 7. Pruebas

El proyecto trae un banco de pruebas automático con 68 escenarios: cada comando, cada caso borde y los atajos del modo visual (que se ejercitan enviando pulsaciones reales a un pseudo-terminal).

```bash
make test                    # o bien:  bash pruebas.sh
SIN_VISUAL=1 bash pruebas.sh # omite las pruebas del modo visual, que son lentas
```

Termina con código 0 si todo pasa e informa qué falló en caso contrario.
