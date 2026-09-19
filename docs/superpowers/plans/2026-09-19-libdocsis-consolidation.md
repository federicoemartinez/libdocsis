# libdocsis: consolidación en librería — Plan de implementación

> **Para trabajadores agénticos:** SUB-SKILL REQUERIDA: usar
> superpowers:subagent-driven-development (recomendado) o
> superpowers:executing-plans para implementar tarea por tarea. Los pasos usan
> sintaxis de checkbox (`- [ ]`) para seguimiento.

**Goal:** Convertir este fork de rlaager/docsis en una librería compartida
instalable, testeada y al día con upstream.

**Architecture:** El CLI deja de ser producto y pasa a ser arnés de test
(`check_PROGRAMS`), lo que preserva los 142 fixtures existentes. El pipeline de
encode, hoy duplicado entre CLI y librería, se unifica sobre
`encode_one_string()`, de modo que la suite existente pase a ejercitar el código
que realmente se exporta. El build usa autotools + libtool, que ya están
cableados en `configure.ac` pero sin usar.

**Tech Stack:** C89/C99, flex, bison, net-snmp, autotools + libtool, pthread.

**Spec:** `docs/superpowers/specs/2026-09-19-libdocsis-consolidation-design.md`

**Rama:** `lib-consolidation`. **No pushear nada** — instrucción explícita del
usuario. El remoto `upstream` tiene la URL de push deshabilitada a propósito.

## Global Constraints

- **La API pública es exactamente estos tres símbolos**, con estos nombres:
  `initialize`, `encode_file`, `free_encoded_file`. No se renombran: hay código
  Python llamándolos por `ctypes`.
- **La firma de `encode_file()` no cambia.** `initialize()` cambia de `void` a
  `int` (transparente para `ctypes`).
- **`0` significa fallo en toda la API, sin excepciones.** `encode_file()`
  devuelve el largo (`> 0`) o `0`. `initialize()` devuelve `1` o `0`.
- **`encode_one_string()` nunca escribe a `stdout`.** Los errores van a `stderr`.
  Los `printf` de progreso viven solo en el camino del CLI.
- **No se exporta decode.** `docsis_decode.c` queda como verificador de
  round-trip en los tests.
- **No se hace reentrancia completa.** Se resuelve con un mutex y queda un TODO.
- **Plataforma POSIX solamente.** `fmemopen()` no existe en MSVC.
- **Todo comentario de simplificación deliberada lleva el prefijo `ponytail:`**
  nombrando el techo y el camino de mejora.

---

### Task 1: Baseline reproducible

Sin esto, el merge de la Task 2 es a ciegas. Este codebase emite binarios
firmados con HMAC-MD5: un error de codificación no produce un stack trace, sino
un `.cm` sutilmente incorrecto que un cable módem rechaza en campo. Necesitamos
saber qué pasa **hoy** antes de cambiar una línea.

**Files:**
- Create: `docs/superpowers/specs/2026-09-19-baseline.txt`
- Delete: `tests/docsis1_0_2class_non-rfc2669.failed`,
  `tests/docsis1_0_2class_rfc2669.failed`, y los otros dos `.failed`

**Interfaces:**
- Consumes: nada.
- Produces: `2026-09-19-baseline.txt`, el conjunto de tests que pasan hoy. Todas
  las tareas siguientes comparan contra este archivo.

- [ ] **Step 1: Confirmar el toolchain**

```bash
for t in autoconf automake libtoolize flex bison net-snmp-config gcc make; do
  printf "%-18s %s\n" "$t" "$(command -v $t || echo FALTA)"
done
ls /usr/include/net-snmp/net-snmp-config.h
```

Esperado: ninguno dice FALTA y el header de net-snmp existe. Nota: el binario
`libtool` puede no estar (paquete `libtool-bin`); no importa, `LT_INIT` genera
su propio script `libtool` en el directorio de build. Lo que sí hace falta es
`libtoolize`.

- [ ] **Step 2: Build limpio**

```bash
./autogen.sh && ./configure && make 2>&1 | tee /tmp/build.log
```

Esperado: `src/docsis` existe. Si falla, **parar y reportar el error completo
antes de seguir** — arreglar el build es parte de esta tarea y hay que decidir
el alcance con el usuario antes de tocar nada.

- [ ] **Step 3: Correr la suite de 142 casos y capturar el resultado**

`RunTests.sh` no exporta `MIBDIRS`, así que hay que dárselo (es lo que hace
`examples/Makefile.am:41`). Se usa `--allow-failed=TRUE` para que no corte en el
primer fallo: queremos el cuadro completo, no el primer síntoma.

```bash
cd tests
MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" \
  ./RunTests.sh --allow-failed=TRUE 2>&1 | tee /tmp/runtests.log
grep -c "failed\|wrong" /tmp/runtests.log
cd ..
```

- [ ] **Step 4: Correr `make check` y confirmar la rotura esperada**

```bash
make check 2>&1 | tee /tmp/makecheck.log; echo "exit=$?"
```

Esperado: `examples/` falla. La causa prevista es que
`examples/Makefile.am:42` hace `sed -n '/^Final content/,$p'` sobre la salida
de `docsis -e`, y el fork comentó ese `fprintf` en `src/docsis.c:578`.
Confirmar que ese es el motivo y no otro.

- [ ] **Step 5: Escribir el baseline**

Crear `docs/superpowers/specs/2026-09-19-baseline.txt` con, textualmente:
el comando usado, la cantidad de casos de `RunTests.sh` que pasaron sobre 142,
la lista de los que fallaron (si hay), el resultado de `make check` por
subdirectorio, y la versión de las herramientas (`gcc --version`,
`net-snmp-config --version`, `flex --version`, `bison --version`).

- [ ] **Step 6: Borrar los `.failed` commiteados**

Son residuos de una corrida rota que quedaron versionados.

```bash
git rm tests/*.failed
```

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/specs/2026-09-19-baseline.txt
git commit -m "test: registrar el baseline de la suite antes de tocar nada

Deja constancia de qué pasa y qué no en el árbol actual, para poder
distinguir una regresión introducida por el merge de upstream de un
fallo que ya existía."
```

**Criterio de salida:** existe un conjunto conocido de tests que pasan, y se
sabe exactamente cuáles no y por qué.

---

### Task 1b: Regresión del fork — los TLV de longitud cero

**Descubierta al ejecutar la Task 1.** No estaba en el plan original porque
hacía falta el baseline para verla. Se intercala acá, antes del merge, porque
es un bug en código propio y arreglarlo sobre el árbol actual es más barato
que después de traer 18 commits encima.

El fork rechaza 7 configs válidas. `create_tlv()` trata `tlv_len == 0` como
error, pero `encode_lenzero()` devuelve 0 **a propósito**: un TLV de longitud
cero es una construcción válida de DOCSIS (L2VPN-I13 Annex B.3.2). Upstream
tenía ese check comentado deliberadamente; el fork lo habilitó al migrar de
`exit()` a códigos de retorno, sin notar que el dominio del valor de retorno
se había ensanchado de dos estados a tres: negativo = error, cero = TLV vacío
legítimo, positivo = TLV con datos.

`encode_lenzero` aparece en exactamente 7 entradas de `docsis_symtable.h`, una
por cada test roto.

**Files:**
- Modify: `src/docsis_yy.y` (`create_tlv`)

- [x] **Step 1: El test que falla ya existe**

Los 7 fixtures de `tests/` son el test. Confirmado contra un build del punto de
fork: el binario pre-fork produce el md5 esperado, HEAD no produce archivo.

- [x] **Step 2: Arreglar en la raíz**

```c
  tlv_len = sym_ptr->encode_func(tlvbuf->tlv_value,value,sym_ptr);
  tlvbuf->tlv_len = tlv_len;
  /* Solo negativo es error. Cero es un TLV de longitud cero legítimo. */
  if (tlv_len < 0 ) {
		free(tlvbuf);
		return 0;
  }
  return tlvbuf;
