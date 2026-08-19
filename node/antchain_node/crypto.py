"""Key generation, signing, and address derivation.

Uses the `cryptography` library's ECDSA over SECP256k1 (the same curve
Bitcoin/Ethereum use) -- real, audited primitives, not homemade crypto.
Addresses are sha256(compressed_pubkey)[:20 bytes], hex-encoded with an
"ant1" prefix, analogous to a simplified Bitcoin/Ethereum address scheme.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.exceptions import InvalidSignature

CURVE = ec.SECP256K1()


def _compressed_point_bytes(pub: ec.EllipticCurvePublicKey) -> bytes:
    return pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.CompressedPoint)


def address_from_pubkey(pub: ec.EllipticCurvePublicKey) -> str:
    digest = hashlib.sha256(_compressed_point_bytes(pub)).digest()
    return "ant1" + digest[:20].hex()


def pubkey_to_hex(pub: ec.EllipticCurvePublicKey) -> str:
    return _compressed_point_bytes(pub).hex()


def pubkey_from_hex(pub_hex: str) -> ec.EllipticCurvePublicKey:
    return ec.EllipticCurvePublicKey.from_encoded_point(CURVE, bytes.fromhex(pub_hex))


@dataclass
class Wallet:
    private_key: ec.EllipticCurvePrivateKey
    public_key: ec.EllipticCurvePublicKey

    @property
    def address(self) -> str:
        return address_from_pubkey(self.public_key)

    @property
    def pubkey_hex(self) -> str:
        return pubkey_to_hex(self.public_key)

    def sign(self, message: bytes) -> str:
        sig = self.private_key.sign(message, ec.ECDSA(hashes.SHA256()))
        return sig.hex()

    @staticmethod
    def generate() -> "Wallet":
        sk = ec.generate_private_key(CURVE)
        return Wallet(private_key=sk, public_key=sk.public_key())

    def to_dict(self) -> dict:
        priv_bytes = self.private_key.private_numbers().private_value.to_bytes(32, "big")
        return {"private_key_hex": priv_bytes.hex(), "public_key_hex": self.pubkey_hex, "address": self.address}

    @staticmethod
    def from_dict(d: dict) -> "Wallet":
        priv_int = int(d["private_key_hex"], 16)
        sk = ec.derive_private_key(priv_int, CURVE)
        return Wallet(private_key=sk, public_key=sk.public_key())


def verify_signature(pub_hex: str, message: bytes, signature_hex: str) -> bool:
    try:
        pub = pubkey_from_hex(pub_hex)
        pub.verify(bytes.fromhex(signature_hex), message, ec.ECDSA(hashes.SHA256()))
        return True
    except (InvalidSignature, ValueError):
        return False


def canonical_json(obj: dict) -> bytes:
    """Deterministic JSON encoding used wherever bytes are signed/hashed."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8")
