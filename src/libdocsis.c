#include "docsis.h"
int encode_file(unsigned char *file_content, unsigned content_size, unsigned char ** output_file,
		       unsigned char *key, unsigned int keylen, int encode_docsis, unsigned int hash);

void initialize(void);

int encode_file( unsigned char *file_content, unsigned file_content_size, unsigned char **buffer,
      unsigned char *key, unsigned int keylen, int encode_docsis, unsigned int hash){

 	return encode_one_string (file_content, file_content_size, buffer, key, keylen, encode_docsis, hash);
}

void initialize(void){
	initialize_lib();
}
