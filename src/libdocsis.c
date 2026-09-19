/*
 * Fachada pública de libdocsis. Todo lo que la .so exporta vive acá;
 * el resto del árbol es interno (ver -export-symbols-regex en Makefile.am).
 */
#include <stdlib.h>
#include <pthread.h>

#include "docsis.h"
#include "libdocsis.h"

/* ponytail: candado global sobre todo el encode. El parser todavía tiene
 * estado global (_my_tlvtree_head en docsis_yy.y, line y strbuf en
 * docsis_lex.l) y ctypes libera el GIL durante la llamada, así que sin esto
 * dos hilos de Python se pisan en silencio. Serializa: correcto siempre,
 * lento solo si alguien realmente paraleliza.
 * Camino de mejora: mover ese estado a un struct de contexto que viaje por
 * %parse-param junto al yyscan_t, y sacar el candado. Toca cada regla de la
 * gramática, así que conviene hacerlo justo después de un merge con upstream
 * y no justo antes. */
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
