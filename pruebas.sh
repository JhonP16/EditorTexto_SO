#!/bin/bash
# ============================================================================
# pruebas.sh - Banco de pruebas automatico del Editor de Texto
# ============================================================================
# Aprovecha que el editor detecta con isatty() que su entrada no es un teclado
# y entra solo al interprete de comandos por STDIN. Eso permite guionizar cada
# comando del parcial y verificar DOS cosas por escenario:
#
#   1. lo que el editor respondio por pantalla, y
#   2. como quedaron realmente los bytes del archivo en disco.
#
# Uso:  ./pruebas.sh        (o  make test)
# Sale con codigo 0 si todo pasa, 1 si algo falla.
# ============================================================================

EDITOR="$(cd "$(dirname "$0")" && pwd)/editor"
TRABAJO="$(mktemp -d)"
TOTAL=0
FALLOS=0

VERDE="\033[1;32m"
ROJO="\033[1;31m"
AZUL="\033[1;36m"
GRIS="\033[0;37m"
FIN="\033[0m"

limpiar() { rm -rf "$TRABAJO"; }
trap limpiar EXIT

if [ ! -x "$EDITOR" ]; then
    echo -e "${ROJO}No se encontro el binario './editor'. Ejecute 'make' primero.${FIN}"
    exit 1
fi

# ---------------------------------------------------------------------------
# Utilidades de asercion
# ---------------------------------------------------------------------------

titulo() { echo -e "\n${AZUL}== $1 ==${FIN}"; }

# correr <archivo> <comandos>  -> ejecuta el editor con esos comandos por STDIN
correr() {
    local archivo="$1"; shift
    printf '%b' "$1" | "$EDITOR" "$archivo" 2>&1
}

# verificar <nombre> <esperado> <obtenido>
verificar() {
    local nombre="$1" esperado="$2" obtenido="$3"
    TOTAL=$((TOTAL + 1))
    if [ "$esperado" = "$obtenido" ]; then
        echo -e "  ${VERDE}OK${FIN}    $nombre"
    else
        FALLOS=$((FALLOS + 1))
        echo -e "  ${ROJO}FALLO${FIN} $nombre"
        echo -e "${GRIS}        esperado: $(printf '%q' "$esperado")${FIN}"
        echo -e "${GRIS}        obtenido: $(printf '%q' "$obtenido")${FIN}"
    fi
}

# verificar_contiene <nombre> <patron> <texto>
verificar_contiene() {
    local nombre="$1" patron="$2" texto="$3"
    TOTAL=$((TOTAL + 1))
    if echo "$texto" | grep -q -- "$patron"; then
        echo -e "  ${VERDE}OK${FIN}    $nombre"
    else
        FALLOS=$((FALLOS + 1))
        echo -e "  ${ROJO}FALLO${FIN} $nombre"
        echo -e "${GRIS}        no aparece: $patron${FIN}"
        echo -e "${GRIS}        en la salida: $(echo "$texto" | head -3)${FIN}"
    fi
}

# verificar_archivo <nombre> <ruta> <contenido esperado>
verificar_archivo() {
    verificar "$1" "$3" "$(cat "$2")"
}

echo -e "${AZUL}=========================================================${FIN}"
echo -e "${AZUL}   BANCO DE PRUEBAS DEL EDITOR DE TEXTO (Sistemas Op.)   ${FIN}"
echo -e "${AZUL}=========================================================${FIN}"
echo "Binario  : $EDITOR"
echo "Sandbox  : $TRABAJO"

# ---------------------------------------------------------------------------
titulo "COMANDOS BASE: o, p, a, d, q"
# ---------------------------------------------------------------------------

# o: abre un archivo que no existe y lo crea con open(O_CREAT)
salida=$(correr "$TRABAJO/creado.txt" 'q\n')
verificar_contiene "o crea el archivo si no existe" "abierto" "$salida"
TOTAL=$((TOTAL + 1))
if [ -f "$TRABAJO/creado.txt" ]; then
    echo -e "  ${VERDE}OK${FIN}    el archivo quedo creado en disco"
