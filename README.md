# libdocsis

Codificador de archivos de configuración DOCSIS, empaquetado como librería
compartida para consumir desde Python con `ctypes`.

Fork de [rlaager/docsis](https://github.com/rlaager/docsis). Upstream es una
herramienta de línea de comandos; este fork expone el codificador como `.so`
que trabaja de buffer a buffer, sin pasar por archivos.

## Dependencias

Debian / Ubuntu:

```sh
sudo apt-get install autoconf automake libtool flex bison libsnmp-dev gcc make
```

## Build

```sh
./autogen.sh
./configure
make
sudo make install
```

Eso instala `libdocsis.so`, su header, y los MIBs de DOCSIS en la ruta de
net-snmp.

## MIBs

`initialize()` carga los MIBs a través de net-snmp y **no** setea `MIBDIRS`.

- Después de `make install` no hace falta hacer nada: los MIBs quedan en
  `/usr/share/snmp/mibs`, que es la ruta por defecto de net-snmp.
- Corriendo desde el árbol de build, exportá:

```sh
export MIBDIRS="$PWD/mibs:$PWD/mibs/iana:$PWD/mibs/ietf"
```

Si los MIBs no están, `initialize()` devuelve 0 y explica qué falta.

## API

Tres funciones. **`0` significa fallo en todas, sin excepciones.**

```c
int  initialize (void);
int  encode_file (unsigned char *content, unsigned content_size,
                  unsigned char **output,
                  unsigned char *key, unsigned int keylen,
                  int encode_docsis, unsigned int hash);
void free_encoded_file (unsigned char *buffer);
```

- `initialize()` — una vez, antes de todo. Devuelve 1 en éxito.
- `encode_file()` — devuelve el largo en bytes (`> 0`) y deja el buffer en
  `*output`. En error devuelve `0` y deja `*output` en `NULL`. El detalle del
  error va a `stderr`.
- `free_encoded_file()` — libera lo que devolvió `encode_file()`. Acepta `NULL`.

## Ejemplo con ctypes

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

config = b"""Main {
  DownstreamFrequency 123000000;
  UpstreamChannelId 1;
  NetworkAccess 1;
}
"""
key = b"aaaaaaaaaaaaaaaa"

out = ctypes.c_char_p()
length = lib.encode_file(config, len(config), ctypes.byref(out),
                         key, len(key), 1, 0)
if length == 0:
    raise RuntimeError("falló la codificación; el detalle salió por stderr")

try:
    data = ctypes.string_at(out, length)   # copia los bytes a Python
finally:
    lib.free_encoded_file(out)

print(f"{len(data)} bytes: {data[:16].hex()}")
```

**`ctypes.string_at(out, length)` y no `out.value`**: el buffer es binario y
contiene ceros, así que tratarlo como cadena terminada en NUL lo trunca en el
primer `0x00`.

## Concurrencia

`encode_file()` serializa internamente con un mutex. Es seguro llamarla desde
varios hilos, pero no corre en paralelo. El candado hace falta porque `ctypes`
libera el GIL durante la llamada y el parser todavía tiene estado global; el
camino de mejora está anotado en `src/docsis_yy.y`.

## Tests

```sh
make check
```

Corre tres suites:

- `tests/test_api` — la API pública, incluidos los caminos de error.
- `tests/run-fixtures.sh` — 142 configuraciones reales, round-trip triple.
  Falla solo si el conjunto de casos rotos cambia respecto de
  `tests/known-failures.txt`, en cualquier dirección.
- `examples/` — comparación encode-contra-decode sobre 21 ejemplos.

Hay 10 fixtures y 1 ejemplo que fallan por bugs de upstream, documentados en
`docs/superpowers/specs/2026-09-19-baseline.txt`. Ninguno afecta al camino de
la librería.

## Diferencias con upstream

- El binario `docsis` **no se instala**. Existe solo como arnés de test.
- El decodificador no se exporta: la librería solo codifica.
- Sin soporte de Windows: `parse_config_string()` usa `fmemopen()`, que es
  POSIX.
- La librería nunca llama a `exit()` ni escribe en `stdout`. Upstream, siendo
  un CLI, hace las dos cosas, y cualquiera de ellas rompe a un consumidor
  embebido.
