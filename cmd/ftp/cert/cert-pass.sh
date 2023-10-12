#!/bin/sh
echo "Making Certificate Signing Request (CSR)"
echo "and RSA Private Key (protected by pass phrase)"
echo "=============================================="

touch newcert-key.pem
chmod 600 newcert-key.pem
openssl req -new -days 365 -out newcert-csr.pem -keyout newcert-key.pem
