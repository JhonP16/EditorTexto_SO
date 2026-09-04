# Documentación Arquitectónica: Editor de Texto

Este documento explica el diseño interno, la arquitectura y el uso general de los componentes de nuestro editor de texto para terminales POSIX.

---

## 1. Diseño General y Arquitectura

El editor fue construido aplicando el principio de "Responsabilidad Única", dividiendo el código en 5 módulos fundamentales que interactúan entre sí:

* **`main.c`**: Es el punto de entrada. Inicia el sistema, llama a la configuración inicial y contiene el bucle infinito principal que lee las pulsaciones del usuario.
* **`terminal.c`**: Capa de abstracción entre el editor y el driver TTY (Teletipo) del Kernel. Configura el modo de la terminal.
* **`estructura.c`**: Es el "Back-end". Gestiona la memoria, alojando la estructura de datos (listas enlazadas) que contienen el documento.
* **`editor.c`**: Es el "Front-end" normal. Mantiene las coordenadas del cursor, maneja la inserción de texto y dibuja las filas y la barra de estado.
* **`comando.c`**: Intercepta atajos de teclado interactivos (los `Ctrl+X`) y renderiza pantallas modales de diagnóstico que se superponen al editor de texto normal.

---

## 2. El Subsistema de la Terminal (`terminal.c`)

En Linux, la terminal normalmente opera en **Modo Cooked (Cocinado)**, lo que significa que el Kernel retiene lo que escribes y sólo se lo envía al programa cuando presionas la tecla `Enter`.

Para crear un editor en tiempo real, tuvimos que construir este módulo para acceder al API de `termios.h`:

* **Modo Raw (Crudo)**: Se utiliza la llamada al sistema (syscall) `tcsetattr` para desactivar el procesamiento del Kernel (se apagan los "flags" `ECHO`, `ICANON`, y las señales de interrupción como `SIGINT`). De esta manera, el programa recibe *byte a byte* al instante.
* **Secuencias ANSI**: Se aprovecha que las terminales modernas interpretan el estándar ANSI/VT100. Mandando códigos de escape como `\x1b[2J` o `\x1b[39m` al descriptor `STDOUT_FILENO` podemos controlar colores, borrar la pantalla y mover el cursor mágicamente.

---

## 3. Modelo de Datos (`estructura.c` y `editor.c`)

No guardamos el texto entero en un solo buffer (array) gigante de memoria plana. En lugar de eso, empleamos una **Lista Enlazada Doble**.

1. **Nodos**: Cada línea es un `NodoLinea` conectado al anterior y al siguiente.
2. **Eficiencia**: Si el usuario pulsa ENTER a mitad de un archivo de 10.000 líneas, no necesitamos desplazar 5.000 líneas en la memoria; simplemente cambiamos 2 punteros para insertar el nuevo `NodoLinea` en el medio.
3. **Renderizado UI**: `editor.c` simplemente recorre esta lista desde un desplazamiento (scroll) dado y pinta línea por línea, terminando con una barra de estado generada dinámicamente usando `snprintf`.

---

## 4. Uso del Editor y Comandos (`comando.c`)

Todas las teclas combinadas con `Control` se interceptan usando una máscara de bits: `#define TECLA_CTRL(k) ((k) & 0x1f)`. Al presionar `Ctrl + Letra`, la terminal envía los primeros bits en cero.

El archivo `comando.c` actúa como el despachador (*dispatcher*). Dependiendo de lo que presiones, ejecuta estas rutinas:

* **`Ctrl + Q` (Salir)**: Limpia todo, restaura la terminal al modo Cooked y mata el proceso llamando a `exit()`.
* **`Ctrl + S` (Guardar)**: Itera la lista enlazada, junta todo el texto y hace una llamada al sistema `write()` para escribir directamente en el hardware del disco. Muestra un "Log" interactivo del proceso.
* **`Ctrl + F` (Buscar)**: Pausa la vista del editor, solicita una palabra al usuario usando el estándar `fgets()`, busca en el _estructura_ y recoloca tu cursor (`X`, `Y`) en la coincidencia.

### Pantallas de Diagnóstico Modales

* **`Ctrl + H` (Ayuda)**: Un pequeño menú de los comandos disponibles.
* **`Ctrl + E` (Estructura)**: Pinta un volcado (dump) directo de la memoria de las estructuras enlazadas (usando los punteros físicos) para entender cómo el editor aloja los datos.
* **`Ctrl + P` (Monitor de Memoria)**: Una poderosa pantalla que lee los archivos del Kernel (`/proc/self/status` y `/proc/self/maps`). Le permite al programador observar en tiempo real cuánto de la RAM física y cuánta memoria Virtual (Heap/Stack) está consumiendo el editor en vivo. Útil para debugear memory leaks.
