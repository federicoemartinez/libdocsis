#include "docsis.h"
int encode_file(unsigned char *file_content, unsigned content_size, unsigned char ** output_file,
		       unsigned char *key, unsigned int keylen, int encode_docsis, unsigned int hash);



int encode_file( unsigned char *file_content, unsigned file_content_size, unsigned char **buffer,
      unsigned char *key, unsigned int keylen, int encode_docsis, unsigned int hash){

 	return encode_one_string (file_content, file_content_size, buffer, key, keylen, encode_docsis, hash);
}

/*
gcc -shared -fPIC -DHAVE_CONFIG_H -I. -I.. -fpic   -DNETSNMP_ENABLE_IPV6 -fno-strict-aliasing -g -O2 -fstack-protector-strong -Wformat -Werror=format-security -DNETSNMP_USE_INLINE -Ulinux -Dlinux=linux -D_REENTRANT -D_GNU_SOURCE -DDEBIAN -fwrapv -fno-strict-aliasing -pipe -I/usr/local/include -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -I/usr/lib/x86_64-linux-gnu/perl/5.22/CORE -Wdate-time -D_FORTIFY_SOURCE=2 -I. -I/usr/include   -Wall  -Waggregate-return -Wbad-function-cast -Wcast-align -Wdeclaration-after-statement -Wendif-labels -Werror-implicit-function-declaration -Wextra -Wno-unused-parameter -Wfloat-equal -Wformat-security -Werror=format-security -Winit-self -Wmissing-declarations -Wmissing-noreturn -Wmissing-prototypes -Wpointer-arith -Wundef -L/usr/lib/x86_64-linux-gnu -lnetsnmp -lcrypto -lfl -lm -lresolv libdocsis.c docsis.o ethermac.o md5.o docsis_encode.o docsis_decode.o docsis_snmp.o docsis_yy.o docsis_lex.o hmac_md5.o sha1.o -Wl,-Bsymbolic-functions -Wl,-z -Wl,relro -Wl,-z -Wl,now  -L/usr/lib/x86_64-linux-gnu -lnetsnmp -lcrypto -lfl -lm -lresolv
fede@belfry:~/Documents/stechs/docsis/sr


from ctypes import *;
lib = cdll.LoadLibrary('/home/fede/Documents/stechs/libdocsis/src/a.out')
ifile = file('cosa2.txt', 'rb')
content = ifile.read()

pointer = POINTER(c_ubyte)
ppointer = POINTER(POINTER(c_ubyte))
p = pointer()
p1 = pointer
lib.encode_file(content, len(content),pointer, 'a',1,1,0) 
lib.encode_file(FULL_PATH_AL_TXT, FULL_PATH_AL_BIN, KEY_STRING, KEY_LENGHT, 1, 0)
*/