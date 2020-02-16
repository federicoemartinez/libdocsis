The idea behind this project is that I needed to be able to encode a docsis file from a buffer and to a buffer. I wanted also to be able to do it from python using ctypes.
I have little experience in C, so I modifieded the original docsis project to add a function that works from buffers.

In order to make it work you have to:
1) Follow the instructions to build the original project: https://github.com/rlaager/docsis
2) Edit the Makefile in the src folder:
Add to the line 
COMPILE = $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
the -fPIC flag:
COMPILE = $(CC) $(DEFS) $(DEFAULT_INCLUDES) -fpic $(INCLUDES) $(AM_CPPFLAGS) \
3) After that complie libdocis.c, with something like:
gcc -shared -fPIC -DHAVE_CONFIG_H -I. -I.. -fpic   -DNETSNMP_ENABLE_IPV6 -fno-strict-aliasing -g -O2 -fstack-protector-strong -Wformat -Werror=format-security -DNETSNMP_USE_INLINE -Ulinux -Dlinux=linux -D_REENTRANT -D_GNU_SOURCE -DDEBIAN -fwrapv -fno-strict-aliasing -pipe -I/usr/local/include -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -I/usr/lib/x86_64-linux-gnu/perl/5.22/CORE -Wdate-time -D_FORTIFY_SOURCE=2 -I. -I/usr/include   -Wall  -Waggregate-return -Wbad-function-cast -Wcast-align -Wdeclaration-after-statement -Wendif-labels -Werror-implicit-function-declaration -Wextra -Wno-unused-parameter -Wfloat-equal -Wformat-security -Werror=format-security -Winit-self -Wmissing-declarations -Wmissing-noreturn -Wmissing-prototypes -Wpointer-arith -Wundef -L/usr/lib/x86_64-linux-gnu -lnetsnmp -lcrypto -lfl -lm -lresolv libdocsis.c docsis.o ethermac.o md5.o docsis_encode.o docsis_decode.o docsis_snmp.o docsis_yy.o docsis_lex.o hmac_md5.o sha1.o -Wl,-Bsymbolic-functions -Wl,-z -Wl,relro -Wl,-z -Wl,now  -L/usr/lib/x86_64-linux-gnu -lnetsnmp -lcrypto -lfl -lm -lresolv

Then you have the .so and you have a function there:
int encode_file(unsigned char *file_content, unsigned content_size, unsigned char ** output_file,
		       unsigned char *key, unsigned int keylen, int encode_docsis, unsigned int hash);
that encodes the file_content and returns the data in the output_file pointer