```

De paso libera `tlvbuf`, que en el camino de error se perdía.

**No tocar los otros siete `<= 0` de `docsis_yy.y`.** Upstream los tenía
habilitados con `exit()` y ahí el cero sí es un error: ninguno de esos caminos
(SNMP OIDs, strings, tipos genéricos explícitos) usa un encoder de longitud
cero. Cambiarlos sería ensanchar el diff contra upstream sin motivo.

- [x] **Step 3: Verificar**

Los 7 dan byte a byte el md5 esperado, y la suite completa pasa de 128/142 a
135/142. Los 7 restantes son el grupo B del baseline: preexistentes de
upstream, a revisar después del merge.

---

### Task 2: Merge de upstream (18 commits)

Va **antes** de reestructurar. Cada commit de reorganización aleja el árbol de
upstream y encarece los conflictos. Hoy `docsis_symtable.h` (152 líneas, el
grueso del cambio) entra sin conflicto porque el fork nunca lo tocó; después de
mover archivos, no.

**Files:**
- Modify: `src/docsis_yy.y`, `src/docsis_common.h`, `src/docsis_lex.l`,
  `src/docsis.h` (conflictos)
- Modify: `src/docsis_symtable.h`, `src/docsis_snmp.c`, `src/docsis_decode.c`
  (sin conflicto, entran solos)
- Add: `examples/ptp.*`, `examples/docsis3.1_snmp_cvc.*`,
  `examples/packetcable_digitmap.*`, `examples/d31*cvc.p7b`

**Interfaces:**
- Consumes: el baseline de la Task 1.
- Produces: un árbol que compila y pasa lo mismo que el baseline, más los TLVs
  nuevos: TLV84 (Diplexer Band Edge), TLV217 (eSTB), MULPIv4.0-I07, Sync/PTP de
  SYNC-I03, `DigitMap`, `ManufacturerCVCChainFile`, `CoSignerCVCChainFile`.

- [ ] **Step 1: Verificar que el árbol está limpio y anotar el punto de partida**

```bash
git status --short          # tiene que estar vacío
git rev-parse HEAD > /tmp/pre-merge.sha
cat /tmp/pre-merge.sha
```

Si algo aparece en `git status`, parar: el merge necesita un árbol limpio para
que los conflictos sean legibles.

- [ ] **Step 2: Traer upstream y ver el tamaño real del conflicto**

```bash
git fetch upstream
git merge --no-commit --no-ff upstream/master
git status --short | grep '^UU\|^AA'
```

Esperado: conflictos en `src/docsis_yy.y`, `src/docsis_common.h`,
`src/docsis_lex.l`, `src/docsis.h`. Todo lo demás se auto-mergea.

- [ ] **Step 3: Resolver `src/docsis_common.h`**

Upstream toca 15 líneas acá. El único cambio del fork en este archivo son dos
líneas en blanco agregadas (commit `53a6cef`), que no aportan nada.

Resolución: **tomar la versión de upstream tal cual** para este archivo.

```bash
git checkout --theirs src/docsis_common.h
git add src/docsis_common.h
```

Después verificar que `NUM_IDENTIFIERS` subió, porque upstream agregó TLVs:

```bash
grep -n "NUM_IDENTIFIERS" src/docsis_common.h
```

Este número tiene que coincidir con la cantidad de entradas de
`symtable[]` en `src/docsis_symtable.h`. Si upstream lo cambió, la Task 10
(que arregla `global_symtable[125576]`) depende de este valor nuevo.

- [ ] **Step 4: Resolver `src/docsis.h` y `src/docsis_lex.l`**

Conflictos menores (2 y 3 líneas). Regla de resolución, en este orden:

1. Las declaraciones nuevas de upstream se conservan.
2. Los cambios de reentrancia del fork se conservan: `yylval.` → `yylval->`,
   `%option reentrant bison-bridge`, el `typedef void* yyscan_t`.
3. Donde upstream agregue una regla de lexer nueva que use `yylval.campo`,
   convertirla a `yylval->campo`. **Esto es obligatorio**: con
   `bison-bridge`, `yylval` es un puntero, y `yylval.campo` ni siquiera
   compila — pero si upstream agregó la regla dentro de un bloque que el
   merge dejó de un solo lado, puede pasar desapercibido hasta runtime.

- [ ] **Step 5: Resolver `src/docsis_yy.y` — el conflicto grande**

Upstream agrega 44 líneas (reglas para `DigitMap` y los CVC chain files). El
fork reescribió este archivo entero (pure parser, `YYABORT`).

Regla de resolución para cada regla de gramática nueva de upstream:

```
# upstream escribe así:
| T_IDENT_X T_STRING ';' { $$ = create_something($1, (union t_val *)&$2); }

# en este árbol tiene que quedar así:
| T_IDENT_X T_STRING ';' { $$ = create_something($1, (union t_val *)&$2); if($$ == 0) YYABORT; }
```

Y cada constructor nuevo que upstream haya agregado en el epílogo del `.y`:

```
# upstream:
if (tlvbuf->tlv_len <= 0) { fprintf(stderr, "..."); exit(-1); }

# acá:
int tlv_len = <la llamada de encode>;
tlvbuf->tlv_len = tlv_len;
if (tlv_len <= 0) { fprintf(stderr, "..."); return 0; }
```

**La variable `int` intermedia no es opcional.** `tlv->tlv_len` es
`unsigned short`: asignarle `-1` lo convierte en 65535 y la comparación
`<= 0` nunca dispara. Ese fue un bug real que el fork ya arregló en los
constructores existentes; hay que aplicarlo a los nuevos.

Conservar del fork: `%define api.pure full`, `%param { yyscan_t scanner }`,
el bloque `%code requires`, `yyerror(yyscan_t, const char*)`,
`parse_config_string()`.

- [ ] **Step 6: Verificar que compila**

```bash
make clean && make 2>&1 | tee /tmp/merge-build.log
grep -i "warning: implicit\|error" /tmp/merge-build.log
```

Esperado: compila. Prestar atención a warnings de declaración implícita: son
la señal de que una función nueva de upstream quedó sin declarar en `docsis.h`.

- [ ] **Step 7: Verificar contra el baseline**

```bash
cd tests
MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" \
  ./RunTests.sh --allow-failed=TRUE 2>&1 | tee /tmp/postmerge.log
cd ..
diff <(grep -c "failed\|wrong" /tmp/runtests.log) <(grep -c "failed\|wrong" /tmp/postmerge.log)
```

Esperado: la misma cantidad de fallos que el baseline, o menos. **Si hay más,
parar** — el merge introdujo una regresión y hay que encontrarla antes de
commitear.

- [ ] **Step 8: Commit del merge**

```bash
git commit -m "merge: traer 18 commits de upstream rlaager/docsis

TLV84 (Diplexer Band Edge), TLV217 (eSTB), MULPIv4.0-I07, Sync/PTP de
SYNC-I03, DigitMap, ManufacturerCVCChainFile/CoSignerCVCChainFile,
corrección del decode de IpModeControl, SFAQMLatencyTarget hasta 255.