else
    FALLOS=$((FALLOS + 1)); echo -e "  ${ROJO}FALLO${FIN} el archivo no se creo"
fi

# o desde el propio interprete
printf 'uno\ndos\ntres\n' > "$TRABAJO/base.txt"
salida=$(printf 'o %s\np\nq\n' "$TRABAJO/base.txt" | "$EDITOR" 2>&1)
verificar_contiene "o [archivo] dentro del interprete" "3 linea" "$salida"

# p sin parametros: imprime todo numerado
salida=$(correr "$TRABAJO/base.txt" 'p\nq\n' | grep '|')
verificar "p imprime el archivo completo numerado" \
"   1 | uno
   2 | dos
   3 | tres" "$salida"

# p [n]: una sola linea
salida=$(correr "$TRABAJO/base.txt" 'p 2\nq\n' | grep '|')
verificar "p [n] imprime solo la linea n" "   2 | dos" "$salida"

# a: anexa al final
cp "$TRABAJO/base.txt" "$TRABAJO/anexo.txt"
correr "$TRABAJO/anexo.txt" 'a cuatro\nq\n' > /dev/null
verificar_archivo "a anade la linea al final" "$TRABAJO/anexo.txt" \
"uno
dos
tres
cuatro"

# a respeta los espacios interiores del texto
correr "$TRABAJO/anexo.txt" 'a  hola   mundo \nq\n' > /dev/null
verificar "a conserva los espacios del texto" "hola   mundo " "$(tail -1 "$TRABAJO/anexo.txt")"

# d: borra una linea del medio desplazando bytes y truncando
cp "$TRABAJO/base.txt" "$TRABAJO/borrar.txt"
correr "$TRABAJO/borrar.txt" 'd 2\nq\n' > /dev/null
verificar_archivo "d borra una linea intermedia" "$TRABAJO/borrar.txt" \
"uno
tres"
verificar "d deja el tamano exacto (ftruncate)" "9" "$(wc -c < "$TRABAJO/borrar.txt" | tr -d ' ')"

# d sobre la ultima linea
cp "$TRABAJO/base.txt" "$TRABAJO/ultima.txt"
correr "$TRABAJO/ultima.txt" 'd 3\nq\n' > /dev/null
verificar_archivo "d borra la ultima linea" "$TRABAJO/ultima.txt" \
"uno
dos"

# d sobre la unica linea -> archivo de 0 bytes
printf 'sola\n' > "$TRABAJO/sola.txt"
correr "$TRABAJO/sola.txt" 'd 1\nq\n' > /dev/null
verificar "d sobre la unica linea deja el archivo vacio" "0" \
"$(wc -c < "$TRABAJO/sola.txt" | tr -d ' ')"

# q: cierra y termina con codigo 0
printf 'q\n' | "$EDITOR" "$TRABAJO/base.txt" > /dev/null 2>&1
verificar "q sale con codigo 0" "0" "$?"

# ---------------------------------------------------------------------------
titulo "SELECCION DE INTERFAZ: bandera -c e isatty()"
# ---------------------------------------------------------------------------

salida=$(printf 'p\nq\n' | "$EDITOR" -c "$TRABAJO/base.txt" 2>&1 | grep '|')
verificar "-c fuerza el modo CLI" \
"   1 | uno
   2 | dos
   3 | tres" "$salida"

salida=$(printf 'q\n' | "$EDITOR" --cli "$TRABAJO/base.txt" 2>&1)
verificar_contiene "--cli es equivalente a -c" "abierto" "$salida"

salida=$("$EDITOR" --help 2>&1)
verificar_contiene "--help documenta la bandera -c" "\-c, --cli" "$salida"

salida=$("$EDITOR" -z 2>&1)
verificar "una bandera desconocida sale con codigo 1" "1" "$?"
verificar_contiene "una bandera desconocida se reporta" "Opción desconocida" "$salida"

# Con una tuberia se entra al modo CLI aunque no se pase -c (isatty == 0)
salida=$(printf 'q\n' | "$EDITOR" "$TRABAJO/base.txt" 2>&1)
verificar_contiene "sin -c, una tuberia activa el modo CLI" "abierto" "$salida"

