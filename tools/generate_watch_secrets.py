#!/usr/bin/env python3
"""Generate the local, ignored firmware secrets header from Vokrr's .env."""

from __future__ import annotations

import argparse
import os
from pathlib import Path


def load_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[key] = value
    return values


def c_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )


def server_url(env: dict[str, str]) -> str:
    explicit = env.get("IOS_DEFAULT_SERVER_URL") or env.get("VOICE_BACKEND_URL")
    if explicit:
        return explicit.rstrip("/")
    host = env.get("BACKEND_HOST", "")
    port = env.get("BACKEND_PORT", "")
    if host and host not in {"0.0.0.0", "::"}:
        suffix = f":{port}" if port else ""
        return f"http://{host}{suffix}"
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--env",
        type=Path,
        default=Path("/Users/dushyant/Projects/Personal/vokrr/.env"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "firmware/esp-brookesia/main/vokrr_secrets.local.h",
    )
    args = parser.parse_args()

    env = load_env(args.env)
    required = {
        "PRIMARY_SSID": env.get("PRIMARY_SSID", ""),
        "PRIMARY_SSID_PASSWORD": env.get("PRIMARY_SSID_PASSWORD", ""),
        "SECONDARY_SSID": env.get("SECONDARY_SSID", ""),
        "SECONDARY_SSID_PASSWORD": env.get("SECONDARY_SSID_PASSWORD", ""),
        "SERVER_URL": server_url(env),
        "SERVER_USERNAME": env.get("APP_BOOTSTRAP_ADMIN_USERNAME", ""),
        "SERVER_PASSWORD": env.get("APP_BOOTSTRAP_ADMIN_PASSWORD", ""),
    }
    missing = [key for key, value in required.items() if not value]
    if missing:
        raise SystemExit(f"Missing required Vokrr environment values: {', '.join(missing)}")

    text = """/* Generated locally. Never commit this file. */
#pragma once

#define VOKRR_PRIMARY_SSID \"{PRIMARY_SSID}\"
#define VOKRR_PRIMARY_PASSWORD \"{PRIMARY_SSID_PASSWORD}\"
#define VOKRR_SECONDARY_SSID \"{SECONDARY_SSID}\"
#define VOKRR_SECONDARY_PASSWORD \"{SECONDARY_SSID_PASSWORD}\"
#define VOKRR_SERVER_URL \"{SERVER_URL}\"
#define VOKRR_SERVER_USERNAME \"{SERVER_USERNAME}\"
#define VOKRR_SERVER_PASSWORD \"{SERVER_PASSWORD}\"
""".format(**{key: c_string(value) for key, value in required.items()})

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_suffix(args.output.suffix + ".tmp")
    temp.write_text(text, encoding="utf-8")
    os.chmod(temp, 0o600)
    os.replace(temp, args.output)
    print(f"Generated ignored watch configuration at {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