Las reglas de gramática nuevas se adaptaron a la convención de este fork:
return 0 + YYABORT en vez de exit(), con variable int intermedia para que
la comprobación <= 0 no se pierda en el unsigned short de tlv_len."
```

---

### Task 3: Auditoría de `exit()` después del merge

El merge trae código de un CLI a una librería. Un `exit()` que sobrevive mata
el intérprete de Python del consumidor. Esta tarea es una red aparte de la Task
2 porque es fácil que un `exit()` nuevo pase el merge sin conflicto: si upstream
lo agregó en una función que el fork no tocó, git lo acepta en silencio.

**Files:**
- Modify: `src/docsis_yy.y`, `src/docsis_encode.c`, `src/docsis_lex.l` (según
  lo que aparezca)

**Interfaces:**
- Consumes: el árbol mergeado de la Task 2.
- Produces: un árbol donde ningún `exit()` es alcanzable desde
  `encode_one_string()`.

- [ ] **Step 1: Listar todos los `exit()` del árbol**

```bash
grep -rn "exit *(" src/*.c src/*.y src/*.l | grep -v "^src/docsis.c:.*usage\|//" 
```

- [ ] **Step 2: Clasificar cada uno**

Para cada resultado, decidir en cuál de estos tres grupos cae:

| Grupo | Qué hacer |
|---|---|
| Alcanzable desde `encode_one_string()` (parser, lexer, encoders) | Convertir: `return 0` en constructores de TLV, `return -1` en funciones de encode, `yyterminate()` en reglas del lexer |
| Solo en `main()` / `usage()` / manejo de argumentos | Dejar. Es el CLI, y el CLI ahora es infraestructura de test. |
| En `decode_*` | Dejar por ahora. Decode no se exporta (ver spec, no-objetivos). |

- [ ] **Step 3: Convertir los del primer grupo**

Aplicar las mismas reglas que la Task 2 Step 5. Recordar la variable `int`
intermedia cuando el resultado se asigna a `tlv_len`.

- [ ] **Step 4: Verificar que no quedó ninguno alcanzable**

```bash
make clean && make
grep -rn "exit *(" src/docsis_yy.y src/docsis_encode.c src/docsis_lex.l
```

Esperado: los únicos resultados restantes en `docsis_lex.l` son los tres de la
Task 11 (`\n`/`\t` en string sin cerrar, y carácter no reconocido), que se
arreglan más adelante con su propio test. Cero resultados en `docsis_yy.y` y
`docsis_encode.c`.

- [ ] **Step 5: Verificar contra el baseline y commitear**

```bash
cd tests && MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" ./RunTests.sh --allow-failed=TRUE; cd ..
git add -A src/
git commit -m "fix: convertir los exit() que el merge trajo de upstream

Un exit() alcanzable desde encode_one_string() mata el intérprete de
Python del consumidor. Los del CLI (main, usage) se dejan: ese binario
ahora es solo arnés de test."
```

---

### Task 4: Build con libtool y header público

`configure.ac:9` ya tiene `LT_INIT` y `autogen.sh` ya corre `libtoolize`, así
que libtool está enteramente cableado y sin usar. El trabajo es casi todo en
`src/Makefile.am`.

**Files:**
- Create: `src/libdocsis.h`
- Modify: `src/Makefile.am`
- Modify: `src/libdocsis.c`

**Interfaces:**
- Consumes: el árbol de la Task 3.
- Produces: `libdocsis.la` construible e instalable, y `src/libdocsis.h` con la
  declaración de los tres símbolos públicos. El binario `docsis` pasa a
  `check_PROGRAMS` (se construye con `make check`, no se instala).

- [ ] **Step 1: Crear el header público**

`src/libdocsis.h`:

```c
/*
 * libdocsis - API pública.
 *
 * Uso: initialize() una vez, después encode_file() cuantas veces haga falta,
 * liberando cada buffer con free_encoded_file().
 *
 * MIBs: initialize() carga los MIBs vía net-snmp y NO setea MIBDIRS. Si los
 * MIBs no están en la ruta por defecto de net-snmp, exportá MIBDIRS antes de
 * llamar a initialize(), apuntando al directorio mibs/ de esta distribución.
 *
 * Concurrencia: encode_file() serializa internamente con un mutex. Es seguro
 * llamarla desde varios hilos, pero no corre en paralelo. Ver el TODO de
 * reentrancia en docsis_yy.y.
 */
#ifndef LIBDOCSIS_H
#define LIBDOCSIS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Carga los MIBs y la tabla de símbolos. Llamar una vez antes de todo.
   Devuelve 1 en éxito, 0 si los MIBs no cargaron.
   (0 significa fallo en toda esta API, sin excepciones.) */
int initialize(void);

/* Codifica `content` (largo `content_size`) en un buffer nuevo.
   En éxito devuelve el largo en bytes (> 0) y deja el buffer en *output.
   En error devuelve 0 y deja *output en NULL.
   El buffer se libera con free_encoded_file(). */
int encode_file(unsigned char *content, unsigned content_size,
                unsigned char **output,
                unsigned char *key, unsigned int keylen,
                int encode_docsis, unsigned int hash);

/* Libera un buffer devuelto por encode_file(). Acepta NULL. */
void free_encoded_file(unsigned char *buffer);

#ifdef __cplusplus
}
#endif

#endif /* LIBDOCSIS_H */
```

- [ ] **Step 2: Reescribir `src/libdocsis.c` para incluir el header**

Hoy `libdocsis.c` redeclara los prototipos a mano, que es exactamente cómo se
desincronizan un header y su implementación. Reemplazar el archivo entero por:

```c
#include "docsis.h"
#include "libdocsis.h"

int encode_file(unsigned char *file_content, unsigned file_content_size,
                unsigned char **buffer, unsigned char *key, unsigned int keylen,
                int encode_docsis, unsigned int hash)
{
	return encode_one_string(file_content, file_content_size, buffer,
	                         key, keylen, encode_docsis, hash);
}

int initialize(void)
{
	return initialize_lib();
}

void free_encoded_file(unsigned char *buffer)
{
	free(buffer);
}
```

`initialize_lib()` todavía devuelve `void` — eso se arregla en la Task 8, que
trae su propio test. Por ahora este archivo no compila: es el estado esperado
al final de este paso.

- [ ] **Step 3: Hacer que `initialize_lib()` devuelva int (mínimo para compilar)**

En `src/docsis.h`, cambiar `void initialize_lib();` por `int initialize_lib(void);`.
En `src/docsis.c`, el cuerpo:

```c
int initialize_lib(void)
{
  setup_mib_flags(0, 0);
  init_global_symtable();
  return 1;
}
```

El chequeo real de que los MIBs cargaron llega en la Task 8. Acá solo se
estabiliza el tipo para que el árbol compile.

- [ ] **Step 4: Reescribir `src/Makefile.am`**

```makefile
lib_LTLIBRARIES = libdocsis.la
libdocsis_la_SOURCES = docsis.c ethermac.c md5.c docsis_encode.c \
	docsis_decode.c docsis_snmp.c docsis_yy.y docsis_lex.l hmac_md5.c \
	sha1.c libdocsis.c
libdocsis_la_LIBADD = $(all_libraries) $(NETSNMP_LIBS) $(LEXLIB) $(LIBM)
# -export-symbols-regex: sin esto la .so exporta todo su mundo interno
# (md5_init, sha1_transform, global_symtable, line...), que colisiona con
# cualquier otra librería cargada en el mismo proceso.
libdocsis_la_LDFLAGS = -export-symbols-regex \
	'^(initialize|encode_file|free_encoded_file)$$'

include_HEADERS = libdocsis.h

# El CLI ya no es un producto: es el arnés que maneja los 142 fixtures de
# tests/. Se construye con `make check` y no se instala.
check_PROGRAMS = docsis
docsis_SOURCES = docsis_main.c
docsis_LDADD = libdocsis.la $(all_libraries) $(NETSNMP_LIBS) $(LEXLIB) $(LIBM)

AM_CPPFLAGS = $(all_includes) $(NETSNMP_CFLAGS)
noinst_HEADERS = ethermac.h md5.h docsis.h docsis_common.h docsis_decode.h \
	docsis_encode.h docsis_symtable.h docsis_snmp.h docsis_globals.h sha1.h
EXTRA_DIST = docsis_yy.h
CLEANFILES = docsis_yy.c docsis_yy.h docsis_yy.output docsis_lex.c
DISTCLEANFILES = Makefile
```

Nota el `$$` en el regex: automake pasa la línea a make, que interpreta `$`.

- [ ] **Step 5: Separar `main()` a su propio archivo**

El `Makefile.am` de arriba espera `docsis_main.c`. `main()` no puede vivir en
`docsis.c` si `docsis.c` entra en la librería: una `.so` con `main()` es legal
pero contamina el espacio de símbolos y confunde a cualquier linker.

Mover de `src/docsis.c` a `src/docsis_main.c`: las funciones `usage()` y
`main()`, tal cual están, más los `#include` que necesiten. El resto de
`docsis.c` (todo el pipeline, `add_cm_mic`, `add_cmts_mic`, `setup_mib_flags`,
`free_tlv`, `encode_one_file`, `encode_one_string`, `decode_file`) se queda.

- [ ] **Step 5b: Linkear el arnés contra el archivo estático**

Descubierto al ejecutar la tarea. El CLI usa símbolos internos
(`decode_file`, `encode_one_file`, `get_output_name`, `setup_mib_flags`,
`init_global_symtable`, `nohash`, `dialplan`) que `-export-symbols-regex`
esconde de la `.so`, así que el link falla con una docena de
`undefined reference`.

Es la tensión real del diseño: la librería debe exportar tres símbolos, el
arnés necesita ver todo. Se resuelve en una línea, porque el regex solo
afecta la tabla dinámica y no el archivo estático que libtool produce igual:

```makefile
docsis_LDFLAGS = -static
```

La alternativa era partir el árbol en una convenience library interna más una
`libdocsis.la` construida encima. Más limpio en teoría, dos targets más que
mantener, y no compra nada que esta línea no dé.

Además hay que sacarle el `static` a `setup_mib_flags()` en `docsis.c` y
declararla en `docsis.h`, y agregar `extern unsigned int dialplan;` a
`docsis_globals.h`. Dejar de ser `static` no contamina la `.so`: el regex la
esconde igual de la tabla dinámica.

- [ ] **Step 6: Verificar que construye e instala**

```bash
./autogen.sh && ./configure && make 2>&1 | tee /tmp/libtool-build.log
ls -la src/.libs/libdocsis.so*
make check 2>&1 | head -40
```

Esperado: existe `src/.libs/libdocsis.so`, y `src/docsis` se construye con
`make check` pero no con `make` solo.

