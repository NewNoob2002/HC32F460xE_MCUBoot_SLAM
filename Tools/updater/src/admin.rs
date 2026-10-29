//! Administrator challenge-response signing for Boot recovery.
//!
//! The device sends a random 32-byte challenge and accepts the raw r||s ECDSA-P256 signature of
//! SHA-256(`ADMIN_DOMAIN` || challenge). The administrator key is independent of the image-signing key.

use crate::Error;
use p256::SecretKey;
use p256::ecdsa::signature::Signer;
use p256::ecdsa::{Signature, SigningKey};
use p256::pkcs8::DecodePrivateKey;
use sha2::{Digest, Sha256};
use std::fmt::Write;

/// Must match `FW_UPDATE_ADMIN_DOMAIN` in `components/fw_update/include/fw_update/admin_auth.h`.
pub const ADMIN_DOMAIN: &[u8] = b"HC32-FWUP-ADMIN-V1";
pub const CHALLENGE_SIZE: usize = 32;
pub const SIGNATURE_SIZE: usize = 64;

pub struct AdminKey {
    signing: SigningKey,
}

impl AdminKey {
    /// Accepts PKCS#8 (`BEGIN PRIVATE KEY`) or SEC1 (`BEGIN EC PRIVATE KEY`) P-256 private keys.
    pub fn from_pem(text: &str) -> Result<Self, Error> {
        let secret = SecretKey::from_pkcs8_pem(text)
            .or_else(|_| SecretKey::from_sec1_pem(text))
            .map_err(|_| {
                Error::Argument("administrator key must be an ECDSA-P256 private key PEM")
            })?;
        Ok(Self {
            signing: SigningKey::from(secret),
        })
    }

    /// RFC 6979 deterministic signature, so no host entropy is needed.
    pub fn sign_challenge(&self, challenge: &[u8]) -> Result<[u8; SIGNATURE_SIZE], Error> {
        if challenge.len() != CHALLENGE_SIZE {
            return Err(Error::Argument("administrator challenge must be 32 bytes"));
        }
        let mut message = Vec::with_capacity(ADMIN_DOMAIN.len() + CHALLENGE_SIZE);
        message.extend_from_slice(ADMIN_DOMAIN);
        message.extend_from_slice(challenge);
        let signature: Signature = self.signing.sign(&message);
        Ok(signature.to_bytes().into())
    }

    /// First 8 bytes of SHA-256 over the uncompressed public point, for showing which key is loaded.
    pub fn fingerprint(&self) -> String {
        let point = self.signing.verifying_key().to_encoded_point(false);
        Sha256::digest(point.as_bytes())[..8]
            .iter()
            .fold(String::new(), |mut text, byte| {
                let _ = write!(text, "{byte:02x}");
                text
            })
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use p256::ecdsa::VerifyingKey;
    use p256::ecdsa::signature::Verifier;

    /// Test-only key (private scalar 0x01..0x20). Tests/fw_update_admin_auth_tests.c verifies the signature this
    /// key makes over challenge 0x40..0x5f, so host signing and device verification are checked against each other.
    pub(crate) const TEST_KEY: &str = "-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgAQIDBAUGBwgJCgsM
DQ4PEBESExQVFhcYGRobHB0eHyChRANCAARRXD1uueOWuQTT/sp/VP3NDMHpl783
XcpRWtCmw7QDX0U2vjpQ8xj7+aVHWQKiIVAr7w1X4IxTsswKVvF9n5NU
-----END PRIVATE KEY-----
";

    pub(crate) fn test_public_point() -> Vec<u8> {
        let key = AdminKey::from_pem(TEST_KEY).unwrap();
        key.signing
            .verifying_key()
            .to_encoded_point(false)
            .as_bytes()
            .to_vec()
    }

    #[test]
    fn signs_the_domain_separated_challenge() {
        let key = AdminKey::from_pem(TEST_KEY).unwrap();
        let challenge: Vec<u8> = (0x40..0x60).collect();
        let signature = key.sign_challenge(&challenge).unwrap();
        let mut message = ADMIN_DOMAIN.to_vec();
        message.extend_from_slice(&challenge);
        let verifying = VerifyingKey::from(&key.signing);
        let parsed = Signature::from_slice(&signature).unwrap();
        assert!(verifying.verify(&message, &parsed).is_ok());
        assert!(verifying.verify(&challenge, &parsed).is_err());
        // Deterministic signing keeps the device test vector stable.
        assert_eq!(signature, key.sign_challenge(&challenge).unwrap());
        assert!(key.sign_challenge(&challenge[..31]).is_err());
        assert_eq!(key.fingerprint().len(), 16);
    }

    #[test]
    fn rejects_non_p256_or_public_keys() {
        assert!(AdminKey::from_pem("not a key").is_err());
        let public = "-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEUVw9brnjlrkE0/7Kf1T9zQzB6Ze/
N13KUVrQpsO0A19FNr46UPMY+/mlR1kCoiFQK+8NV+CMU7LMClbxfZ+TVA==
-----END PUBLIC KEY-----
";
        assert!(AdminKey::from_pem(public).is_err());
    }
}