# ---------------------------------------------------------------------------
titulo "EQUIPOS DE 2: insercion arbitraria (i) y busqueda (s)"
# ---------------------------------------------------------------------------

cp "$TRABAJO/base.txt" "$TRABAJO/insertar.txt"
correr "$TRABAJO/insertar.txt" 'i 1 cero\nq\n' > /dev/null
verificar_archivo "i inserta al principio desplazando el resto" "$TRABAJO/insertar.txt" \
"cero
uno
dos
tres"

correr "$TRABAJO/insertar.txt" 'i 3 dos-y-medio\nq\n' > /dev/null
verificar_archivo "i inserta en medio del archivo" "$TRABAJO/insertar.txt" \
"cero
uno
dos-y-medio
dos
tres"

# insertar mas alla del final degrada a anexado
cp "$TRABAJO/base.txt" "$TRABAJO/lejos.txt"
salida=$(correr "$TRABAJO/lejos.txt" 'i 99 final\nq\n')
verificar_contiene "i mas alla del final avisa y anexa" "se anadio al final" "$salida"
verificar "i mas alla del final deja el texto de ultimo" "final" \
"$(tail -1 "$TRABAJO/lejos.txt")"

# s: busca y reporta linea y columna
printf 'alfa beta\ngamma\nbeta final\n' > "$TRABAJO/buscar.txt"
salida=$(correr "$TRABAJO/buscar.txt" 's beta\nq\n')
verificar_contiene "s encuentra todas las coincidencias" "2 coincidencia" "$salida"
verificar_contiene "s reporta el numero de linea" "   1 | col 6" "$salida"
verificar_contiene "s reporta la segunda coincidencia" "   3 | col 1" "$salida"

salida=$(correr "$TRABAJO/buscar.txt" 's inexistente\nq\n')
verificar_contiene "s informa cuando no hay coincidencias" "no aparece" "$salida"

# ---------------------------------------------------------------------------
titulo "EQUIPOS DE 3: metadatos (m) y portapapeles (y / x)"
# ---------------------------------------------------------------------------

salida=$(correr "$TRABAJO/base.txt" 'm\nq\n')
inodo_real=$(stat -c '%i' "$TRABAJO/base.txt")
tamano_real=$(stat -c '%s' "$TRABAJO/base.txt")
verificar_contiene "m reporta el inodo real del archivo" "Numero de inodo : $inodo_real" "$salida"
verificar_contiene "m reporta el tamano real"           "Tamano          : $tamano_real bytes" "$salida"
verificar_contiene "m reporta los permisos en rwx"      "Permisos        : -rw" "$salida"
verificar_contiene "m reporta la fecha de modificacion" "Ultima modific. : 20" "$salida"

# y acumula en orden y x los pega todos juntos
printf 'a\nb\nc\n' > "$TRABAJO/copiar.txt"
salida=$(correr "$TRABAJO/copiar.txt" 'y 1\ny 3\ny\nx 2\nq\n')
verificar_contiene "y acumula lineas en el portapapeles" "PORTAPAPELES SECUENCIAL (2 linea" "$salida"
verificar_archivo "x pega el portapapeles completo y en orden" "$TRABAJO/copiar.txt" \
"a
a
c
b
c"

# yc vacia el portapapeles
salida=$(correr "$TRABAJO/copiar.txt" 'y 1\nyc\nx 1\nq\n')
verificar_contiene "yc vacia el portapapeles" "portapapeles esta vacio" "$salida"

# x sin nada copiado es un error controlado
salida=$(correr "$TRABAJO/copiar.txt" 'x 1\nq\n')
verificar_contiene "x sin copiar previamente avisa" "portapapeles esta vacio" "$salida"

# ---------------------------------------------------------------------------
titulo "CASOS BORDE"
# ---------------------------------------------------------------------------

# Archivo vacio
: > "$TRABAJO/vacio.txt"
salida=$(correr "$TRABAJO/vacio.txt" 'p\nq\n')
verificar_contiene "p sobre archivo vacio no revienta" "archivo vacio" "$salida"

