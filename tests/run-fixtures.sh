#!/bin/sh
# Corre RunTests.sh sobre los 142 fixtures y falla SOLO si el conjunto de
# casos rotos cambió respecto de known-failures.txt.
#
# RunTests.sh a secas sale con error ante cualquier fallo, y hay 10 conocidos
# que no son nuestros (ver docs/superpowers/specs/2026-09-19-baseline.txt).
# Eso dejaría make check permanentemente en rojo, y una suite siempre roja se
# ignora. Así falla solo cuando aparece un caso nuevo -- o cuando uno conocido
# se arregla, que también queremos saber.
set -e
cd "$(dirname "$0")"

# Sin MIBDIRS net-snmp no encuentra los MIBs de esta distribución y todo lo
# que tenga un OID simbólico falla -- lo que se ve como una decena de
# "regresiones" que no lo son. make check lo pasa por TESTS_ENVIRONMENT, pero
# el script tiene que servir también corrido a mano.
: "${MIBDIRS:=../mibs:../mibs/iana:../mibs/ietf}"
export MIBDIRS

KNOWN=known-failures.txt
GOT=$(mktemp)
trap 'rm -f "$GOT"' EXIT

# A un archivo y no por pipe: el pipe deja la salida bufferizada hasta el
# final, así que no se puede ver el progreso de una corrida de ~10 minutos ni
# diagnosticar una que se corta a la mitad.
LOG=RunTests.log
./RunTests.sh --allow-failed=TRUE > "$LOG" 2>&1
grep -oE "Test [A-Za-z0-9_]+ (failed|created)" "$LOG" \
  | awk '{print $2}' | sort -u > "$GOT"

NEW=$(comm -13 "$KNOWN" "$GOT")
GONE=$(comm -23 "$KNOWN" "$GOT")

if [ -n "$NEW" ]; then
  echo "REGRESIÓN: fixtures que antes pasaban y ahora fallan:"
  echo "$NEW" | sed 's/^/  /'
fi
if [ -n "$GONE" ]; then
  echo "MEJORA: fixtures que estaban en known-failures.txt y ahora pasan:"
  echo "$GONE" | sed 's/^/  /'
  echo "  (sacalos de $KNOWN para que la mejora quede fijada)"
fi
[ -z "$NEW" ] && [ -z "$GONE" ] || exit 1

echo "ok - $(( 142 - $(wc -l < "$KNOWN") ))/142 fixtures, y los $(wc -l < "$KNOWN") rotos son los conocidos"
