"""Cifra AES-256-ECB (usada para regravar blocos e cabeçalhos de NeFS)."""

import unittest

import os

from tools.egodata import aes
from tools.egodata.aes import decrypt_ecb, encrypt_ecb


class AesEncryptTest(unittest.TestCase):
    def test_fips197_aes256_vector(self):
        key = bytes(range(32))
        plain = bytes.fromhex("00112233445566778899aabbccddeeff")
        self.assertEqual(encrypt_ecb(plain, key).hex(), "8ea2b7ca516745bfeafc49904b496089")

    def test_roundtrip_leaves_partial_tail(self):
        key = bytes(range(32))
        data = bytes(range(50))
        out = encrypt_ecb(data, key)
        self.assertEqual(out[48:], data[48:])
        self.assertEqual(decrypt_ecb(out, key), data)

    def test_native_matches_pure_python(self):
        if aes._NATIVE is None:
            self.skipTest("libcrypto indisponível")
        for size in (16, 24, 32):
            key, data = os.urandom(size), os.urandom(16 * 40 + 5)
            self.assertEqual(encrypt_ecb(data, key), aes._py_encrypt_ecb(data, key))
            self.assertEqual(decrypt_ecb(data, key), aes._py_decrypt_ecb(data, key))


if __name__ == "__main__":
    unittest.main()