- [ ] **Step 7: Verificar que la tabla de símbolos exportados es la correcta**

Este es el paso que valida el `-export-symbols-regex`, que es fácil de escribir
mal y silencioso cuando falla.

```bash
nm -D --defined-only src/.libs/libdocsis.so | grep ' T ' | awk '{print $3}' | sort
```

Esperado, exactamente estas tres líneas:

```
encode_file
free_encoded_file
initialize
```

Si aparece `md5_init`, `sha1_transform` o `global_symtable`, el regex no tomó.

- [ ] **Step 8: Commit**

```bash
git add src/libdocsis.h src/libdocsis.c src/Makefile.am src/docsis.c src/docsis_main.c src/docsis.h
git commit -m "build: producir libdocsis.la con libtool, CLI a check_PROGRAMS

configure.ac ya tenía LT_INIT sin usar. El binario docsis deja de
instalarse y pasa a ser el arnés que maneja los 142 fixtures de tests/.

-export-symbols-regex limita la tabla exportada a las tres funciones
públicas; antes la .so exportaba md5_init, sha1_transform y global_symtable,
que es un riesgo de colisión real en un proceso que también cargue OpenSSL."
```

---

### Task 5: Unificar el pipeline de encode

Hoy hay dos copias del pipeline y **ya divergieron**: `encode_one_file()` llama
`decode_main_aggregate()` antes de escribir, `encode_one_string()` no. La
consecuencia es que los 142 tests que pasan validan un camino de código que la
librería no ejecuta.

**Files:**
- Modify: `src/docsis.c` (`encode_one_file`, líneas ~501-588)

**Interfaces:**
- Consumes: `encode_one_string(content, size, &buf, key, keylen, encode_docsis, hash) -> int`
  de la Task 4.
- Produces: `encode_one_file()` reducida a lectura + delegación + escritura.
  Su firma y sus códigos de retorno (`0` éxito, `-1` parse, `-2` E/S) no cambian:
  `main()` depende de ellos.

- [ ] **Step 1: Verificar el estado de partida**

```bash
cd tests && MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" ./RunTests.sh --allow-failed=TRUE 2>&1 | tail -5; cd ..
```

Anotar el resultado. Esta tarea no puede cambiarlo: es refactor puro.

- [ ] **Step 2: Reemplazar el cuerpo de `encode_one_file()`**

```c
int encode_one_file ( char *input_file, char *output_file,
			unsigned char *key, unsigned int keylen,
			int encode_docsis, unsigned int hash)
{
  unsigned char *content = NULL, *buffer = NULL;
  unsigned int content_size = 0;
  int buflen;
  FILE *inf, *of;
  struct stat st;

  /* It's not an error to specify the input and output as "-". */
  if (!strcmp (input_file, output_file) && strcmp (input_file, "-"))
    {
      fprintf (stderr, "docsis: Error: source file is the same as destination file\n");
      return -1;
    }

  /* Leer la entrada entera a memoria. */
  if (!strcmp (input_file, "-"))
    {
      size_t cap = 65536, used = 0, got;
      content = (unsigned char *) malloc (cap);
      if (content == NULL) return -2;
      while ((got = fread (content + used, 1, cap - used, stdin)) > 0)
	{
	  used += got;
	  if (used == cap)
	    {
	      unsigned char *bigger = (unsigned char *) realloc (content, cap * 2);
	      if (bigger == NULL) { free (content); return -2; }
	      content = bigger;
	      cap *= 2;
	    }
	}
      content_size = (unsigned int) used;
    }
  else
    {
      if (stat (input_file, &st) != 0 || (inf = fopen (input_file, "rb")) == NULL)
	{
	  fprintf (stderr, "docsis: Can't open input file %s\n", input_file);
	  return -1;
	}
      content_size = (unsigned int) st.st_size;
      content = (unsigned char *) malloc (content_size);
      if (content == NULL) { fclose (inf); return -2; }
      if (fread (content, 1, content_size, inf) != content_size)
	{
	  fprintf (stderr, "docsis: Can't read input file %s\n", input_file);
	  fclose (inf); free (content); return -1;
	}
      fclose (inf);
    }

  buflen = encode_one_string (content, content_size, &buffer,
			      key, keylen, encode_docsis, hash);
  free (content);

  if (buflen <= 0 || buffer == NULL)
    {
      fprintf (stderr, "Error parsing config file %s\n", input_file);
      return -1;
    }

  /* Salida de progreso del CLI. La librería no imprime nada de esto:
     examples/Makefile.am parsea justamente esta línea. */
  fprintf (stdout, "Final content of config file:\n");
  decode_main_aggregate (buffer, (unsigned int) buflen);

  if (!strcmp (output_file, "-"))
    {
      of = stdout;
    }
  else if ((of = fopen (output_file, "wb")) == NULL)
    {
      fprintf (stderr, "docsis: error: can't open output file %s\n", output_file);
      free (buffer);
      return -2;
    }
  if (fwrite (buffer, 1, (size_t) buflen, of) != (size_t) buflen)
    {
      fprintf (stderr, "docsis: error: can't write to output file %s\n", output_file);
      if (of != stdout) fclose (of);
      free (buffer);
      return -2;
    }
  if (of != stdout) fclose (of);
  free (buffer);
  return 0;
}
```

Notar tres arreglos que vienen gratis con el refactor: el `fclose(stdout)` que
la versión vieja hacía cuando `output_file` era `-`, la fuga de `buffer` en los
caminos de error de escritura, y el `buffer` sin liberar cuando `fwrite` fallaba.

- [ ] **Step 3: Restaurar los `printf` de progreso comentados**

En el mismo archivo, descomentar los tres mensajes que el fork silenció, ahora
que viven del lado del CLI. Están en `encode_one_file()` (dialplan, NA hash, EU
hash). Ojo: las mismas líneas dentro de `encode_one_string()` **quedan
comentadas** — la librería no imprime.

- [ ] **Step 4: Verificar que `parse_config_file()` quedó sin usar**

```bash
grep -rn "parse_config_file" src/
```

Si el único resultado es su definición en `docsis_yy.y` y su declaración en
`docsis.h`, borrar ambas: el CLI ahora pasa por `parse_config_string()`, así
que hay un solo camino de parseo y no dos que puedan divergir.

- [ ] **Step 5: Verificar que nada se movió**

```bash
make clean && make && make check
cd tests && MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" ./RunTests.sh --allow-failed=TRUE 2>&1 | tail -5; cd ..
```

Esperado: **exactamente** el mismo resultado que el Step 1. Este es el punto de
la tarea: los 142 fixtures ahora prueban el código que se exporta, y el
resultado no cambió.

Además `make check` en `examples/` debería pasar de rojo a verde, porque
`"Final content of config file:"` volvió a `stdout`.

- [ ] **Step 6: Commit**

```bash
git add src/docsis.c src/docsis.h src/docsis_yy.y
git commit -m "refactor: unificar el pipeline de encode sobre encode_one_string

Había dos copias del pipeline y ya habían divergido: encode_one_file
llamaba decode_main_aggregate antes de escribir y encode_one_string no,
o sea que los 142 fixtures validaban un camino que la librería no corre.

Ahora encode_one_file lee, delega y escribe. De paso arregla el
fclose(stdout) cuando la salida es '-' y dos fugas de buffer en los
caminos de error de escritura.

Los printf de progreso vuelven, pero solo del lado del CLI: examples/
parsea 'Final content of config file:' y estaba roto desde 3b56b3e."
```

---

### Task 6: Cablear `tests/` al build y crear el arnés de la API

`tests/` no está en `SUBDIRS` ni en `AC_CONFIG_FILES`: está huérfano del build,
y `RunTests.sh` solo lo corría Travis a mano. Esta tarea lo incorpora y crea el
vehículo de TDD para las Tasks 7-11.

**Files:**
- Create: `tests/Makefile.am`
- Create: `tests/test_api.c`
- Modify: `Makefile.am` (SUBDIRS)
- Modify: `configure.ac` (AC_CONFIG_FILES)

**Interfaces:**
- Consumes: `libdocsis.la`, `src/libdocsis.h`.
- Produces: `make check` corre las dos suites. `tests/test_api.c` con un
  helper `static int encode(const char *cfg, unsigned char **out)` que las
  tareas siguientes extienden con casos nuevos.

- [ ] **Step 1: Crear `tests/Makefile.am`**

