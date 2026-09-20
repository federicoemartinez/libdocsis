lib_LTLIBRARIES = libdocsis.la
libdocsis_la_SOURCES = docsis.c ethermac.c md5.c docsis_encode.c \
	docsis_decode.c docsis_snmp.c docsis_yy.y docsis_lex.l hmac_md5.c \
	sha1.c libdocsis.c
libdocsis_la_LIBADD = $(all_libraries) $(NETSNMP_LIBS) $(LEXLIB) $(LIBM)
# Without -export-symbols-regex the .so exports its whole internal world
# (md5_init, sha1_transform, global_symtable, line...), which collides with
# any other library loaded in the same process -- OpenSSL, for one.
libdocsis_la_LDFLAGS = -export-symbols-regex \
	'^(initialize|encode_file|free_encoded_file)$$'

include_HEADERS = libdocsis.h

# The CLI is no longer a product: it is the harness that drives the 142
# fixtures under tests/ and the encode-vs-decode comparison under examples/.
# noinst_PROGRAMS rather than check_PROGRAMS: `make` builds it but
# `make install` does not install it, which is exactly what we want. With
# check_PROGRAMS it only existed during `make check`, and examples/Makefile.am
# needs it earlier -- its `.cfg.bin: ../src/docsis` buys nothing, because GNU
# make ignores prerequisites on suffix rules (it warns about it at run time).
noinst_PROGRAMS = docsis
docsis_SOURCES = docsis_main.c
docsis_LDADD = libdocsis.la $(all_libraries) $(NETSNMP_LIBS) $(LEXLIB) $(LIBM)
# -static: the harness uses internal symbols (decode_file, encode_one_file,
# get_output_name...) that -export-symbols-regex hides from the .so. Linking
# against the static archive keeps them visible without opening up the
# product's dynamic table, which is exactly what we want: the library exports
# three symbols, the harness sees everything.
docsis_LDFLAGS = -static

AM_CPPFLAGS = $(all_includes) $(NETSNMP_CFLAGS)
noinst_HEADERS = ethermac.h md5.h docsis.h docsis_common.h docsis_decode.h \
	docsis_encode.h docsis_symtable.h docsis_snmp.h docsis_globals.h sha1.h
EXTRA_DIST = docsis_yy.h
CLEANFILES = docsis_yy.c docsis_yy.h docsis_yy.output docsis_lex.c
DISTCLEANFILES = Makefile
