/*
 * Tests for the public libdocsis API.
 *
 * No framework: a main() with asserts. Run through `make check`.
 *
 * Links against libdocsis.la, i.e. against the .so with its symbol table
 * already restricted: if anything here fails to link, the public API is
 * incomplete.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libdocsis.h"

static unsigned char KEY[] = "aaaaaaaaaaaaaaaa";

/* Poison for the output pointer: were the helper to set it to NULL, the
   error-path tests would pass without proving anything. Never dereferenced. */
#define POISON ((unsigned char *) 0xDEADBEEF)

/* Encodes a config and returns the length; *out receives the buffer (or
   NULL on failure). The caller releases it with free_encoded_file(). */
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
  printf ("ok - a minimal config encodes (%d bytes)\n", len);
}

/* Reads a fixture from tests/ into memory. Returns NULL if absent. */
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

/* A real config from the fixture set: the minimal one above is four lines
   and exercises no nesting, no vendor-specific TLVs and no OIDs. */
static void
test_real_world_config_encodes (void)
{
  unsigned char *buf = POISON;
  unsigned size = 0;
  char *cfg = slurp_fixture ("TLV_24_last_before_43.txt", &size);
  int len;

  if (cfg == NULL)
    {
      printf ("skip - fixture not found\n");
      return;
    }
  len = encode_file ((unsigned char *) cfg, size, &buf,
		     KEY, (unsigned) strlen ((char *) KEY), 1, 0);
  free (cfg);

  assert (len > 0);
  assert (buf != NULL && buf != POISON);
  free_encoded_file (buf);
  printf ("ok - a real, complex config encodes (%d bytes)\n", len);
}

static void
test_invalid_value_fails_cleanly (void)
{
  /* NetworkAccess accepts 0 or 1; 99 is out of range. */
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
  printf ("ok - an out-of-range value fails without leaving a dangling pointer\n");
}

static void
test_unknown_symbol_fails_cleanly (void)
{
  static const char *BAD = "Main {\n  NoExisteEsteTLV 1;\n}\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - an unknown symbol fails without leaving a dangling pointer\n");
}

static void
test_unclosed_brace_fails_cleanly (void)
{
  static const char *BAD = "Main {\n  NetworkAccess 1;\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - an unclosed brace fails without leaving a dangling pointer\n");
}

static void
test_empty_input_fails_cleanly (void)
{
  unsigned char *buf;
  int len = encode ("", &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - empty input fails without crashing\n");
}

static void
test_unterminated_string_fails_cleanly (void)
{
  /* Unterminated quote: the lexer hits \n while in the STRING state. That
     used to call exit(-1) and kill the caller's process, so this test only
     passes if the process survives to the end of main(). */
  static const char *BAD =
    "Main {\n"
    "  SnmpMibObject sysContact.0 String \"sin cerrar;\n"
    "}\n";
  unsigned char *buf;
  int len = encode (BAD, &buf);

  assert (len == 0);
  assert (buf == NULL);
  printf ("ok - an unterminated string fails without killing the process\n");
}

static void
test_null_output_pointer_is_rejected (void)
{
  /* Must not dereference NULL. */
  int len = encode_file ((unsigned char *) MINIMAL, (unsigned) strlen (MINIMAL),
			 NULL, KEY, (unsigned) strlen ((char *) KEY), 1, 0);
  assert (len == 0);
  printf ("ok - a NULL output pointer is rejected without being dereferenced\n");
}

/* The spec asks for this explicitly: two consecutive calls with the same
   input must yield exactly the same bytes. This is the test that catches
   global state leaking from one call into the next. */
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
  printf ("ok - two consecutive calls produce identical bytes\n");
}

/* free_encoded_file(NULL) must be harmless, like free(). */
static void
test_free_accepts_null (void)
{
  free_encoded_file (NULL);
  printf ("ok - free_encoded_file(NULL) is harmless\n");
}


static void
test_snmp_oid_resolves (void)
{
  /* If the MIBs did not load, this symbolic OID does not resolve and the
     encode fails. It is the indirect proof that initialize() did its job. */
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
  printf ("ok - a symbolic OID resolves, so the MIBs loaded\n");
}


/* `line` is global. Without resetting it, from the process's second encode
   onwards the error line numbers accumulate: an error on line 4 of the
   second input was reported as line 15. Two identical inputs must produce
   the same message. */
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
	  printf ("skip - could not redirect stderr\n");
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
  printf ("ok - line numbers reset between calls\n");
}

int
main (void)
{
  if (!initialize ())
    {
      fprintf (stderr, "initialize() failed: are the MIBs on MIBDIRS?\n");
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

  printf ("all API tests passed\n");
  return 0;
}