```makefile
check_PROGRAMS = test_api
test_api_SOURCES = test_api.c
test_api_CPPFLAGS = -I$(top_srcdir)/src
test_api_LDADD = $(top_builddir)/src/libdocsis.la

TESTS = test_api RunTests.sh

# RunTests.sh no exporta MIBDIRS por su cuenta; sin esto net-snmp no
# encuentra los MIBs de esta distribución y todos los OIDs fallan.
TESTS_ENVIRONMENT = \
	MIBDIRS="$(top_srcdir)/mibs:$(top_srcdir)/mibs/iana:$(top_srcdir)/mibs/ietf"

EXTRA_DIST = RunTests.sh key
# ponytail: los 426 fixtures (.txt/.cm/.conf) no están en EXTRA_DIST, así que
# `make dist` no los incluye. Listarlos a mano es inmanejable y este repo no
# hace releases con tarball. Si algún día hace falta, generar la lista con un
# include de Makefile.
```

- [ ] **Step 2: Escribir el test que falla**

`tests/test_api.c`. Arranca con un solo caso: que la API funcione de punta a
punta. Esto todavía no puede pasar porque `initialize()` devuelve `int` pero no
verifica nada — y sobre todo porque hay que confirmar que el linkeo contra la
`.so` con símbolos restringidos realmente resuelve.

```c
/* Tests de la API pública de libdocsis.
 * Sin framework: un main() con asserts. Correr con `make check`. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libdocsis.h"

static unsigned char KEY[] = "aaaaaaaaaaaaaaaa";

/* Codifica una config y devuelve el largo; *out queda con el buffer
   (o en NULL si falló). El llamador libera con free_encoded_file(). */
static int encode (const char *cfg, unsigned char **out)
{
  *out = NULL;
  return encode_file ((unsigned char *) cfg, (unsigned) strlen (cfg), out,
		      KEY, (unsigned) strlen ((char *) KEY), 1, 0);
}

static const char *MINIMAL =
  "Main {\n"
  "  DownstreamFrequency 123000000;\n"
  "  UpstreamChannelId 1;\n"
  "  NetworkAccess 1;\n"
  "}\n";

static void test_minimal_config_encodes (void)
{
  unsigned char *buf = NULL;
  int len = encode (MINIMAL, &buf);

  assert (len > 0);
  assert (buf != NULL);
  free_encoded_file (buf);
  printf ("ok - una config mínima codifica (%d bytes)\n", len);
}

int main (void)
{
  if (!initialize ())
    {
      fprintf (stderr, "initialize() falló: ¿están los MIBs en MIBDIRS?\n");
      return 1;
    }

  test_minimal_config_encodes ();

  printf ("todos los tests de la API pasaron\n");
  return 0;
}
```

- [ ] **Step 3: Cablear `tests/` al build**

En `Makefile.am`, cambiar la línea de `SUBDIRS`:

```makefile
SUBDIRS = src doc examples mibs tests
```

`tests` va último: depende de `src` para `libdocsis.la`.

En `configure.ac:214`, agregar `tests/Makefile` a la lista:

```
AC_CONFIG_FILES([build_setup.nsi Makefile src/Makefile doc/Makefile examples/Makefile mibs/Makefile tests/Makefile])
```

- [ ] **Step 4: Correr el test y verificar que pasa**

```bash
./autogen.sh && ./configure && make && make check 2>&1 | tee /tmp/check.log
grep -E "^(ok|PASS|FAIL)" /tmp/check.log
```

Esperado: `test_api` pasa, `RunTests.sh` pasa (o falla exactamente igual que el
baseline), `examples/` pasa.

Si `test_api` falla al linkear con "undefined reference to `initialize`", el
`-export-symbols-regex` de la Task 4 está mal escrito.

- [ ] **Step 5: Commit**

```bash
git add tests/Makefile.am tests/test_api.c Makefile.am configure.ac
git commit -m "test: cablear tests/ al build y agregar el arnés de la API

tests/ no estaba en SUBDIRS ni en AC_CONFIG_FILES: RunTests.sh solo lo
corría Travis a mano. Ahora make check corre las dos suites.

test_api.c es el vehículo para los tests de los caminos de error, que
hoy tienen cobertura cero: los 142 fixtures solo prueban entradas
válidas, justo lo contrario de lo que el fork reescribió."
```

---

### Task 7: `*output = NULL` en todo camino de error

Hoy, en el camino de error, `*output` nunca se asigna: un llamador que no
chequee el retorno lee memoria no inicializada. Es el bug de la API con peor
relación entre gravedad y esfuerzo de arreglo.

**Files:**
- Modify: `tests/test_api.c`
- Modify: `src/docsis.c` (`encode_one_string`)

**Interfaces:**
- Consumes: el helper `encode()` de la Task 6.
- Produces: garantía de que `*output == NULL` siempre que el retorno sea `0`.

- [ ] **Step 1: Escribir el test que falla**

Agregar a `tests/test_api.c`, antes de `main()`:

```c
static void test_invalid_value_fails_cleanly (void)
{
  /* NetworkAccess acepta 0 o 1; 99 está fuera de rango. */
  static const char *BAD =
    "Main {\n"
    "  DownstreamFrequency 123000000;\n"
    "  UpstreamChannelId 1;\n"
    "  NetworkAccess 99;\n"
    "}\n";
  unsigned char *buf = (unsigned char *) 0xDEADBEEF;  /* veneno */
  int len;

  buf = NULL;
  len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);   /* el contrato: en error, *output queda en NULL */
  printf ("ok - un valor fuera de rango falla sin dejar el puntero colgado\n");
}

static void test_unknown_symbol_fails_cleanly (void)
{
  static const char *BAD = "Main {\n  NoExisteEsteTLV 1;\n}\n";
  unsigned char *buf = NULL;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - un símbolo desconocido falla sin dejar el puntero colgado\n");
}

static void test_unclosed_brace_fails_cleanly (void)
{
  static const char *BAD = "Main {\n  NetworkAccess 1;\n";
  unsigned char *buf = NULL;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - una llave sin cerrar falla sin dejar el puntero colgado\n");
}

static void test_empty_input_fails_cleanly (void)
{
  unsigned char *buf = NULL;
  int len = encode ("", &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - una entrada vacía falla sin crashear\n");
}
```

Y llamarlas desde `main()`, después de `test_minimal_config_encodes()`.

- [ ] **Step 2: Correr y verificar que falla**

```bash
make check 2>&1 | tail -30
```

Esperado: `test_api` aborta en un `assert`. El caso más probable de fallo es
`test_invalid_value_fails_cleanly`, porque `encode_one_string()` hace
`return 0` sin haber tocado `*bufferp`, así que `buf` conserva lo que tenía.
Anotar **cuál** assert falló: informa el arreglo.

- [ ] **Step 3: Arreglar `encode_one_string()`**

En `src/docsis.c`, al principio de la función, antes de cualquier otra cosa:

```c
  if (bufferp == NULL)
    return 0;
  *bufferp = NULL;   /* el contrato: en error, el llamador ve NULL */
```

Y en el camino de error de parseo, liberar el árbol antes de salir (hoy se
pierde cuando `global_tlvtree_head` quedó a medio construir):

```c
  if (parse_result || global_tlvtree_head == NULL)
    {
      fprintf (stderr, "Error parsing config file\n");
      free_tlv (global_tlvtree_head);
      return 0;
    }
```

Y en el camino de éxito, verificar el `malloc`, que hoy no se chequea:

```c
  buflen = tlvtreelen (global_tlvtree_head);
  *bufferp = (unsigned char *) malloc (buflen + 255 + 8192);
  if (*bufferp == NULL)
    {
      free_tlv (global_tlvtree_head);
      return 0;
    }
  buffer = *bufferp;
```

- [ ] **Step 4: Correr y verificar que pasa**

```bash
make check 2>&1 | grep -E "^ok|FAIL"
```

Esperado: las cinco líneas `ok`, y ningún FAIL.

- [ ] **Step 5: Commit**

```bash
git add tests/test_api.c src/docsis.c
git commit -m "fix: garantizar *output = NULL en todo camino de error

encode_one_string devolvía 0 sin tocar el puntero de salida, así que un
llamador que no chequeara el retorno leía memoria sin inicializar.

De paso: liberar el árbol de TLVs cuando el parseo falla a medio camino,
y chequear el malloc del buffer de salida, que no se chequeaba."
```

---

### Task 8: `initialize()` reporta si los MIBs no cargaron

`setup_mib_flags(0, 0)` pasa `custom_mibs = NULL`, así que `initialize()`
**nunca setea `MIBDIRS`**: la librería depende de una variable de entorno que
nadie documenta. Y como `netsnmp_init_mib()` devuelve `void`, hoy no hay forma
de saber si los MIBs cargaron hasta que un OID falla en medio de un encode.

