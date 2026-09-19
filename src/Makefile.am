lib_LTLIBRARIES = libdocsis.la
libdocsis_la_SOURCES = docsis.c ethermac.c md5.c docsis_encode.c \
	docsis_decode.c docsis_snmp.c docsis_yy.y docsis_lex.l hmac_md5.c \
	sha1.c libdocsis.c
libdocsis_la_LIBADD = $(all_libraries) $(NETSNMP_LIBS) $(LEXLIB) $(LIBM)
# Sin -export-symbols-regex la .so exporta todo su mundo interno (md5_init,
# sha1_transform, global_symtable, line...), que colisiona con cualquier otra
# librería cargada en el mismo proceso -- OpenSSL, por ejemplo.
libdocsis_la_LDFLAGS = -export-symbols-regex \
	'^(initialize|encode_file|free_encoded_file)$$'

include_HEADERS = libdocsis.h

# El CLI ya no es un producto: es el arnés que maneja los 142 fixtures de
# tests/ y la comparación encode-contra-decode de examples/.
# noinst_PROGRAMS y no check_PROGRAMS: `make` lo construye pero `make install`
# no lo instala, que es exactamente lo que queremos. Con check_PROGRAMS solo
# existía durante `make check`, y examples/Makefile.am lo necesita antes --
# su `.cfg.bin: ../src/docsis` no sirve de nada porque GNU make ignora los
# prerequisitos de las reglas de sufijo (lo avisa al correr).
noinst_PROGRAMS = docsis
docsis_SOURCES = docsis_main.c
docsis_LDADD = libdocsis.la $(all_libraries) $(NETSNMP_LIBS) $(LEXLIB) $(LIBM)
# -static: el arnés usa símbolos internos (decode_file, encode_one_file,
# get_output_name...) que -export-symbols-regex esconde de la .so. Linkear
# contra el archivo estático los deja visibles sin abrir la tabla dinámica
# del producto, que es justo lo que queremos: la librería exporta 3 símbolos,
# el arnés ve todo.
docsis_LDFLAGS = -static

AM_CPPFLAGS = $(all_includes) $(NETSNMP_CFLAGS)
noinst_HEADERS = ethermac.h md5.h docsis.h docsis_common.h docsis_decode.h \
	docsis_encode.h docsis_symtable.h docsis_snmp.h docsis_globals.h sha1.h
EXTRA_DIST = docsis_yy.h
CLEANFILES = docsis_yy.c docsis_yy.h docsis_yy.output docsis_lex.c
DISTCLEANFILES = Makefile
