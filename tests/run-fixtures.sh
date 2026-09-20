#!/bin/sh
# Runs RunTests.sh over the 142 fixtures and fails ONLY if the set of broken
# cases changed with respect to known-failures.txt.
#
# Plain RunTests.sh exits non-zero on any failure, and there are 10 known ones
# that are not ours (see docs/superpowers/specs/2026-09-19-baseline.txt). That
# would leave make check permanently red, and an always-red suite gets
# ignored. This way it fails only when a new case shows up -- or when a known
# one starts passing, which we also want to hear about.
set -e
cd "$(dirname "$0")"

# Without MIBDIRS, net-snmp does not find this distribution's MIBs and
# anything with a symbolic OID fails -- which reads as a dozen "regressions"
# that are not. make check passes it through TESTS_ENVIRONMENT, but the script
# has to work when run by hand too.
: "${MIBDIRS:=../mibs:../mibs/iana:../mibs/ietf}"
export MIBDIRS

KNOWN=known-failures.txt
GOT=$(mktemp)
trap 'rm -f "$GOT"' EXIT

# To a file rather than through a pipe: a pipe keeps the output buffered
# until the end, so there is no way to watch the progress of a ~10 minute run
# nor to diagnose one that gets cut off halfway.
LOG=RunTests.log
./RunTests.sh --allow-failed=TRUE > "$LOG" 2>&1
grep -oE "Test [A-Za-z0-9_]+ (failed|created)" "$LOG" \
  | awk '{print $2}' | sort -u > "$GOT"

NEW=$(comm -13 "$KNOWN" "$GOT")
GONE=$(comm -23 "$KNOWN" "$GOT")

if [ -n "$NEW" ]; then
  echo "REGRESSION: fixtures that used to pass and now fail:"
  echo "$NEW" | sed 's/^/  /'
fi
if [ -n "$GONE" ]; then
  echo "IMPROVEMENT: fixtures listed in known-failures.txt that now pass:"
  echo "$GONE" | sed 's/^/  /'
  echo "  (drop them from $KNOWN so the improvement is locked in)"
fi
[ -z "$NEW" ] && [ -z "$GONE" ] || exit 1

echo "ok - $(( 142 - $(wc -l < "$KNOWN") ))/142 fixtures, and the $(wc -l < "$KNOWN") broken ones are the known set"
