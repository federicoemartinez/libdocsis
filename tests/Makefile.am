check_PROGRAMS = test_api
test_api_SOURCES = test_api.c
test_api_CPPFLAGS = -I$(top_srcdir)/src
test_api_LDADD = $(top_builddir)/src/libdocsis.la

TESTS = test_api run-fixtures.sh

# RunTests.sh does not export MIBDIRS on its own; without this net-snmp does
# not find this distribution's MIBs and every symbolic OID fails.
# test_api uses srcdir to locate the fixtures.
TESTS_ENVIRONMENT = \
	MIBDIRS="$(top_srcdir)/mibs:$(top_srcdir)/mibs/iana:$(top_srcdir)/mibs/ietf" \
	srcdir="$(srcdir)"

EXTRA_DIST = RunTests.sh run-fixtures.sh known-failures.txt key lsan.supp
# ponytail: the 426 fixtures (.txt/.cm/.conf) are not in EXTRA_DIST, so
# `make dist` leaves them out. Listing them by hand is unmanageable and this
# repo does not cut tarball releases. If that changes, generate the list into
# an included Makefile fragment.

CLEANFILES = *.new RunTests.log