**Files:**
- Modify: `tests/test_api.c`
- Modify: `src/docsis.c` (`initialize_lib`)

**Interfaces:**
- Consumes: `initialize_lib()` de la Task 4.
- Produces: `initialize()` devuelve `0` cuando el árbol de MIBs quedó vacío.

- [ ] **Step 1: Escribir el test que falla**

Agregar a `tests/test_api.c`:

```c
static void test_snmp_oid_resolves (void)
{
  /* Si los MIBs no cargaron, este SnmpMibObject no resuelve y el encode
     falla. Es la prueba indirecta de que initialize() hizo su trabajo. */
  static const char *WITH_OID =
    "Main {\n"
    "  DownstreamFrequency 123000000;\n"
    "  UpstreamChannelId 1;\n"
    "  NetworkAccess 1;\n"
    "  SnmpMibObject sysContact.0 String \"test\";\n"
    "}\n";
  unsigned char *buf = NULL;
  int len = encode (WITH_OID, &buf);

  assert (len > 0);
  assert (buf != NULL);
  free_encoded_file (buf);
  printf ("ok - un OID simbólico resuelve, o sea que los MIBs cargaron\n");
}
```

Llamarla desde `main()`.

- [ ] **Step 2: Correr y verificar**

```bash
make check 2>&1 | grep -E "^ok|FAIL|MIBs"
```

Si pasa directo, los MIBs ya cargan bien vía el `TESTS_ENVIRONMENT` de la Task
6 — lo cual confirma la dependencia de `MIBDIRS` que estamos documentando.
Verificar entonces que el test **detecta** el problema cuando no están:

```bash
cd tests && MIBDIRS=/nonexistent ./test_api; echo "exit=$?"; cd ..
```

Esperado: falla. Si pasa igual, el test no sirve y hay que endurecerlo.

- [ ] **Step 3: Hacer que `initialize_lib()` verifique**

En `src/docsis.c`:

```c
int initialize_lib (void)
{
  setup_mib_flags (0, 0);
  if (get_tree_head () == NULL)
    {
      fprintf (stderr,
	       "libdocsis: net-snmp no cargó ningún MIB. Exportá MIBDIRS "
	       "apuntando al directorio mibs/ de esta distribución.\n");
      return 0;
    }
  return init_global_symtable ();
}
```

`get_tree_head()` viene de `<net-snmp/library/parse.h>`, que ya entra por los
includes de net-snmp de `docsis.c`. Si no compila, agregar el include
explícito.

- [ ] **Step 4: Correr y verificar**

```bash
make check 2>&1 | grep -E "^ok|FAIL"
cd tests && MIBDIRS=/nonexistent ./test_api 2>&1 | head -3; echo "exit=$?"; cd ..
```

Esperado: con MIBs, todo `ok`. Sin MIBs, el mensaje explicativo y exit distinto
de cero — no un fallo críptico a mitad de un encode.

- [ ] **Step 5: Commit**

```bash
git add tests/test_api.c src/docsis.c
git commit -m "fix: initialize() reporta cuando los MIBs no cargaron

setup_mib_flags(0,0) nunca setea MIBDIRS, así que la librería depende de
una variable de entorno que no estaba documentada en ningún lado. Como
netsnmp_init_mib() devuelve void, el fallo aparecía recién cuando un OID
no resolvía en medio de un encode.

Ahora se chequea get_tree_head() y se falla temprano con un mensaje que
dice qué hacer."
```

---

### Task 9: Serializar `encode_file()` con un mutex

`ctypes` **libera el GIL** durante la llamada, así que dos hilos de Python
llamando `encode_file()` a la vez corren de verdad en paralelo. Y
`_my_tlvtree_head`, `line` y `strbuf[2048]` siguen siendo globales: se pisan y
producen basura silenciosamente, que es el peor modo de falla posible para algo
que emite binarios firmados.

**Files:**
- Modify: `configure.ac`
- Modify: `src/libdocsis.c`
- Modify: `src/docsis_yy.y` (comentario TODO)
- Modify: `tests/test_api.c`
- Modify: `tests/Makefile.am`

**Interfaces:**
- Consumes: `encode_file()` de la Task 4.
- Produces: `encode_file()` serializada. La API no cambia.

- [ ] **Step 1: Escribir el test que falla**

Agregar a `tests/test_api.c` (necesita `#include <pthread.h>` arriba):

```c
#define NTHREADS 8
#define NITERS   25

/* Largo de referencia de MINIMAL, fijado antes de largar los hilos. */
static int g_reflen;

static void *hammer (void *unused)
{
  int i;
  (void) unused;
  for (i = 0; i < NITERS; i++)
    {
      unsigned char *buf = NULL;
      int len = encode (MINIMAL, &buf);
      /* Todas las iteraciones codifican la misma entrada: si el estado
	 global se pisa, alguna devuelve un largo distinto o falla. */
      assert (buf != NULL);
      assert (len == g_reflen);
      free_encoded_file (buf);
    }
  return NULL;
}

static void test_concurrent_calls_do_not_corrupt (void)
{
  pthread_t th[NTHREADS];
  unsigned char *ref = NULL;
  int i;

  g_reflen = encode (MINIMAL, &ref);
  assert (g_reflen > 0);
  free_encoded_file (ref);

  for (i = 0; i < NTHREADS; i++)
    assert (pthread_create (&th[i], NULL, hammer, NULL) == 0);
  for (i = 0; i < NTHREADS; i++)
    assert (pthread_join (th[i], NULL) == 0);

  printf ("ok - %d hilos x %d encodes sin corrupción\n", NTHREADS, NITERS);
}

/* El hermano determinista del test de arriba. La versión con hilos es una
   carrera y puede no disparar; ésta falla siempre que el estado global se
   filtre de una llamada a la siguiente. El spec lo pide explícitamente. */
static void test_repeated_calls_are_identical (void)
{
  unsigned char *a = NULL, *b = NULL;
  int la, lb;

  la = encode (MINIMAL, &a);
  lb = encode (MINIMAL, &b);

  assert (la > 0);
  assert (la == lb);
  assert (memcmp (a, b, (size_t) la) == 0);

  free_encoded_file (a);
  free_encoded_file (b);
  printf ("ok - dos llamadas seguidas producen bytes idénticos\n");
}
```

Llamar ambas desde `main()`.

- [ ] **Step 2: Correr y verificar que falla**

```bash
make check 2>&1 | tail -20
```

Esperado: aborta en un assert, o segfaultea. Si pasa a la primera, correrlo
unas cuantas veces — es una carrera, no es determinista:

```bash
cd tests && for i in $(seq 1 20); do ./test_api >/dev/null 2>&1 || echo "falló en la corrida $i"; done; cd ..
```

Si nunca falla, subir `NTHREADS` y `NITERS` hasta que lo haga. Un test de
concurrencia que nunca ve la carrera no prueba nada.

- [ ] **Step 3: Agregar pthread al build**

En `configure.ac`, después de `AC_PROG_LEX`:

```
AX_PTHREAD([], [AC_MSG_ERROR([se necesita soporte de pthreads])])
```

Si `AX_PTHREAD` no está disponible (viene de autoconf-archive), usar la
alternativa portable sin dependencias:

```
AC_CHECK_LIB([pthread], [pthread_mutex_lock],
             [PTHREAD_LIBS=-lpthread],
             [AC_MSG_ERROR([se necesita soporte de pthreads])])
AC_SUBST(PTHREAD_LIBS)
```

En `src/Makefile.am`, agregar `$(PTHREAD_LIBS)` a `libdocsis_la_LIBADD`.
En `tests/Makefile.am`, agregar `$(PTHREAD_LIBS)` a `test_api_LDADD`.

- [ ] **Step 4: Poner el candado**

En `src/libdocsis.c`:

```c
#include <pthread.h>

/* ponytail: candado global sobre todo el encode. El parser todavía tiene
 * estado global (_my_tlvtree_head en docsis_yy.y, line y strbuf en
 * docsis_lex.l) y ctypes libera el GIL, así que sin esto dos hilos de
 * Python se pisan en silencio. Serializa: correcto siempre, lento solo si
 * alguien realmente paraleliza. Camino de mejora: mover ese estado a un
 * struct de contexto que viaje por %parse-param junto al yyscan_t, y
 * sacar el candado. Es un refactor que toca cada regla de la gramática,
 * así que conviene hacerlo justo después de un merge con upstream y no
 * justo antes. */
static pthread_mutex_t encode_lock = PTHREAD_MUTEX_INITIALIZER;

int encode_file (unsigned char *file_content, unsigned file_content_size,
		 unsigned char **buffer, unsigned char *key, unsigned int keylen,
		 int encode_docsis, unsigned int hash)
{
	int rv;

	pthread_mutex_lock (&encode_lock);
	rv = encode_one_string (file_content, file_content_size, buffer,
				key, keylen, encode_docsis, hash);
	pthread_mutex_unlock (&encode_lock);
	return rv;
}
```