# Archivo sin salto de linea final
printf 'sin-salto' > "$TRABAJO/nosalto.txt"
salida=$(correr "$TRABAJO/nosalto.txt" 'p\nq\n')
verificar_contiene "la ultima linea sin salto se indexa igual" "sin-salto" "$salida"
correr "$TRABAJO/nosalto.txt" 'a segunda\nq\n' > /dev/null
verificar_archivo "a inserta el salto que faltaba antes de anexar" "$TRABAJO/nosalto.txt" \
"sin-salto
segunda"

# Linea mas larga que el bloque de lectura de 4096 bytes
python3 -c "print('L'*8000)" > "$TRABAJO/larga.txt" 2>/dev/null || \
    awk 'BEGIN{s="";for(i=0;i<8000;i++)s=s "L";print s}' > "$TRABAJO/larga.txt"
correr "$TRABAJO/larga.txt" 'a corta\nq\n' > /dev/null
verificar "una linea de 8000 bytes sobrevive intacta" "8000" \
"$(head -1 "$TRABAJO/larga.txt" | wc -c | tr -d ' ' | awk '{print $1-1}')"

# Numeros de linea invalidos
salida=$(correr "$TRABAJO/base.txt" 'd 99\np 0\np abc\ni 0 x\nq\n')
verificar_contiene "d con linea inexistente da error controlado" "no existe" "$salida"
verificar_contiene "p 0 es rechazado"                            "Uso: p \[n\]" "$salida"
verificar_contiene "p con texto no numerico es rechazado"        "no es un numero" "$salida"
verificar_contiene "i 0 es rechazado"                            "Uso: i \[n\]" "$salida"
verificar_archivo "ningun comando invalido modifico el archivo" "$TRABAJO/base.txt" \
"uno
dos
tres"

# Comando inexistente
salida=$(correr "$TRABAJO/base.txt" 'zz\nq\n')
verificar_contiene "un comando desconocido se reporta" "Comando desconocido" "$salida"

# Comandos sin archivo abierto
salida=$(printf 'p\na hola\nd 1\nm\nq\n' | "$EDITOR" 2>&1)
verificar_contiene "sin archivo abierto los comandos avisan" "no hay ningun archivo abierto" "$salida"

# Apertura imposible: perror + codigo de salida distinto de cero
salida=$(printf 'q\n' | "$EDITOR" "$TRABAJO/no/existe/x.txt" 2>&1)
codigo=$?
verificar_contiene "una apertura fallida reporta con perror" "open:" "$salida"
verificar "una apertura fallida sale con codigo 1" "1" "$codigo"

# Ayuda
salida=$("$EDITOR" --help 2>&1)
verificar_contiene "--help documenta los comandos" "o \[archivo\]" "$salida"

# ---------------------------------------------------------------------------
titulo "MODO VISUAL: atajos de teclado sobre un pseudo-terminal"
# ---------------------------------------------------------------------------
# El modo visual solo arranca si isatty() ve una terminal, asi que estas
# pruebas usan 'script' para darle un pseudo-terminal y le envian pulsaciones
# reales. Hacen falta pausas entre teclas porque cada vez que el editor vuelve
# al modo raw usa TCSAFLUSH, que descarta la entrada pendiente.
#
# Se pueden omitir (son lentas) con:  SIN_VISUAL=1 ./pruebas.sh

# correr_visual <archivo> <tecla> [tecla...]
correr_visual() {
    local archivo="$1"; shift
    {
        sleep 1;  printf '\n'      # cierra el reporte de carga inicial
        sleep 3                     # espera a que pase la pantalla de bienvenida
        local tecla
        for tecla in "$@"; do
            printf '%b' "$tecla"
            sleep 2
        done
        sleep 2
    } | timeout 90 script -qec "$EDITOR $archivo" /dev/null > /dev/null 2>&1
}

if [ -n "$SIN_VISUAL" ]; then
    echo -e "  ${GRIS}omitidas (SIN_VISUAL activo)${FIN}"
