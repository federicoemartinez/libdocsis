/*
 * Fachada pública de libdocsis. Todo lo que la .so exporta vive acá;
 * el resto del árbol es interno (ver -export-symbols-regex en Makefile.am).
 */
#include <stdlib.h>

#include "docsis.h"
#include "libdocsis.h"

int
encode_file (unsigned char *file_content, unsigned file_content_size,
	     unsigned char **buffer, unsigned char *key, unsigned int keylen,
	     int encode_docsis, unsigned int hash)
{
  return encode_one_string (file_content, file_content_size, buffer,
			    key, keylen, encode_docsis, hash);
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
