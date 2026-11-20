#!/bin/sh
# make_test_certs.sh - make the certificates the TLS tests and example use.
#
#   sh tests/make_test_certs.sh
#
# writes tests/tls_certs.h (PEM text as C strings, for test_tls) and the PEM
# files in examples/10_tls/certs. Needs the openssl command. The keys are
# for testing only: they are published with the source.
#
# Made:
#   ca         a certificate authority the tests trust
#   server     signed by ca, for localhost, 127.0.0.1 and ::1
#   client     signed by ca, for client authentication (mutual TLS)
#   other-ca   an authority the tests do not trust
#   stranger   a server certificate signed by other-ca
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
DAYS=36500   # 100 years

key() {
    openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$WORK/$1.key" 2>/dev/null
}

# ca NAME SUBJECT
ca() {
    key "$1"
    openssl req -new -x509 -key "$WORK/$1.key" -out "$WORK/$1.pem" -days $DAYS \
        -subj "$2" -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
}

# leaf NAME ISSUER SUBJECT EXTENSIONS
leaf() {
    key "$1"
    openssl req -new -key "$WORK/$1.key" -out "$WORK/$1.csr" -subj "$3" 2>/dev/null
    printf '%s\n' "$4" > "$WORK/$1.ext"
    openssl x509 -req -in "$WORK/$1.csr" -CA "$WORK/$2.pem" -CAkey "$WORK/$2.key" \
        -CAcreateserial -out "$WORK/$1.pem" -days $DAYS -extfile "$WORK/$1.ext" 2>/dev/null
}

SERVER_EXT='basicConstraints=CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1'
CLIENT_EXT='basicConstraints=CA:FALSE
keyUsage=critical,digitalSignature
extendedKeyUsage=clientAuth'

ca ca "/O=liboncrpc tests/CN=liboncrpc test CA"
leaf server ca "/O=liboncrpc tests/CN=localhost" "$SERVER_EXT"
leaf client ca "/O=liboncrpc tests/CN=test client" "$CLIENT_EXT"
ca other-ca "/O=somebody else/CN=untrusted CA"
leaf stranger other-ca "/O=somebody else/CN=localhost" "$SERVER_EXT"

# PEM file -> C string literal lines
cstring() {
    sed -e 's/^/    "/' -e 's/$/\\n"/' "$1"
}

OUT="$ROOT/tests/tls_certs.h"
{
    echo "/* tls_certs.h - test certificates and keys, made by tests/make_test_certs.sh."
    echo " * For testing only: the private keys are public. */"
    echo "#ifndef TLS_CERTS_H"
    echo "#define TLS_CERTS_H"
    for n in ca server client other-ca stranger; do
        id=$(echo "$n" | tr '-' '_' | tr 'a-z' 'A-Z')
        echo ""
        echo "static const char TEST_${id}_CERT[] ="
        cstring "$WORK/$n.pem"
        echo "    ;"
        if [ "$n" != ca ] && [ "$n" != other-ca ]; then
            echo "static const char TEST_${id}_KEY[] ="
            cstring "$WORK/$n.key"
            echo "    ;"
        fi
    done
    echo ""
    echo "#endif"
} > "$OUT"

# The example gets the same kinds of files, from the same authority.
CERTS="$ROOT/examples/10_tls/certs"
mkdir -p "$CERTS"
cp "$WORK/ca.pem" "$CERTS/ca.pem"
cp "$WORK/server.pem" "$CERTS/server.pem"
cp "$WORK/server.key" "$CERTS/server.key"
cp "$WORK/client.pem" "$CERTS/client.pem"
cp "$WORK/client.key" "$CERTS/client.key"

# The project's files must never contain one particular three-letter word
# in any form; random base64 can spell it by chance, so start again
# with new keys when it does.
WORD="s""un"
if grep -qi "$WORD" "$OUT" "$CERTS"/*; then
    exec sh "$0"
fi

echo "wrote $OUT and $CERTS"
