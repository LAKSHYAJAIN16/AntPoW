"""Account-model transactions: sender, recipient, amount, sender nonce
(replay protection), ECDSA signature. No fees, no UTXO -- kept minimal since
the point of this project is the consensus mechanism, not payment-layer
design."""
from __future__ import annotations

from dataclasses import dataclass

from .crypto import canonical_json, verify_signature, address_from_pubkey, pubkey_from_hex


@dataclass
class Transaction:
    sender: str
    sender_pubkey: str
    recipient: str
    amount: float
    nonce: int
    signature: str = ""

    def signing_payload(self) -> dict:
        return {
            "sender": self.sender,
            "recipient": self.recipient,
            "amount": self.amount,
            "nonce": self.nonce,
        }

    def signing_bytes(self) -> bytes:
        return canonical_json(self.signing_payload())

    @property
    def tx_hash(self) -> str:
        import hashlib
        payload = dict(self.signing_payload())
        payload["signature"] = self.signature
        return hashlib.sha256(canonical_json(payload)).hexdigest()

    def sign(self, wallet) -> None:
        self.signature = wallet.sign(self.signing_bytes())

    def verify(self) -> bool:
        if self.amount <= 0:
            return False
        try:
            if address_from_pubkey(pubkey_from_hex(self.sender_pubkey)) != self.sender:
                return False
        except ValueError:
            return False
        return verify_signature(self.sender_pubkey, self.signing_bytes(), self.signature)

    def to_dict(self) -> dict:
        d = self.signing_payload()
        d["sender_pubkey"] = self.sender_pubkey
        d["signature"] = self.signature
        return d

    @staticmethod
    def from_dict(d: dict) -> "Transaction":
        return Transaction(
            sender=d["sender"],
            sender_pubkey=d["sender_pubkey"],
            recipient=d["recipient"],
            amount=d["amount"],
            nonce=d["nonce"],
            signature=d.get("signature", ""),
        )