elif ! command -v script > /dev/null 2>&1; then
    echo -e "  ${GRIS}omitidas: falta la herramienta 'script' (paquete util-linux)${FIN}"
else
    # Ctrl+D borra la linea del cursor. Al abrir, el cursor esta en la ultima.
    printf 'uno\ndos\ntres\n' > "$TRABAJO/vis1.txt"
    correr_visual "$TRABAJO/vis1.txt" '\x04' '\n' '\n' '\x11'
    verificar_archivo "Ctrl+D + ENTER borra la linea del cursor" "$TRABAJO/vis1.txt" \
"uno
dos"

    # Ctrl+A pide el texto y lo anexa al final
    printf 'uno\ndos\n' > "$TRABAJO/vis2.txt"
    correr_visual "$TRABAJO/vis2.txt" '\x01' 'tres\n' '\n' '\x11'
    verificar_archivo "Ctrl+A anade la linea pedida por pantalla" "$TRABAJO/vis2.txt" \
"uno
dos
tres"

    # Ctrl+Q termina con codigo 0
    printf 'uno\n' > "$TRABAJO/vis3.txt"
    correr_visual "$TRABAJO/vis3.txt" '\x11'
    verificar "Ctrl+Q sale del modo visual con codigo 0" "0" "$?"
fi

# ---------------------------------------------------------------------------
titulo "INTEGRACION CON EL SHELL DE CLASE (categoria 'aplicaciones')"
# ---------------------------------------------------------------------------

DIR_SHELL="$(cd "$(dirname "$0")" && pwd)/shell"

if [ ! -d "$DIR_SHELL" ]; then
    echo -e "  ${GRIS}omitidas: no existe el directorio shell/${FIN}"
elif ! command -v make > /dev/null 2>&1; then
    echo -e "  ${GRIS}omitidas: falta 'make' para compilar el shell${FIN}"
else
    make -C "$DIR_SHELL" compilar > /dev/null 2>&1
    SHELL_BIN="$DIR_SHELL/eafitOS"

    TOTAL=$((TOTAL + 1))
    if [ -x "$SHELL_BIN" ]; then
        echo -e "  ${VERDE}OK${FIN}    el shell compila con la categoria nueva"
    else
        FALLOS=$((FALLOS + 1))
        echo -e "  ${ROJO}FALLO${FIN} el shell no compilo"
    fi

    if [ -x "$SHELL_BIN" ]; then
        # correr_shell <comandos del shell>
        correr_shell() { (cd "$DIR_SHELL" && printf '%b' "$1" | ./eafitOS 2>&1); }

        salida=$(correr_shell 'help\nexit\n')
        verificar_contiene "'help' anuncia la categoria aplicaciones" "aplicaciones" "$salida"

        salida=$(correr_shell 'help aplicaciones\nexit\n')
        verificar_contiene "'help aplicaciones' lista el comando editor"     "editor " "$salida"
        verificar_contiene "'help aplicaciones' lista el comando editor_cmd" "editor_cmd" "$salida"

        salida=$(correr_shell 'help editor\nexit\n')
        verificar_contiene "'help editor' detalla sus syscalls" "execv(2)" "$salida"

        # El shell localiza el editor con access() sin ayuda del usuario
        salida=$(correr_shell 'editor_cmd /dev/null "h"\nexit\n')
        verificar_contiene "el shell localiza el editor con access()" "access" "$salida"

        # editor_cmd modifica de verdad los bytes del archivo
        printf 'uno\ndos\ntres\n' > "$TRABAJO/desde_shell.txt"
        correr_shell "editor_cmd $TRABAJO/desde_shell.txt \"a cuarta; d 2\"\nexit\n" > /dev/null
        verificar_archivo "editor_cmd ejecuta las ordenes sobre el archivo" \
            "$TRABAJO/desde_shell.txt" \
