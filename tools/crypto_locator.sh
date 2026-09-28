#!/bin/bash
set -eu
root=${1:?usage: crypto_locator.sh ROOT OUTPUT}
out=${2:?usage: crypto_locator.sh ROOT OUTPUT}
{
    find "$root" -type f \( -iname '*.pem' -o -iname '*.crt' -o -iname '*.cer' -o -iname '*.der' -o -iname '*.key' -o -iname '*.sig' \) -printf '%s\t%p\n'
    rg -l -a -i 'BEGIN (RSA |EC |DSA )?(PRIVATE|PUBLIC) KEY|BEGIN CERTIFICATE|DssVerify|RSA_verify|EVP_Verify|secure.?boot|signature check' "$root" 2>/dev/null
} > "$out"
