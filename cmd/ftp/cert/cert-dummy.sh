#!/bin/sh
echo "Making self-signed certificate"
echo "and RSA Private Key"
echo "=============================="

touch cert-dummy.pem
chmod 600 cert-dummy.pem
openssl req -new -x509 -nodes -days 365 -out cert-dummy.pem -keyout cert-dummy.pem
