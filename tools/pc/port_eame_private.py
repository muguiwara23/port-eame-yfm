#!/usr/bin/env python3
"""Port/eame: keep a private file (Arturo's own app icon) in the public fork
only as ciphertext.

    port_eame_private.py encrypt IN OUT KEY
    port_eame_private.py decrypt IN OUT KEY

The key never enters the repository: the iOS workflow takes it from the
PORTEAME_ICON_KEY secret. SHA-256 in counter mode over a PBKDF2 key, with
an HMAC that refuses a wrong key; Python's standard library only."""
import hashlib
import hmac
import os
import sys

MAGIC = b"PORTEAME1"


def keys(passphrase, salt):
    material = hashlib.pbkdf2_hmac("sha256", passphrase.encode(), salt, 200_000, 64)
    return material[:32], material[32:]


def stream(key, nonce, size):
    out = bytearray()
    counter = 0
    while len(out) < size:
        out += hashlib.sha256(key + nonce + counter.to_bytes(8, "big")).digest()
        counter += 1
    return bytes(out[:size])


def encrypt(data, passphrase):
    salt, nonce = os.urandom(16), os.urandom(16)
    cipher_key, mac_key = keys(passphrase, salt)
    body = bytes(a ^ b for a, b in zip(data, stream(cipher_key, nonce, len(data))))
    tag = hmac.new(mac_key, MAGIC + salt + nonce + body, hashlib.sha256).digest()
    return MAGIC + salt + nonce + tag + body


def decrypt(blob, passphrase):
    if not blob.startswith(MAGIC):
        raise SystemExit("Not a Port/eame private file")
    salt, nonce, tag, body = blob[9:25], blob[25:41], blob[41:73], blob[73:]
    cipher_key, mac_key = keys(passphrase, salt)
    if not hmac.compare_digest(tag, hmac.new(mac_key, MAGIC + salt + nonce + body, hashlib.sha256).digest()):
        raise SystemExit("Wrong key for the private file")
    return bytes(a ^ b for a, b in zip(body, stream(cipher_key, nonce, len(body))))


def main():
    if len(sys.argv) != 5 or sys.argv[1] not in ("encrypt", "decrypt"):
        raise SystemExit(__doc__)
    action, source, target, passphrase = sys.argv[1:]
    data = open(source, "rb").read()
    out = encrypt(data, passphrase) if action == "encrypt" else decrypt(data, passphrase)
    open(target, "wb").write(out)
    print(f"{action}ed {source} -> {target} ({len(out)} bytes)")


if __name__ == "__main__":
    main()