"uno
tres
cuarta"

        # La tuberia demuestra pipe/dup2 y el editor termina solo con el EOF
        salida=$(correr_shell "editor_cmd $TRABAJO/desde_shell.txt \"p\"\nexit\n")
        verificar_contiene "editor_cmd traza pipe(2)"    "pipe" "$salida"
        verificar_contiene "editor_cmd traza dup2 vía la tuberia" "tuberia\[0\]" "$salida"
        verificar_contiene "el editor termina solo al cerrar la tuberia" "terminó correctamente" "$salida"

        # El shell reenvia al editor las banderas tal cual, sin interpretarlas
        salida=$(correr_shell 'editor --help\nexit\n')
        verificar_contiene "'editor' reenvia --help al editor" "\-c, --cli" "$salida"

        salida=$(correr_shell 'editor -z\nexit\n')
        verificar_contiene "una bandera invalida llega al editor y este la rechaza" \
            "terminó con código" "$salida"

        # 'editor -c' abre el interprete de linea DENTRO del shell.
        # Necesita un pseudo-terminal: con una tuberia el editor y el shell
        # competirian por la misma entrada estandar.
        if command -v script > /dev/null 2>&1 && [ -z "$SIN_VISUAL" ]; then
            printf 'uno\ndos\ntres\n' > "$TRABAJO/desde_shell_cli.txt"
            (cd "$DIR_SHELL" && {
                sleep 1; printf "editor -c $TRABAJO/desde_shell_cli.txt\n"
                sleep 2; printf 'd 2\n'
                sleep 1; printf 'q\n'
                sleep 1; printf 'exit\n'
                sleep 1
             } | timeout 60 script -qec './eafitOS' /dev/null > /dev/null 2>&1)
            verificar_archivo "'editor -c' abre el interprete de linea desde el shell" \
                "$TRABAJO/desde_shell_cli.txt" \
"uno
tres"
        else
            echo -e "  ${GRIS}omitida: 'editor -c' interactivo (requiere 'script')${FIN}"
        fi

        # Uso incorrecto
        salida=$(correr_shell 'editor_cmd\nexit\n')
        verificar_contiene "editor_cmd sin argumentos muestra su uso" "Uso: editor_cmd" "$salida"

        # EDITOR_SO tiene prioridad sobre las rutas por defecto
        printf 'x\ny\n' > "$TRABAJO/con_var.txt"
        (cd "$TRABAJO" && printf "editor_cmd $TRABAJO/con_var.txt \"d 1\"\nexit\n" | \
            EDITOR_SO="$EDITOR" "$SHELL_BIN" > /dev/null 2>&1)
        verificar_archivo "EDITOR_SO permite indicar la ruta del editor" \
            "$TRABAJO/con_var.txt" "y"

        # Sin editor a la vista, el error es claro y no revienta
        salida=$(cd "$TRABAJO" && printf 'editor_cmd x.txt "p"\nexit\n' | "$SHELL_BIN" 2>&1)
        verificar_contiene "sin binario del editor el shell avisa con claridad" \
            "No se encontró el binario del editor" "$salida"
    fi
fi

# ---------------------------------------------------------------------------
titulo "SESION COMPLETA (el guion de la demostracion)"
# ---------------------------------------------------------------------------

printf 'uno\ndos\ntres\n' > "$TRABAJO/demo.txt"
correr "$TRABAJO/demo.txt" 'a cuatro\nd 2\ni 1 cero\ny 1\nx 4\nq\n' > /dev/null
verificar_archivo "secuencia a->d->i->y->x deja el archivo esperado" "$TRABAJO/demo.txt" \
"cero
uno
tres
cero
cuatro"

# ---------------------------------------------------------------------------
echo
echo -e "${AZUL}=========================================================${FIN}"
if [ "$FALLOS" -eq 0 ]; then
    echo -e "${VERDE}  TODAS LAS PRUEBAS PASARON: $TOTAL/$TOTAL${FIN}"
    echo -e "${AZUL}=========================================================${FIN}"
    exit 0
else
    echo -e "${ROJO}  PRUEBAS FALLIDAS: $FALLOS de $TOTAL${FIN}"
    echo -e "${AZUL}=========================================================${FIN}"
    exit 1
fi
