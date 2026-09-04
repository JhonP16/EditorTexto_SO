# Manual de Usuario - Editor

Bienvenido al **Editor de Texto**.



 Este es un editor ágil y minimalista que opera directamente en modo texto (CLI), construido para funcionar desde la consola de Linux sin requerir interfaces gráficas pesadas.

---

## 1. Compilación e Inicio

Antes de utilizar el editor por primera vez, debes compilar el código fuente ejecutando la herramienta de construcción `make`:

```bash
make clean
make
```

Para abrir un archivo de texto existente o crear uno nuevo en blanco, ejecuta el programa binario pasándole el nombre del archivo como argumento:

```bash
./editor mi_archivo.txt
```

./editor demo.txt



## 2. Entorno y Barra de Estado

Una vez dentro, verás el contenido de tu documento y una **Barra de Estado** resaltada en la fila inferior de tu consola. Esta barra se actualiza en tiempo real y te muestra:

* El nombre del archivo actual (`mi_archivo.txt` o `[Nuevo]` si olvidaste pasar un argumento).
* La posición de tu cursor indicada por **Fila** (Línea) y **Columna**.
* Un recordatorio rápido de los atajos principales del sistema.

---

## 3. Navegación y Edición Básica

Para editar el documento, simplemente empieza a escribir. El texto se irá insertando de forma fluida.
Puedes moverte libremente por el documento utilizando:

* **Teclas de Dirección**: Flechas `Arriba`, `Abajo`, `Izquierda`, `Derecha`.
* **W, A, S, D**: A modo de atajo alternativo para usuarios ágiles, estas cuatro letras actúan como flechas de navegación directas, evitando tener que mover tu mano derecha lejos del centro del teclado.
* **Backspace (Retroceso) / Enter**: Funcionan de manera habitual para eliminar caracteres hacia atrás o dividir líneas.

---

## 4. Atajos de Comandos (Ctrl)

El editor se controla mediante combinaciones rápidas utilizando la tecla `Control (Ctrl)`. Todos los comandos operan de manera instantánea:

* **`Ctrl + S` (Guardar Documento)**: Escribe todos tus cambios en el disco duro. Al presionarlo, la pantalla se pausará y mostrará un *log* confirmando la llamada al sistema de escritura. Solo presiona `ENTER` para retomar tu edición.
* **`Ctrl + Q` (Salir del Editor)**: Cierra inmediatamente el editor, limpia la pantalla y te regresa a tu línea de comandos regular. ¡Recuerda hacer `Ctrl+S` antes de salir para no perder nada!
* **`Ctrl + F` (Buscar Palabra)**: Congela el entorno y te abre un menú de búsqueda en la parte inferior. Escribe la palabra que deseas localizar y presiona `ENTER`. El cursor viajará mágicamente a la coincidencia.

---

## 5. Herramientas de Diagnóstico (Modales)

Como característica especial, el editor SO2026B incluye "Pantallas Modales" interactivas (se superponen al texto). Para salir de ellas y volver a la edición, simplemente **vuelve a presionar el mismo atajo que las abrió**.

* **`Ctrl + H` (Menú de Ayuda)**: Despliega en el centro de la pantalla la lista maestra de comandos para que nunca te pierdas.
* **`Ctrl + E` (Visor de Estructura)**: Destripa el archivo mostrándote cómo están organizadas físicamente las palabras dentro de la RAM. Muestra punteros de Listas Enlazadas de los nodos.
* **`Ctrl + P` (Monitor de Memoria OS)**: Extrae e imprime la telemetría del núcleo de Linux (`/proc/self/status`), mostrándole al programador cuántos kilobytes está consumiendo el Editor en el segmento *Heap* (mallocs) y en el segmento *Stack*.
