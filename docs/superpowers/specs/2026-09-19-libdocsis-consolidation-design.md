# libdocsis: consolidación en librería

**Fecha:** 2026-09-19
**Estado:** propuesto, pendiente de revisión

## Contexto

Este repositorio es un fork de [rlaager/docsis](https://github.com/rlaager/docsis)
divergido en `d7c9643`, con 7 commits propios cuyo objetivo fue convertir un CLI
en una librería consumible desde Python vía `ctypes`. El fork logra eso, pero
quedó a medio camino: la librería no está en el sistema de build, el CLI y la
librería duplican el pipeline de encode, varias correcciones tienen restos de
depuración, y el árbol está 18 commits detrás de upstream.

## Objetivo

Dejar el repositorio en un estado donde:

1. El producto es una librería compartida instalable, no un CLI.
2. Un cambio que rompa la codificación falla un test antes de llegar a producción.
3. Traer cambios de upstream sea una operación de rutina y no una arqueología.

## No-objetivos

- **Exponer decode.** `docsis_decode.c` se queda en el árbol porque los tests lo
  usan para verificar round-trip, pero no se exporta. Refactorearlo para escribir
  a un buffer en vez de a `stdout` es más trabajo que todo el resto junto, y no
  hay un consumidor que lo pida.
- **Reentrancia completa.** Se resuelve con un candado (ver Fase 4) y queda un
  TODO registrado. El refactor real toca cada regla de la gramática y cada acción
  del lexer, o sea que maximiza los conflictos con upstream. Es un proyecto aparte.
- **Portabilidad a Windows.** `parse_config_string()` usa `fmemopen()`, que es
  POSIX y no existe en MSVC. El fork ya tomó esa decisión implícitamente; acá se
  hace explícita. `build_setup.nsi.in` (instalador NSIS de upstream) queda sin
  mantener.
- **Refactor no relacionado.** Las fugas de `TSAVE` en el lexer y el manejo de
  errores del CLI son de upstream y se dejan como están salvo que bloqueen algo.

## Decisiones tomadas

| Tema | Decisión |
|---|---|
| Producto | Solo la librería. El CLI deja de instalarse. |
| API | Solo encode. Se corrige la convención de retorno y se agrega liberación de memoria. |
| Concurrencia | Candado a nivel de la llamada + TODO documentado para reentrancia real. |
| Build | Autotools + libtool (`LT_INIT` ya está en `configure.ac:9`, sin usar). |

## Arquitectura

### El CLI como infraestructura de test

Los 142 fixtures de `tests/` (cada uno un `.txt` de entrada, su `.cm` binario
esperado y su `.conf` decodificado esperado) son el activo más valioso del
repositorio: son la única definición ejecutable de "codificar correctamente".
El driver que los maneja es el binario `docsis`.

Por lo tanto el CLI no se borra: pasa de `bin_PROGRAMS` a `check_PROGRAMS`.
Deja de ser un producto y pasa a ser un arnés. Esto no cuesta nada y preserva
las dos suites existentes.

### Deduplicación del pipeline de encode

Hoy existen dos copias casi idénticas del pipeline, y **ya divergieron**:
`encode_one_file()` llama `decode_main_aggregate()` antes de escribir,
`encode_one_string()` no. La consecuencia es que los 142 tests que hoy pasan
validan un camino de código que la librería no ejecuta.

```
ANTES                                  DESPUÉS
encode_one_file()   ~80 líneas         encode_one_string()  el pipeline real
encode_one_string() ~80 líneas         encode_one_file()    read → encode_one_string → write
  (copia divergente)                                        (~15 líneas, solo CLI)
```

Con esto, correr la suite existente a través del CLI ejercita transitivamente
el código que se exporta.

### Separación de salida: el CLI habla, la librería calla

`encode_one_file()` es quien imprime `"Final content of config file:"`, y
`examples/Makefile.am:42` hace `sed -n '/^Final content/,$p'` sobre esa salida.
El fork comentó ese `printf`, lo que dejó `make check` de `examples/` roto.

Como `encode_one_file()` queda del lado del CLI, restaurar ahí los `printf` de
progreso recupera esa suite sin ensuciar la librería. Los dos contratos conviven
sin necesidad de un flag de verbosidad.

`encode_one_string()` no escribe a `stdout` nunca. Los errores van a `stderr`.

### Superficie pública

Se agrega `src/libdocsis.h`, instalado vía `include_HEADERS`. Hoy no hay header
público: `docsis.h` es `noinst_HEADERS` y arrastra todo el mundo interno.

```c
/* Carga los MIBs y la tabla de símbolos. Llamar una vez antes de todo.
   Devuelve 1 en éxito, 0 si los MIBs no cargan.
   (0 significa fallo en toda la API, sin excepciones.) */
int  docsis_init(void);

/* Codifica `content` (largo `content_size`) en un buffer nuevo.
   En éxito: devuelve el largo en bytes (> 0) y *output apunta al buffer.
   En error: devuelve 0 y *output queda en NULL.
   El buffer se libera con docsis_free().
   NO es thread-safe por debajo; la llamada está serializada internamente. */
int  docsis_encode(const unsigned char *content, unsigned content_size,
                   unsigned char **output,
                   const unsigned char *key, unsigned keylen,
                   int encode_docsis, unsigned hash);

/* Libera un buffer devuelto por docsis_encode(). */
void docsis_free(unsigned char *buffer);
```

**Cambio incompatible a revisar:** los nombres actuales son `initialize()` y
`encode_file()` — demasiado genéricos para el espacio de nombres global de una
`.so`, donde colisionan con cualquier otra librería. La propuesta los renombra.
Esto rompe el código Python existente, que necesita actualizar tres líneas de
`ctypes`. *No* se agregan alias de compatibilidad: con un único consumidor
conocido, mantener dos nombres para siempre es complejidad especulativa.
Si preferís conservar los nombres actuales, decilo en la revisión.

### Contrato de errores

El estado actual es que en el camino de error `*output` nunca se asigna, así que
un llamador que no chequee el retorno lee memoria no inicializada. Se corrige
garantizando `*output = NULL` antes de cualquier retorno de error.

El retorno sigue siendo `0` para error (no un código negativo) para no romper
llamadores que chequean `!= 0`. Un archivo DOCSIS válido nunca mide 0 bytes,
así que no hay ambigüedad. El detalle del error sigue yendo a `stderr`.

## Plan de ejecución

### Fase 0 — Baseline verde

Sin esto, el merge de la Fase 1 es a ciegas. En un codebase que emite binarios
firmados con HMAC-MD5, un error de codificación no produce un stack trace:
produce un `.cm` sutilmente incorrecto que un cable módem rechaza en campo.

1. `./autogen.sh && ./configure && make` — registrar si compila hoy.
2. Correr `tests/RunTests.sh`; registrar cuáles de los 142 pasan.
3. Correr `make check`; confirmar la rotura esperada de `examples/`.
4. Borrar los cuatro `.failed` commiteados en `tests/` (residuos de una corrida rota).
5. Congelar el resultado en `docs/superpowers/specs/2026-09-19-baseline.txt`.

**Criterio de salida:** existe un conjunto conocido de tests que pasan, y se
sabe exactamente cuáles no y por qué.

### Fase 1 — Merge de upstream (18 commits)

Va **antes** de reestructurar: cada commit de reorganización aleja el árbol de
upstream y encarece los conflictos.

Reparto del trabajo:

| Archivo | Líneas | Conflicto |
|---|---|---|
| `src/docsis_symtable.h` | 152 | No — el fork nunca lo tocó |
| `src/docsis_snmp.c` | 42 | No |
| `src/docsis_decode.c` | 9 | No |
| `src/docsis_yy.y` | 44 | Sí — el fork lo reescribió |
| `src/docsis_common.h` | 15 | Sí |
| `src/docsis_lex.l` | 3 | Sí, menor |
| `src/docsis.h` | 2 | Sí, menor |

Contenido: TLV84 (Diplexer Band Edge), TLV217 (eSTB), MULPIv4.0-I07,
DOCSIS Sync/PTP de SYNC-I03, `DigitMap`, `ManufacturerCVCChainFile` /
`CoSignerCVCChainFile`, corrección del decode de `IpModeControl`, límite de
`SFAQMLatencyTarget` subido a 255, y un memory leak.

**Riesgo específico:** el código nuevo de upstream en `docsis_yy.y` usa
`exit()` donde este fork usa `return 0` + `YYABORT`. El merge va a traer
`exit()` nuevos. Hay que auditar cada `exit()` del resultado del merge y
convertirlo, o un valor inválido en un TLV nuevo mata el proceso Python.

Los fixtures nuevos de upstream (`examples/ptp.*`, `docsis3.1_snmp_cvc.*`,
`packetcable_digitmap.*`) entran con el merge y amplían la cobertura gratis.

**Criterio de salida:** los tests de Fase 0 que pasaban, siguen pasando. Ningún
`exit()` nuevo en el camino de la librería.

### Fase 2 — Reestructurar el build

`configure.ac` ya tiene `LT_INIT` y `autogen.sh` ya corre `libtoolize`, así que
el trabajo es casi todo en `src/Makefile.am`:

1. `lib_LTLIBRARIES = libdocsis.la` con todos los fuentes actuales más
   `libdocsis.c`. Libtool maneja `-fPIC`, el soname y la instalación, lo que
   elimina el `gcc -shared` manual del README con sus rutas hardcodeadas
   (`perl/5.22`, `x86_64-linux-gnu`).
2. `check_PROGRAMS = docsis`, linkeando contra `libdocsis.la`.
3. `include_HEADERS = libdocsis.h`.
4. Deduplicar `encode_one_file()` sobre `encode_one_string()`.
5. Restaurar los `printf` de progreso en `encode_one_file()`.
6. Agregar `tests/Makefile.am` con `TESTS = RunTests.sh`, sumarlo a `SUBDIRS`
   y a `AC_CONFIG_FILES`. Hoy `tests/` está huérfano del build.
7. Reescribir el README: `./autogen.sh && ./configure && make && make install`.

**Criterio de salida:** `make check` corre las dos suites. `make install` deja
`libdocsis.so` y su header. El README ya no contiene comandos `gcc` manuales.

### Fase 3 — Correcciones conocidas

Van al final porque varias son de una línea y no vale la pena arrastrarlas por
un merge.

1. **Convención de retorno**: `*output = NULL` en todo camino de error.
2. **`docsis_free()`**: hoy Python tiene que llamar al `free` de libc por
   `ctypes`, lo que funciona en glibc por accidente pero no es un contrato.
3. **Candado**: un mutex de pthread alrededor de `docsis_encode()`, porque
   `ctypes` libera el GIL durante la llamada y `_my_tlvtree_head`, `line` y
   `strbuf[2048]` siguen siendo globales. Serializa: correcto siempre, lento
   solo si alguien realmente paraleliza. Marcar con un comentario
   `ponytail: candado global, refactor a contexto si el throughput importa`.
   Agregar `AX_PTHREAD` a `configure.ac`.
4. **`global_symtable[125576]`** → `[NUM_IDENTIFIERS]`. El 125576 es
   `sizeof(symbol_type) * NUM_IDENTIFIERS` = 88 × 1427, o sea el tamaño en
   *bytes* usado como *cantidad de elementos*: ~11 MB de BSS donde alcanzan
   125 KB. No es un bug de corrección (el `memcpy` copia la cantidad correcta),
   pero es desperdicio puro y confunde a quien lo lea.
5. **Restos de depuración**: sacar `"linea 61"`, `"linea 106"`, `"linea 138"`
   de los mensajes de error de `docsis_encode.c`.
6. **`line` no se resetea** entre parses: a partir del segundo archivo los
   números de línea de los mensajes de error son acumulados y por lo tanto
   incorrectos. Resetear a 1 en `parse_config_string()`.
7. **`exit()` remanentes en el lexer** (`docsis_lex.l:144,145,169`): `\n` o `\t`
   dentro de un string sin cerrar, y caracteres no reconocidos, todavía voltean
   el proceso. Convertir a `yyterminate()` como los demás.

### Fase 4 — Tests de la API

Los 142 fixtures solo prueban entradas **válidas**. La cobertura de caminos de
error es cero — justo lo que el fork reescribió entero cuando cambió `exit()`
por códigos de retorno. O sea: el trabajo más delicado del fork no tiene ni un
test.

Un archivo `tests/test_api.c` (linkeado contra `libdocsis.la`, corriendo bajo
`make check`) con asserts para:

| Caso | Se espera |
|---|---|
| Config válido mínimo | retorno > 0, `*output != NULL` |
| Valor fuera de rango (`NetworkAccess 99;`) | retorno 0, `*output == NULL`, proceso vivo |
| Símbolo desconocido | retorno 0, proceso vivo |
| Llave sin cerrar | retorno 0, proceso vivo |
| Buffer vacío / `content_size` 0 | retorno 0, sin crash |
| Dos llamadas seguidas | la segunda da el mismo resultado que la primera |

El último caso es el que detecta regresiones de estado global, que es la clase
de bug más probable en este código.

Sin framework, sin fixtures, sin mocks: un `main()` con `assert`.

## Riesgos

- **El baseline puede estar rojo.** Si la Fase 0 encuentra que los 142 no pasan
  hoy, hay que decidir si se arregla antes de mergear o se acepta un baseline
  parcial. Se resuelve con datos cuando lleguemos.
- **El merge puede traer `exit()` en caminos calientes.** Mitigado por la
  auditoría explícita de la Fase 1.
- **El renombre de la API rompe el Python existente.** Explícito arriba, sujeto
  a revisión.
- **Los MIBs tienen que estar presentes en tiempo de ejecución.** `docsis_init()`
  los carga desde `mibs/`. Si un consumidor instala la `.so` sin los MIBs, falla
  en runtime con un mensaje de net-snmp. Documentar en el README.
