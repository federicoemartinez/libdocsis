/*
 * Tests de la API pública de libdocsis.
 *
 * Sin framework: un main() con asserts. Correr con `make check`.
 *
 * Linkea contra libdocsis.la, o sea contra la .so con la tabla de símbolos
 * ya restringida: si algo de acá no linkea, es que la API pública está
 * incompleta.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdocsis.h"

static unsigned char KEY[] = "aaaaaaaaaaaaaaaa";

/*
 * Valor de veneno para el puntero de salida. Nunca se desreferencia: existe
 * para que los tests del camino de error prueben algo.
 *
 * Si el helper pusiera *out = NULL antes de llamar, la aserción
 * "assert (buf == NULL)" se cumpliría sola y el test pasaría aunque
 * encode_file no tocara el puntero -- que es justo el bug que buscamos.
 */
#define POISON ((unsigned char *) 0xDEADBEEF)

/* Codifica una config y devuelve el largo; *out queda con el buffer (o en
   NULL si falló). El llamador libera con free_encoded_file(). */
static int
encode (const char *cfg, unsigned char **out)
{
  *out = POISON;
  return encode_file ((unsigned char *) cfg, (unsigned) strlen (cfg), out,
		      KEY, (unsigned) strlen ((char *) KEY), 1, 0);
}

static const char *MINIMAL =
  "Main {\n"
  "  DownstreamFrequency 123000000;\n"
  "  UpstreamChannelId 1;\n"
  "  NetworkAccess 1;\n"
  "}\n";

static void
test_minimal_config_encodes (void)
{
  unsigned char *buf = NULL;
  int len = encode (MINIMAL, &buf);

  assert (len > 0);
  assert (buf != NULL && buf != POISON);
  free_encoded_file (buf);
  printf ("ok - una config mínima codifica (%d bytes)\n", len);
}

/* Lee un fixture de tests/ a memoria. Devuelve NULL si no está. */
static char *
slurp_fixture (const char *name, unsigned *size)
{
  const char *dir = getenv ("srcdir");
  char path[512];
  FILE *f;
  long n;
  char *buf;

  snprintf (path, sizeof (path), "%s/%s", dir ? dir : ".", name);
  if ((f = fopen (path, "rb")) == NULL)
    return NULL;
  fseek (f, 0, SEEK_END);
  n = ftell (f);
  fseek (f, 0, SEEK_SET);
  if (n <= 0)
    {
      fclose (f);
      return NULL;
    }
  buf = (char *) malloc ((size_t) n + 1);
  if (buf == NULL)
    {
      fclose (f);
      return NULL;
    }
  if (fread (buf, 1, (size_t) n, f) != (size_t) n)
    {
      fclose (f);
      free (buf);
      return NULL;
    }
  fclose (f);
  buf[n] = '\0';
  *size = (unsigned) n;
  return buf;
}

/*
 * Tres fixtures fallan en tests/RunTests.sh desde que se mergeó upstream,
 * porque upstream cambió AggregateServiceFlowReference de contenedor a hoja
 * ushort sin actualizar sus propios fixtures. El fallo ocurre en
 * decode_ushort(), que hace exit(-45), y el CLI decodifica para mostrar antes
 * de escribir -- o sea que un error de DECODE aborta el ENCODE.
 *
 * La librería no decodifica. Este test verifica que el camino que realmente
 * se exporta codifica estas entradas sin problema, y por lo tanto que esas
 * tres regresiones de upstream afectan al arnés y no al producto.
 */
static void
test_upstream_regressions_do_not_affect_the_library (void)
{
  static const char *FIXTURES[] = {
    "TLV_24_last_before_43.txt",
    "TLV_25_remaining.txt",
    "TLV_70_TLV_71_AggregateServiceFlow.txt",
  };
  size_t i;

  for (i = 0; i < sizeof (FIXTURES) / sizeof (FIXTURES[0]); i++)
    {
      unsigned char *buf = POISON;
      unsigned size = 0;
      char *cfg = slurp_fixture (FIXTURES[i], &size);
      int len;

      if (cfg == NULL)
	{
	  printf ("skip - no encontré %s\n", FIXTURES[i]);
	  continue;
	}
      len = encode_file ((unsigned char *) cfg, size, &buf,
			 KEY, (unsigned) strlen ((char *) KEY), 1, 0);
      free (cfg);

      assert (len > 0);
      assert (buf != NULL && buf != POISON);
      free_encoded_file (buf);
      printf ("ok - la librería codifica %s (%d bytes)\n", FIXTURES[i], len);
    }
}