- [ ] **Step 5: Poner el TODO donde vive el problema**

En `src/docsis_yy.y`, sobre la declaración de `_my_tlvtree_head`:

```c
/* ponytail: estado global del parser. encode_file() lo protege con un mutex
 * (ver src/libdocsis.c). Para reentrancia real esto tiene que vivir en un
 * struct de contexto pasado por %parse-param. */
struct tlv *_my_tlvtree_head;
```

- [ ] **Step 6: Correr y verificar que pasa**

```bash
./autogen.sh && ./configure && make && make check 2>&1 | grep -E "^ok|FAIL"
cd tests && for i in $(seq 1 20); do ./test_api >/dev/null 2>&1 || echo "falló en la corrida $i"; done; echo "20 corridas hechas"; cd ..
```

Esperado: todas las corridas pasan.

- [ ] **Step 7: Commit**

```bash
git add configure.ac src/Makefile.am src/libdocsis.c src/docsis_yy.y tests/test_api.c tests/Makefile.am
git commit -m "fix: serializar encode_file() con un mutex

ctypes libera el GIL durante la llamada, así que dos hilos de Python
corren encode_file() realmente en paralelo sobre un parser que todavía
tiene estado global (_my_tlvtree_head, line, strbuf). El resultado era
basura silenciosa, que es el peor modo de falla para algo que emite
binarios firmados.

Candado global en vez de refactor: el refactor toca cada regla de la
gramática y maximizaría los conflictos con upstream. Queda el TODO
anotado donde vive el problema."
```

---

### Task 10: Resetear `line` entre parseos

`line` arranca en 1 y nunca vuelve. A partir del segundo `encode_file()` de un
proceso, los números de línea de los mensajes de error son acumulados: un error
en la línea 3 del segundo archivo se reporta como línea 15. Para alguien
depurando una config desde Python, eso es peor que no tener número.

**Files:**
- Modify: `tests/test_api.c`
- Modify: `src/docsis_yy.y` (`parse_config_string`)

**Interfaces:**
- Consumes: `parse_config_string()` de la Task 2.
- Produces: `line` reseteada en cada parseo.

- [ ] **Step 1: Escribir el test que falla**

El mensaje de error va a `stderr`, así que el test lo captura redirigiendo a
un archivo temporal.

```c
static void test_line_numbers_reset_between_calls (void)
{
  /* El error está en la línea 4 de ambas entradas. Si `line` no se
     resetea, el segundo encode lo reporta en una línea mayor. */
  static const char *BAD =
    "Main {\n"
    "  DownstreamFrequency 123000000;\n"
    "  UpstreamChannelId 1;\n"
    "  NetworkAccess 99;\n"
    "}\n";
  char first[256] = {0}, second[256] = {0};
  unsigned char *buf = NULL;
  FILE *cap;
  int saved;

  /* primera pasada */
  fflush (stderr);
  saved = dup (fileno (stderr));
  cap = freopen ("stderr1.tmp", "w", stderr);
  assert (cap != NULL);
  encode (BAD, &buf);
  fflush (stderr);
  dup2 (saved, fileno (stderr));

  /* segunda pasada, entrada idéntica */
  cap = freopen ("stderr2.tmp", "w", stderr);
  assert (cap != NULL);
  encode (BAD, &buf);
  fflush (stderr);
  dup2 (saved, fileno (stderr));
  close (saved);

  { FILE *f = fopen ("stderr1.tmp", "r"); assert (f); fread (first, 1, sizeof (first) - 1, f); fclose (f); }
  { FILE *f = fopen ("stderr2.tmp", "r"); assert (f); fread (second, 1, sizeof (second) - 1, f); fclose (f); }
  remove ("stderr1.tmp"); remove ("stderr2.tmp");

  /* Dos entradas idénticas tienen que producir el mismo mensaje. */
  assert (strcmp (first, second) == 0);
  printf ("ok - los números de línea se resetean entre llamadas\n");
}
```

Necesita `#include <unistd.h>` arriba, por `dup`/`dup2`/`close`.
Llamarla desde `main()`.

- [ ] **Step 2: Correr y verificar que falla**

```bash
make check 2>&1 | tail -20
```

Esperado: el assert de `strcmp` falla — el segundo mensaje tiene un número de
línea mayor.

- [ ] **Step 3: Arreglar**

En `src/docsis_yy.y`, en `parse_config_string()`, antes de `yylex_init`:

```c
  line = 1;   /* `line` es global y no se resetea sola */
```

Hacer lo mismo en `parse_config_file()` si todavía existe (la Task 5 Step 4
puede haberla borrado).

- [ ] **Step 4: Correr y verificar que pasa**

```bash
make check 2>&1 | grep -E "^ok|FAIL"
```

- [ ] **Step 5: Commit**

```bash
git add tests/test_api.c src/docsis_yy.y
git commit -m "fix: resetear el contador de líneas entre parseos

line es global y arrancaba en 1 una sola vez por proceso, así que desde
el segundo encode_file() los números de línea de los errores venían
acumulados. Desde Python, donde el proceso vive indefinidamente, eso
significaba números crecientes sin relación con el archivo."
```

---

### Task 11: Limpieza final

Cambios chicos e independientes, agrupados en una tarea porque ninguno amerita
su propio ciclo de revisión y todos se verifican con la misma corrida.

**Files:**
- Modify: `src/docsis.c`, `src/docsis_globals.h` (tamaño del symtable)
- Modify: `src/docsis_encode.c` (restos de depuración)
- Modify: `src/docsis_lex.l` (`exit()` remanentes)

**Interfaces:**
- Consumes: el árbol de la Task 10.
- Produces: nada nuevo en la API.

- [ ] **Step 1: Arreglar el tamaño de `global_symtable`**

El `125576` es `sizeof(symbol_type) * NUM_IDENTIFIERS` = 88 × 1427, o sea el
tamaño en **bytes** usado como **cantidad de elementos**: ~11 MB de BSS donde
alcanzan 125 KB. Salió del `fprintf(stderr, "LALA %lu", ...)` que quedó
comentado dos líneas arriba en `docsis.c`.

Primero confirmar el valor actual, porque el merge de la Task 2 pudo cambiar
`NUM_IDENTIFIERS`:

```bash
grep -n "NUM_IDENTIFIERS" src/docsis_common.h
```

En `src/docsis.c`: `symbol_type global_symtable[125576];` →
`symbol_type global_symtable[NUM_IDENTIFIERS];`

En `src/docsis_globals.h`: `extern symbol_type global_symtable[125576];` →
`extern symbol_type global_symtable[NUM_IDENTIFIERS];`

En `src/docsis_lex.l`: `extern symbol_type global_symtable[125576];` →
`extern symbol_type global_symtable[NUM_IDENTIFIERS];`

Borrar también el `fprintf` comentado de `init_global_symtable()` y el bloque
de `malloc` comentado: ya no informan nada.

- [ ] **Step 2: Verificar la reducción de memoria**

```bash
make clean && make
size -A src/.libs/libdocsis_la-docsis.o | grep -i bss
```

Esperado: el `.bss` baja de ~11 MB a ~125 KB. Es la verificación de que el
cambio hizo lo que decía.

- [ ] **Step 3: Sacar los restos de depuración en español**

```bash
grep -n "linea " src/docsis_encode.c
```

Tres resultados (`"linea 61"`, `"linea 106"`, `"linea 138"`), insertados dentro
de mensajes de error que ve el usuario final. Sacar solo ese fragmento y dejar
el resto del mensaje intacto:

```c
/* antes */
fprintf(stderr, "docsis: at line %d, %s value %d linea 61 out of range %hd-%hd\n ", ...);
/* después */
fprintf(stderr, "docsis: at line %d, %s value %d out of range %hd-%hd\n ", ...);
```

Verificar de paso que `encode_uchar` devuelve `-1` y no `-15` como sus vecinas
— hoy es la única que difiere, sin motivo aparente. Unificar en `-1`, que es lo
que el resto del archivo usa para "no pude codificar esto".

- [ ] **Step 4: Convertir los `exit()` que quedan en el lexer**

`src/docsis_lex.l` todavía tiene tres. Son alcanzables desde la librería: un
`.cfg` con una comilla sin cerrar voltea el proceso Python.

