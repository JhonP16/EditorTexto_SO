# Arquitectura de las Estructuras de Datos

El editor mantiene **tres estructuras dinámicas** con papeles distintos, todas construidas con `malloc`/`free`:

| Estructura | Archivo | Papel |
| :--- | :--- | :--- |
| Lista doblemente enlazada de líneas | `src/estructura.c` | El documento en RAM: lo que se dibuja y se escribe con el teclado. |
| Índice de offsets de línea | `src/archivo.c` | El mapa del archivo en disco: dónde empieza y cuánto mide cada línea. |
| Portapapeles secuencial | `src/portapapeles.c` | Las líneas copiadas con `y`, en orden. |

---

# Parte 1: La lista enlazada del documento (`estructura.c`)

## 1. ¿Por qué usar Listas Enlazadas?

Si el editor almacenara un archivo entero (por ejemplo de 50.000 líneas) en un solo vector continuo en memoria RAM, cada vez que el usuario escribiera una sola letra al principio del archivo, el procesador tendría que desplazar en cascada todos los datos restantes para hacerle espacio. Esto causaría retrasos inaceptables ("lag").

Al utilizar una **Lista Doblemente Enlazada de Líneas**, si se inserta un "ENTER" o se modifica un párrafo, el sistema sólo debe reasignar memoria localmente y reconectar unos punteros. El resto del archivo no se mueve de la memoria y la operación ocurre de manera instantánea.

## 2. Jerarquía de Estructuras (Structs)

Toda la información reside en tres niveles lógicos (declarados en `src/estructura.h`):

### A. `EstructuraTexto` (El Documento)
Es el contenedor raíz. Representa la totalidad del archivo en RAM.
*   **`cabeza`**: puntero a la primera línea del documento.
*   **`cola`**: puntero a la última línea (permite anexar en tiempo constante).
*   **`totalLineas`**: contador del total de líneas.

### B. `NodoLinea` (La Fila de Texto)
Es el andamio vertical del documento: una **lista doblemente enlazada**, que permite recorrer el texto hacia abajo (`siguiente`) o hacia arriba (`anterior`).
*   **`longitud`**: recuento total de caracteres de la fila.
*   **`siguiente` y `anterior`**: punteros a los demás nodos de línea.
*   **`palabras_cabeza` / `palabras_cola`**: la lista de bloques de texto de la fila.

### C. `NodoPalabra` (La Palabra o Bloque de Texto)
Cada línea subdivide su texto horizontalmente, para dar pie a funcionalidades como resaltado de sintaxis, conteo léxico o búsqueda de palabras enteras.
*   **`texto`**: la cadena de caracteres real.
*   **`longitud`** y **`capacidad`**: cuánto mide y cuánto tiene reservado.
*   **`siguiente` / `anterior`**: punteros a las palabras vecinas de la misma fila.

## 3. Representación Visual en la Memoria del Sistema

Aspecto esquemático de los punteros al guardar un documento de dos renglones:

```text
[EstructuraTexto]
       |
       v
 [NodoLinea 1] (ant: NULL, sig: NodoLinea 2)
       |
       L---> palabras_cabeza ---> [NodoPalabra "Hola"] ---> [NodoPalabra "Mundo"] ---> NULL

 [NodoLinea 2] (ant: NodoLinea 1, sig: NULL)
       |
       L---> palabras_cabeza ---> [NodoPalabra "Programando"] ---> [NodoPalabra "en"] ---> [NodoPalabra "C"] ---> NULL
```

Este volcado se puede ver en vivo, con las direcciones reales de los nodos, pulsando **`Ctrl + E`** dentro del editor.

## 4. Buffers dinámicos en lugar de arreglos fijos

La primera versión de este módulo armaba el texto de una línea sobre un arreglo fijo de la pila:

```c
char buffer[4096];              // <- desbordamiento con lineas mas largas
obtener_texto_linea(linea, buffer);
```

