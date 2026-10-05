"""Cifra AES-256-ECB (usada para regravar blocos e cabeçalhos de NeFS)."""

import unittest

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


if __name__ == "__main__":
    unittest.main()
