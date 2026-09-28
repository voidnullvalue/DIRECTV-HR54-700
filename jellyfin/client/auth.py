"""Jellyfin user authentication and private persistent device state.

The requesting side holds the Quick Connect secret. Nothing in this module
prints an access token or places one in a playback URL.
"""

import json
import os
import secrets
import urllib.parse
import urllib.request
from pathlib import Path


class JellyfinAuthError(RuntimeError):
    pass


class DeviceState:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        os.chmod(self.directory, 0o700)
        self.config_path = self.directory / "config.json"
        self.token_path = self.directory / "token"

    def _atomic_write(self, path, content):
        temporary = self.directory / (".write-" + secrets.token_hex(8))
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
            os.chmod(path, 0o600)
        finally:
            temporary.unlink(missing_ok=True)

    def load(self):
        if self.config_path.exists():
            config = json.loads(self.config_path.read_text())
        else:
            config = {}
        if not config.get("device_id"):
            config["device_id"] = secrets.token_hex(16)
            self.save_config(config)
        token = self.token_path.read_text().strip() if self.token_path.exists() else None
        return config, token

    def save_config(self, config):
        safe = {key: config[key] for key in
                ("server", "device_id", "user_id", "user_name") if key in config}
        self._atomic_write(self.config_path,
                           (json.dumps(safe, separators=(",", ":")) + "\n").encode())

    def save_login(self, config, token):
        if not token:
            raise JellyfinAuthError("empty access token")
        self._atomic_write(self.token_path, (token + "\n").encode())
        self.save_config(config)

    def logout(self):
        self.token_path.unlink(missing_ok=True)
        config, _ = self.load()
        config.pop("user_id", None)
        config.pop("user_name", None)
        self.save_config(config)


class JellyfinAuth:
    def __init__(self, server, device_id, token=None):
        parsed = urllib.parse.urlsplit(server)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            raise ValueError("server must be an HTTP(S) URL")
        self.server = server.rstrip("/")
        self.device_id = device_id
        self.token = token

    def _request(self, path, payload=None, token=None):
        auth = ('MediaBrowser Client="HR54 Jellyfin", Device="DIRECTV HR54", '
                f'DeviceId="{self.device_id}", Version="0.1"')
        access_token = self.token if token is None else token
        if access_token:
            auth += f', Token="{access_token}"'
        headers = {"Authorization": auth, "Accept": "application/json"}
        body = None
        if payload is not None:
            headers["Content-Type"] = "application/json"
            body = json.dumps(payload).encode()
        request = urllib.request.Request(
            self.server + "/" + path.lstrip("/"), data=body, headers=headers,
            method="POST" if payload is not None else "GET")
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                return json.load(response)
        except Exception as error:
            # HTTP errors may include server bodies with private data. Do not
            # propagate the request URL, headers, or response body to logs.
            raise JellyfinAuthError(type(error).__name__) from None

    def quick_connect_enabled(self):
        return self._request("QuickConnect/Enabled") is True

    def start_quick_connect(self):
        result = self._request("QuickConnect/Initiate", {})
        if not result.get("Secret") or not result.get("Code"):
            raise JellyfinAuthError("Quick Connect response lacks secret or code")
        return result

    def quick_connect_status(self, secret):
        return self._request("QuickConnect/Connect?Secret=" +
                             urllib.parse.quote(secret, safe=""))

    def finish_quick_connect(self, secret):
        result = self._request("Users/AuthenticateWithQuickConnect",
                               {"Secret": secret})
        if not result.get("AccessToken") or not result.get("User", {}).get("Id"):
            raise JellyfinAuthError("Quick Connect did not return a user token")
        return result

    def authenticate_by_name(self, username, password):
        result = self._request("Users/AuthenticateByName",
                               {"Username": username, "Pw": password})
        if not result.get("AccessToken"):
            raise JellyfinAuthError("username login did not return a token")
        return result

    def authorize_quick_connect(self, code, authorizer_token):
        path = "QuickConnect/Authorize?Code=" + urllib.parse.quote(code, safe="")
        return self._request(path, {}, token=authorizer_token)

    def current_user(self):
        return self._request("Users/Me")
