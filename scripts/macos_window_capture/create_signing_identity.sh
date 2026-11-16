#!/usr/bin/env bash
# Create a stable, local-only code-signing identity for OpenageWindowCapture.
# Unlike ad-hoc signing, its designated requirement survives binary rebuilds,
# so macOS TCC permissions remain attached to the same code identity.
set -euo pipefail

NAME="Openage Window Capture Local"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

if security find-identity -v -p codesigning | grep -F "\"$NAME\"" >/dev/null; then
	printf 'Signing identity already available: %s\n' "$NAME"
	exit 0
fi
if security find-certificate -c "$NAME" "$KEYCHAIN" >/dev/null 2>&1; then
	printf 'Certificate exists but its private key is unavailable: %s\n' "$NAME" >&2
	printf 'Remove that certificate from Keychain Access before retrying.\n' >&2
	exit 1
fi

TMP="$(mktemp -d "${TMPDIR%/}/openage-window-capture-cert.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
chmod 700 "$TMP"

openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
	-subj "/CN=$NAME/" \
	-addext 'basicConstraints=critical,CA:FALSE' \
	-addext 'keyUsage=critical,digitalSignature' \
	-addext 'extendedKeyUsage=critical,codeSigning' \
	-keyout "$TMP/key.pem" \
	-out "$TMP/cert.pem" \
	>/dev/null 2>&1

PASS="$(openssl rand -hex 24)"
openssl pkcs12 -export \
	-inkey "$TMP/key.pem" \
	-in "$TMP/cert.pem" \
	-out "$TMP/identity.p12" \
	-passout "pass:$PASS" \
	>/dev/null 2>&1

security import "$TMP/identity.p12" \
	-k "$KEYCHAIN" \
	-f pkcs12 \
	-P "$PASS" \
	-T /usr/bin/codesign \
	-T /usr/bin/security

# Trust this self-signed certificate ONLY for local code signing. Without a
# user trust setting, Security.framework imports the identity but
# `security find-identity -p codesigning` does not expose it to codesign.
security add-trusted-cert \
	-r trustRoot \
	-p codeSign \
	-k "$KEYCHAIN" \
	"$TMP/cert.pem"

if ! security find-identity -v -p codesigning | grep -F "\"$NAME\"" >/dev/null; then
	printf 'The certificate was imported, but macOS did not expose it for code signing.\n' >&2
	exit 1
fi

printf 'Created local signing identity: %s\n' "$NAME"
printf 'Private key location: login keychain (not stored in the repository).\n'