Cualquier línea de más de 4095 bytes corrompía la pila. Hoy el texto se materializa con un `malloc` del **tamaño exacto de la línea** más la holgura que haga falta:

```c
static char* linea_obtener_texto(NodoLinea *linea, size_t bytes_extra);
```

y el texto de tamaño desconocido de antemano (la carga desde disco, los reportes de los comandos) se acumula en una `Cadena` (`src/cadena.c`), un buffer que duplica su capacidad con `realloc` cada vez que se llena. El banco de pruebas incluye un caso con una línea de 8.000 bytes para cubrir esto.

---

# Parte 2: El índice de líneas del archivo (`archivo.c`)

Los comandos del editor operan sobre los bytes reales del archivo en disco. Para no releer el archivo completo en cada comando, `archivo.c` mantiene un **índice de offsets**: dos arreglos dinámicos paralelos que dicen, para cada línea, dónde empieza y cuánto mide.

```c
typedef struct {
    int     fd;                // descriptor devuelto por open()
    char   *ruta;
    off_t  *inicio;            // offset absoluto donde empieza cada linea
    size_t *largo;             // largo de cada linea, sin el '\n'
    int     total;
    size_t  capacidad;         // capacidad reservada (crece x2 con realloc)
    int     termina_sin_salto; // 1 si el ultimo byte no es '\n'
    off_t   tamano;
} Archivo;
```

```text
archivo.txt en disco:   h o l a \n m u n d o \n
                        ^         ^
                        |         |
inicio[] =            { 0,        5 }
largo[]  =            { 4,        5 }
```

**Cómo se construye:** `archivo_indexar()` hace `lseek` al byte 0 y lee el archivo por bloques de 4 KB, contando bytes hasta cada `\n`. Es exactamente el recorrido que pide el enunciado del comando `p`.

**Cuándo se reconstruye:** después de cada mutación (`a`, `d`, `i`, `x`) y al abrir el archivo. Así el índice nunca queda desfasado respecto al disco.

**Para qué sirve:** convierte `p 7` en un `lseek` directo al byte donde empieza la línea 7, en lugar de un recorrido desde el principio. También da a `d [n]` los dos números que necesita para desplazar la cola y truncar: el inicio de la línea y el inicio de la siguiente.

---

# Parte 3: El portapapeles secuencial (`portapapeles.c`)

El requisito de equipos de 3 pide "gestión de un portapapeles secuencial local". Se implementa como una **lista enlazada simple** de líneas copiadas:

```c
typedef struct NodoCopia {
    char             *texto;      // copia propia del contenido de la linea
    size_t            longitud;
    struct NodoCopia *siguiente;
} NodoCopia;

typedef struct {
    NodoCopia *cabeza;
    NodoCopia *cola;              // permite encolar en O(1)
    int        total;
} Portapapeles;
```

Es *secuencial* porque conserva el orden en que se copiaron las líneas:

```text
y 3      ->  [ "linea 3" ]
y 1      ->  [ "linea 3" ] -> [ "linea 1" ]
x 5      ->  inserta ambas a partir de la linea 5, en ese mismo orden
yc       ->  libera todos los nodos y vuelve a dejarlo vacio
```

Se guarda una **copia propia** del texto de cada línea, no un puntero al buffer del llamador: ese buffer se libera en cuanto el comando termina, y el portapapeles debe sobrevivirle.

---

# Parte 4: Interacción con el disco

* **Lectura**: `estructura_cargar_archivo()` lee bloques físicos con `read()`, detecta los saltos de línea, pide memoria con `malloc()` para crear los nodos, tokeniza las palabras y enlaza las estructuras.
* **Escritura (Ctrl+S)**: recorre la lista desde `cabeza`, extrae el texto de cada línea y lo persiste con `write()`, verificando escrituras parciales.
* **Comandos**: no pasan por la lista enlazada. Trabajan directamente sobre el descriptor con `lseek`, `read`, `write` y `ftruncate`, y luego la lista se recarga desde el disco. El detalle de cada comando está en [SYSCALLS.md](SYSCALLS.md).
