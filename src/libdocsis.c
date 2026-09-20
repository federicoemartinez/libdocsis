/*
 * Public facade of libdocsis. Everything the .so exports lives here; the
 * rest of the tree is internal (see -export-symbols-regex in Makefile.am).
 */
#include <stdlib.h>
#include <pthread.h>

#include "docsis.h"
#include "libdocsis.h"

/* ponytail: one global lock around the whole encode. The parser still has
 * global state (_my_tlvtree_head in docsis_yy.y, line and strbuf in
 * docsis_lex.l) and ctypes releases the GIL for the duration of the call,
 * so without this two Python threads corrupt each other silently. It
 * serializes: always correct, only slow if somebody actually parallelizes.
 * Upgrade path: move that state into a context struct carried through
 * %parse-param alongside the yyscan_t, then drop the lock. That touches
 * every grammar rule, so it is best done right after an upstream merge
 * rather than right before one. */
static pthread_mutex_t encode_lock = PTHREAD_MUTEX_INITIALIZER;

int
encode_file (unsigned char *file_content, unsigned file_content_size,
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

int
initialize (void)
{
  return initialize_lib ();
}

void
free_encoded_file (unsigned char *buffer)
{
  free (buffer);
}
