#!/usr/bin/env bash
# Generates OpenSSL RSA 2048-bit PKI keys for RAUC bundle signing and target verification

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${SCRIPT_DIR}/../layers/meta-selfhealing/recipes-core/rauc/files"

mkdir -p "${TARGET_DIR}"
cd "${TARGET_DIR}"

echo "[*] Generating RAUC Development PKI Keys in ${TARGET_DIR}..."

# 1. Generate Root CA Key & Certificate
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key.pem -out ca.cert.pem -days 3650 \
    -subj "/C=DE/ST=Bavaria/L=Munich/O=SelfHealingDistro/CN=selfhealing-rauc-ca"

# 2. Generate Development Signing Key & CSR
openssl req -newkey rsa:2048 -nodes -keyout development-1.key.pem -out development-1.csr.pem \
    -subj "/C=DE/ST=Bavaria/L=Munich/O=SelfHealingDistro/CN=selfhealing-development-1"

# 3. Sign Development Key with Root CA
openssl x509 -req -in development-1.csr.pem -CA ca.cert.pem -CAkey ca.key.pem \
    -CAcreateserial -out development-1.cert.pem -days 1825

# Clean up CSR & serial
rm -f development-1.csr.pem ca.srl

echo "[+] Keys generated successfully:"
echo "    - CA Public Certificate (installed on target): ca.cert.pem"
echo "    - Signing Certificate (used by host bitbake):   development-1.cert.pem"
echo "    - Signing Private Key (kept safe on host):     development-1.key.pem"

