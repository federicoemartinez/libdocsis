check_PROGRAMS = test_api
test_api_SOURCES = test_api.c
test_api_CPPFLAGS = -I$(top_srcdir)/src
test_api_LDADD = $(top_builddir)/src/libdocsis.la

TESTS = test_api run-fixtures.sh

# RunTests.sh no exporta MIBDIRS por su cuenta; sin esto net-snmp no encuentra
# los MIBs de esta distribución y todos los OIDs fallan.
# srcdir lo usa test_api para ubicar los fixtures.
TESTS_ENVIRONMENT = \
	MIBDIRS="$(top_srcdir)/mibs:$(top_srcdir)/mibs/iana:$(top_srcdir)/mibs/ietf" \
	srcdir="$(srcdir)"

EXTRA_DIST = RunTests.sh run-fixtures.sh known-failures.txt key
# ponytail: los 426 fixtures (.txt/.cm/.conf) no están en EXTRA_DIST, así que
# `make dist` no los incluye. Listarlos a mano es inmanejable y este repo no
# hace releases con tarball. Si hiciera falta, generar la lista con un include.

CLEANFILES = *.new RunTests.log
