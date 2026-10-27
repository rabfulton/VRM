"""Endpoint-scoped credentials in the operating system's credential service."""
from __future__ import annotations

import hashlib
from urllib.parse import urlsplit, urlunsplit


class CredentialError(RuntimeError):
    pass


def normalize_endpoint(value: str) -> str:
    try:
        url = urlsplit(value.strip())
        host = url.hostname
        port = url.port
    except (ValueError, AttributeError):
        raise ValueError("Enter a valid chat completions URL.") from None
    if (url.scheme not in ("https", "http") or not host or url.username is not None
            or url.password is not None or url.query or url.fragment):
        raise ValueError("Use an HTTPS chat completions URL without credentials, query or fragment.")
    if url.scheme == "http" and host.lower() not in ("localhost", "127.0.0.1", "::1"):
        raise ValueError("HTTP is supported only for local servers; use HTTPS for a remote API.")
    authority = f"[{host.lower()}]" if ":" in host else host.lower()
    if port is not None and port != (443 if url.scheme == "https" else 80):
        authority += f":{port}"
    return urlunsplit((url.scheme, authority, url.path.rstrip("/") or "/", "", ""))


def is_local_endpoint(endpoint: str) -> bool:
    return urlsplit(normalize_endpoint(endpoint)).hostname in ("localhost", "127.0.0.1", "::1")


class SystemCredentials:
    SERVICE = "vrm-overlay.chat"
    # Never let a chainer silently choose a plaintext/file fallback.
    OS_BACKENDS = {"keyring.backends.SecretService", "keyring.backends.kwallet",
                   "keyring.backends.Windows", "keyring.backends.macOS"}

    @classmethod
    def _backend(cls):
        try:
            import keyring
            backend = keyring.get_keyring()
            candidates = backend.backends if type(backend).__module__ == "keyring.backends.chainer" else [backend]
            for candidate in candidates:
                module = type(candidate).__module__
                if any(module == name or module.startswith(name + ".") for name in cls.OS_BACKENDS):
                    if candidate.priority > 0:
                        return candidate
        except Exception:
            pass
        raise CredentialError("System keyring unavailable. Install keyring and unlock your desktop's Secret Service/KWallet, Keychain or Credential Manager.")

    @staticmethod
    def account(endpoint):
        return hashlib.sha256(normalize_endpoint(endpoint).encode()).hexdigest()

    def get(self, endpoint):
        try:
            return self._backend().get_password(self.SERVICE, self.account(endpoint)) or ""
        except CredentialError:
            raise
        except Exception:
            raise CredentialError("Could not read the system keyring. Unlock it and try again.") from None

    def set(self, endpoint, secret):
        if not isinstance(secret, str) or not secret.strip() or any(c in secret for c in "\r\n\0"):
            raise CredentialError("Enter a valid API key.")
        try:
            backend = self._backend()
            account = self.account(endpoint)
            backend.set_password(self.SERVICE, account, secret.strip())
            if backend.get_password(self.SERVICE, account) != secret.strip():
                raise CredentialError("The system keyring did not confirm storage; the key was not saved to settings.")
        except CredentialError:
            raise
        except Exception:
            # Backend errors can contain arguments; do not expose them to logs/UI.
            raise CredentialError("Could not save to the system keyring. Unlock it and try again.") from None

    def delete(self, endpoint):
        try:
            backend = self._backend()
            account = self.account(endpoint)
            if backend.get_password(self.SERVICE, account) is not None:
                backend.delete_password(self.SERVICE, account)
        except CredentialError:
            raise
        except Exception:
            raise CredentialError("Could not remove the API key from the system keyring.") from None