/*
 * Task 7: el contrato del camino de error.
 *
 * encode_one_string() devolvía 0 sin tocar el puntero de salida, así que un
 * llamador que no chequeara el retorno leía memoria sin inicializar. Estos
 * tests fijan la garantía: retorno 0 implica *output == NULL.
 *
 * Los 142 fixtures solo prueban entradas válidas. Esta es la primera
 * cobertura de los caminos de error -- justo lo que el fork reescribió entero
 * cuando cambió exit() por códigos de retorno.
 */

static void
test_invalid_value_fails_cleanly (void)
{
  /* NetworkAccess acepta 0 o 1; 99 está fuera de rango. */
  static const char *BAD =
    "Main {\n"
    "  DownstreamFrequency 123000000;\n"
    "  UpstreamChannelId 1;\n"
    "  NetworkAccess 99;\n"
    "}\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - un valor fuera de rango falla sin dejar el puntero colgado\n");
}

static void
test_unknown_symbol_fails_cleanly (void)
{
  static const char *BAD = "Main {\n  NoExisteEsteTLV 1;\n}\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - un símbolo desconocido falla sin dejar el puntero colgado\n");
}

static void
test_unclosed_brace_fails_cleanly (void)
{
  static const char *BAD = "Main {\n  NetworkAccess 1;\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - una llave sin cerrar falla sin dejar el puntero colgado\n");
}

static void
test_empty_input_fails_cleanly (void)
{
  unsigned char *buf;
  int len = encode ("", &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - una entrada vacía falla sin crashear\n");
}

static void
test_unterminated_string_fails_cleanly (void)
{
  /* Comilla sin cerrar: el lexer encuentra \n dentro del estado STRING.
     Antes eso llamaba exit(-1) y mataba el proceso del llamador, así que
     este test pasa solo si el proceso sobrevive hasta el final de main(). */
  static const char *BAD =
    "Main {\n"
    "  SnmpMibObject sysContact.0 String \"sin cerrar;\n"
    "}\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - un string sin cerrar falla sin matar el proceso\n");
}

static void
test_null_output_pointer_is_rejected (void)
{
  /* No debe desreferenciar NULL. */
  int len = encode_file ((unsigned char *) MINIMAL, (unsigned) strlen (MINIMAL),
			 NULL, KEY, (unsigned) strlen ((char *) KEY), 1, 0);
  assert (len == 0);
  printf ("ok - un puntero de salida NULL se rechaza sin desreferenciarlo\n");
}

/* El spec lo pide explícitamente: dos llamadas seguidas con la misma entrada
   tienen que dar exactamente los mismos bytes. Es el test que detecta que el
   estado global se filtre de una llamada a la siguiente. */
static void
test_repeated_calls_are_identical (void)
{
  unsigned char *a = NULL, *b = NULL;
  int la, lb;

  la = encode (MINIMAL, &a);
  lb = encode (MINIMAL, &b);

  assert (la > 0);
  assert (la == lb);
  assert (a != NULL && a != POISON);
  assert (b != NULL && b != POISON);
  assert (memcmp (a, b, (size_t) la) == 0);

  free_encoded_file (a);
  free_encoded_file (b);
  printf ("ok - dos llamadas seguidas producen bytes idénticos\n");
}

/* free_encoded_file(NULL) tiene que ser inocuo, como free(). */
static void
test_free_accepts_null (void)
{
  free_encoded_file (NULL);
  printf ("ok - free_encoded_file(NULL) es inocuo\n");
}

int
main (void)
{
  if (!initialize ())
    {
      fprintf (stderr, "initialize() falló: ¿están los MIBs en MIBDIRS?\n");
      return 1;
    }

  test_minimal_config_encodes ();
  test_upstream_regressions_do_not_affect_the_library ();
  test_repeated_calls_are_identical ();
  test_invalid_value_fails_cleanly ();
  test_unknown_symbol_fails_cleanly ();
  test_unclosed_brace_fails_cleanly ();
  test_empty_input_fails_cleanly ();
  test_unterminated_string_fails_cleanly ();
  test_null_output_pointer_is_rejected ();
  test_free_accepts_null ();

  printf ("todos los tests de la API pasaron\n");
  return 0;
}