```c
/* antes */
<STRING>\n	{ fprintf(stderr, "line %d: \\n not allowed in string, unmatched \" ?\n",line); exit(-1); }
<STRING>\t	{ fprintf(stderr, "line %d: \\t not allowed in string, unmatched \" ?\n",line); exit(-1); }
.	 { fprintf(stderr, "Unrecognized char \"%c\" at line %d\n",*yytext,line); exit(-1); }

/* después: misma línea, yyterminate() en vez de exit() */
<STRING>\n	{ fprintf(stderr, "line %d: \\n not allowed in string, unmatched \" ?\n",line); yyterminate(); }
<STRING>\t	{ fprintf(stderr, "line %d: \\t not allowed in string, unmatched \" ?\n",line); yyterminate(); }
.	 { fprintf(stderr, "Unrecognized char \"%c\" at line %d\n",*yytext,line); yyterminate(); }
```

- [ ] **Step 5: Test para el string sin cerrar**

Agregar a `tests/test_api.c`:

```c
static void test_unterminated_string_fails_cleanly (void)
{
  /* Una comilla sin cerrar: el lexer encuentra \n dentro del estado STRING.
     Antes esto llamaba exit(-1) y mataba el proceso del llamador. */
  static const char *BAD =
    "Main {\n"
    "  SnmpMibObject sysContact.0 String \"sin cerrar;\n"
    "}\n";
  unsigned char *buf = NULL;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - un string sin cerrar falla sin matar el proceso\n");
}
```

Llamarla desde `main()`. El test pasa solo si el proceso **sobrevive**: si
`exit()` sigue ahí, `make check` reporta el test como fallido porque el binario
termina antes de imprimir la línea final.

- [ ] **Step 6: Correr todo y verificar**

```bash
./autogen.sh && ./configure && make && make check 2>&1 | grep -E "^ok|FAIL|PASS"
cd tests && MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" ./RunTests.sh --allow-failed=TRUE 2>&1 | tail -3; cd ..
```

Esperado: todos los `ok`, las dos suites verdes, y `RunTests.sh` igual o mejor
que el baseline de la Task 1.

- [ ] **Step 7: Commit**

```bash
git add src/docsis.c src/docsis_globals.h src/docsis_lex.l src/docsis_encode.c tests/test_api.c
git commit -m "fix: tamaño del symtable, restos de depuración y exit() del lexer

global_symtable[125576] usaba el tamaño en bytes como cantidad de
elementos (88 x 1427): ~11 MB de BSS donde alcanzan 125 KB.

Los mensajes de error traían 'linea 61', 'linea 106' y 'linea 138'
pegados adentro, restos de una sesión de depuración.

Los tres exit() que quedaban en el lexer mataban el proceso del llamador
cuando un .cfg tenía una comilla sin cerrar."
```

---

### Task 12: README

El README actual documenta un `gcc -shared` manual de 400 caracteres con rutas
hardcodeadas (`perl/5.22`, `x86_64-linux-gnu`) y un paso de editar el Makefile
a mano. Nada de eso sigue siendo cierto.

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: todo lo anterior.
- Produces: instrucciones que funcionan.

- [ ] **Step 1: Reescribir `README.md`**

Tiene que cubrir, en este orden:

1. **Qué es**: fork de rlaager/docsis que expone la codificación de archivos
   de configuración DOCSIS como librería compartida, para consumir desde Python
   con `ctypes`. Enlace a upstream.
2. **Dependencias**: `autoconf automake libtool flex bison libsnmp-dev gcc make`
   (nombres de paquete de Debian/Ubuntu).
3. **Build**: `./autogen.sh && ./configure && make && sudo make install`.
   Nada más. Sin comandos `gcc` a mano.
4. **MIBs**: que `initialize()` no setea `MIBDIRS` y que hay que exportarla
   apuntando a `mibs/`, `mibs/iana` y `mibs/ietf` de esta distribución. Es el
   modo de falla número uno en runtime y la única dependencia no obvia.
5. **La API**: las tres funciones, con la convención de `0` = fallo en todas.
6. **Ejemplo de `ctypes`**, exactamente este, que es el que se verifica en el
   Step 2:

```python
import ctypes

lib = ctypes.CDLL("libdocsis.so")

lib.initialize.restype = ctypes.c_int
lib.initialize.argtypes = []

lib.encode_file.restype = ctypes.c_int
lib.encode_file.argtypes = [
    ctypes.c_char_p,                    # content
    ctypes.c_uint,                      # content_size
    ctypes.POINTER(ctypes.c_char_p),    # output
    ctypes.c_char_p,                    # key
    ctypes.c_uint,                      # keylen
    ctypes.c_int,                       # encode_docsis
    ctypes.c_uint,                      # hash
]

lib.free_encoded_file.restype = None
lib.free_encoded_file.argtypes = [ctypes.c_char_p]

if not lib.initialize():
    raise RuntimeError("no se pudieron cargar los MIBs: revisá MIBDIRS")

config = b'''Main {
  DownstreamFrequency 123000000;
  UpstreamChannelId 1;
  NetworkAccess 1;
}
'''
key = b"aaaaaaaaaaaaaaaa"

out = ctypes.c_char_p()
length = lib.encode_file(config, len(config), ctypes.byref(out),
                         key, len(key), 1, 0)
if length == 0:
    raise RuntimeError("falló la codificación; el detalle salió por stderr")

try:
    data = ctypes.string_at(out, length)     # copia los bytes a Python
finally:
    lib.free_encoded_file(out)

print(f"{len(data)} bytes: {data[:16].hex()}")
```

   Advertir en el texto que `ctypes.string_at(out, length)` es obligatorio y
   no `out.value`: el buffer es binario y contiene ceros, así que tratarlo
   como cadena terminada en NUL lo trunca en el primer `0x00`.
7. **Concurrencia**: que `encode_file()` serializa internamente; segura desde
   varios hilos pero sin paralelismo real.
8. **Tests**: `make check`.
9. **Qué cambia respecto de upstream**: que el binario `docsis` ya no se
   instala (es arnés de test), que decode no está expuesto, y que no hay
   soporte de Windows porque `fmemopen()` es POSIX.

- [ ] **Step 2: Verificar el ejemplo de Python**

El ejemplo del README no es decorativo: si está mal, es el primer tropiezo de
cualquiera que use esto. Correrlo de verdad:

```bash
cd /tmp && cat > readme_check.py <<'EOF'
# pegar acá, textualmente, el ejemplo que quedó en el README
EOF
MIBDIRS="/home/fede/Documents/libdocsis/mibs:/home/fede/Documents/libdocsis/mibs/iana:/home/fede/Documents/libdocsis/mibs/ietf" \
  LD_LIBRARY_PATH=/home/fede/Documents/libdocsis/src/.libs \
  python3 readme_check.py
```

Esperado: imprime el largo y los primeros bytes, sin excepciones.

- [ ] **Step 3: Verificar el build desde cero**

```bash
cd /tmp && rm -rf readme-check && git clone /home/fede/Documents/libdocsis readme-check
cd readme-check && git checkout lib-consolidation
./autogen.sh && ./configure && make && make check
```

Esperado: funciona siguiendo solo lo que dice el README, en un árbol limpio sin
artefactos de build previos.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: reescribir el README para el build con autotools

El anterior pedía editar src/Makefile a mano y pegar un gcc -shared con
rutas hardcodeadas de perl 5.22 y x86_64-linux-gnu. Ahora es
./autogen.sh && ./configure && make.

Documenta la dependencia de MIBDIRS, que no estaba en ningún lado y es
el modo de falla más común en runtime."
```

---

## Verificación final

Después de la Task 12, con el árbol limpio:

```bash
make distclean 2>/dev/null; ./autogen.sh && ./configure && make && make check
nm -D --defined-only src/.libs/libdocsis.so | grep ' T ' | awk '{print $3}' | sort
cd tests && MIBDIRS="../mibs:../mibs/iana:../mibs/ietf" ./RunTests.sh 2>&1 | tail -3
```

Los cuatro objetivos, uno por uno:

| Objetivo | Cómo se verifica |
|---|---|
| Tests | `make check` corre `test_api` (12 casos) + `RunTests.sh` (142) + `examples` (24) |
| Bugs arreglados | Task 1b (regresión de TLV longitud cero) + Tasks 7-11, cada una con su test que falló primero |
| Upstream al día | `git log --oneline HEAD..upstream/master` vacío |
| Build ordenado | `make install` deja la `.so` y el header; el README no tiene ningún `gcc` |
