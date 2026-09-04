# Arquitectura de la Estructura de Datos (estructura.c)

El editor de texto SO2026B utiliza un modelo de datos en memoria diseñado explícitamente para soportar la edición ágil de texto. En lugar de cargar todo el archivo en una sola cadena (array) gigante de bytes, el programa descompone el texto utilizando **Listas Enlazadas (Linked Lists)**.

## 1. ¿Por qué usar Listas Enlazadas?
Si el editor almacenara un archivo entero (por ejemplo de 50.000 líneas) en un solo vector continuo en memoria RAM, cada vez que el usuario escribiera una sola letra al principio del archivo, el procesador tendría que desplazar en cascada todos los datos restantes para hacerle espacio. Esto causaría retrasos inaceptables ("lag").

Al utilizar una **Lista Doblemente Enlazada de Líneas**, si se inserta un "ENTER" o se modifica un párrafo, el sistema operativo solo debe reasignar memoria localmente y reconectar unos punteros. El resto del archivo no se mueve de la memoria y la operación ocurre de manera instantánea.

## 2. Jerarquía de Estructuras (Structs)

Toda la información reside en tres niveles lógicos (declarados en `src/estructura.h`):

### A. `EstructuraTexto` (El Documento)
Es el contenedor raíz. Representa la totalidad del archivo en RAM. Su única responsabilidad es mantener la referencia de dónde comienza el texto.
*   **`cabeza`**: Puntero a la primera línea del documento (apunta al primer `NodoLinea`).

### B. `NodoLinea` (La Fila de Texto)
Actúa como el andamio vertical del documento. Es una **Lista Doblemente Enlazada**, lo que permite recorrer el texto hacia abajo (siguiente) o hacia arriba (anterior).
*   **`longitud`**: El recuento total de caracteres que contiene la fila.
*   **`siguiente` y `anterior`**: Punteros a los demás nodos de línea (para bajar o subir de renglón).
*   **`palabras_cabeza`**: Puntero al primer bloque de texto (la primera palabra de la fila).

### C. `NodoPalabra` (La Palabra o Bloque de Texto)
Para dar pie a funcionalidades complejas (como resaltado de sintaxis, conteo léxico o búqueda rápida de palabras enteras), cada línea subdivide su texto horizontalmente.
*   **`texto`**: La cadena de caracteres de la palabra real.
*   **`longitud`**: Cuánto mide esta palabra en memoria.
*   **`siguiente`**: Puntero a la siguiente palabra alojada en esta misma fila.

---

## 3. Representación Visual en la Memoria del Sistema

Este es el aspecto esquemático de los punteros (diagrama lógico) generados al guardar un documento con dos renglones:

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

---

## 4. Interacción con el Hardware (Disco Duro)
El módulo `estructura.c` se encarga de serializar y deserializar esta matriz de memoria:

*   **Lectura de Archivos**: Al arrancar, un bucle lee porciones (chunks) físicos desde el disco duro haciendo un _Call System_ (`read()`). El algoritmo detecta los espacios en blanco y saltos de línea (`\n`), pide memoria con `malloc()` al Kernel para crear un nodo, inserta la palabra y enlaza las estructuras.
*   **Escritura (Ctrl+S)**: Al revés. Empieza desde la `cabeza`, recorre el árbol de punteros extrayendo el `texto` de cada palabra y línea, lo agrupa en un buffer grande y ejecuta el _Call System_ `write()` enviando la información consolidada al hardware de persistencia.
