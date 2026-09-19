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
#include <unistd.h>

#include "libdocsis.h"

static unsigned char KEY[] = "aaaaaaaaaaaaaaaa";

/* Veneno para el puntero de salida: si el helper lo pusiera en NULL, los
   tests del camino de error pasarían sin probar nada. Nunca se desreferencia. */
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

/* Una config real del set de fixtures: la mínima de arriba tiene 4 líneas y
   no ejercita anidamiento, TLVs vendor-specific ni OIDs. */
static void
test_real_world_config_encodes (void)
{
  unsigned char *buf = POISON;
  unsigned size = 0;
  char *cfg = slurp_fixture ("TLV_24_last_before_43.txt", &size);
  int len;

  if (cfg == NULL)
    {
      printf ("skip - no encontré el fixture\n");
      return;
    }
  len = encode_file ((unsigned char *) cfg, size, &buf,
		     KEY, (unsigned) strlen ((char *) KEY), 1, 0);
  free (cfg);

  assert (len > 0);
  assert (buf != NULL && buf != POISON);
  free_encoded_file (buf);
  printf ("ok - una config real y compleja codifica (%d bytes)\n", len);
}

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


static void
test_snmp_oid_resolves (void)
{
  /* Si los MIBs no cargaron, este OID simbólico no resuelve y el encode
     falla. Es la prueba indirecta de que initialize() hizo su trabajo. */
  static const char *WITH_OID =
    "Main {\n"
    "  DownstreamFrequency 123000000;\n"
    "  UpstreamChannelId 1;\n"
    "  NetworkAccess 1;\n"
    "  SnmpMibObject sysContact.0 String \"test\";\n"
    "}\n";
  unsigned char *buf;
  int len = encode (WITH_OID, &buf);

  assert (len > 0);
  assert (buf != NULL && buf != POISON);
  free_encoded_file (buf);
  printf ("ok - un OID simbólico resuelve, o sea que los MIBs cargaron\n");
}


/* `line` es global. Sin resetearla, desde el segundo encode del proceso los
   números de línea de los errores vienen acumulados: un error en la línea 4
   del segundo archivo se reportaba como línea 15. Dos entradas idénticas
   tienen que producir el mismo mensaje. */
static void
test_line_numbers_reset_between_calls (void)
{
  static const char *BAD =
    "Main {\n"
    "  DownstreamFrequency 123000000;\n"
    "  UpstreamChannelId 1;\n"
    "  NetworkAccess 99;\n"
    "}\n";
  char first[512] = { 0 }, second[512] = { 0 };
  unsigned char *buf;
  int saved;
  size_t i;

  fflush (stderr);
  saved = dup (fileno (stderr));

  for (i = 0; i < 2; i++)
    {
      char *dst = i ? second : first;
      FILE *f;

      if (freopen (i ? "stderr2.tmp" : "stderr1.tmp", "w", stderr) == NULL)
	{
	  printf ("skip - no pude redirigir stderr\n");
	  dup2 (saved, fileno (stderr));
	  close (saved);
	  return;
	}
      encode (BAD, &buf);
      fflush (stderr);
      dup2 (saved, fileno (stderr));

      f = fopen (i ? "stderr2.tmp" : "stderr1.tmp", "r");
      assert (f != NULL);
      if (fread (dst, 1, 511, f) == 0)
	dst[0] = '\0';
      fclose (f);
    }
  close (saved);
  remove ("stderr1.tmp");
  remove ("stderr2.tmp");

  assert (strstr (first, "out of range") != NULL);
  assert (strcmp (first, second) == 0);
  printf ("ok - los números de línea se resetean entre llamadas\n");
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
  test_real_world_config_encodes ();
  test_repeated_calls_are_identical ();
  test_invalid_value_fails_cleanly ();
  test_unknown_symbol_fails_cleanly ();
  test_unclosed_brace_fails_cleanly ();
  test_empty_input_fails_cleanly ();
  test_unterminated_string_fails_cleanly ();
  test_null_output_pointer_is_rejected ();
  test_free_accepts_null ();
  test_snmp_oid_resolves ();
  test_line_numbers_reset_between_calls ();

  printf ("todos los tests de la API pasaron\n");
  return 0;
}
