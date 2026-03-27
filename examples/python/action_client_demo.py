#!/usr/bin/env python3
"""
Minimal HaruhiDB Python client demo.

Usage:
  python3 examples/python/action_client_demo.py

Optional environment variables:
  HARUHIDB_BASE     default: http://127.0.0.1:8080
  HARUHIDB_DB_PATH  optional db_path override
"""

import json
import os
import uuid

import requests

BASE = os.getenv("HARUHIDB_BASE", "http://127.0.0.1:8080").rstrip("/")
DB_PATH = os.getenv("HARUHIDB_DB_PATH", "").strip()


def execute(action, args, mode="read_only", request_id=None, db_path=None, timeout=10):
    if request_id is None:
        request_id = f"demo-{uuid.uuid4().hex[:8]}"

    envelope = {
        "version": "v3",
        "request_id": request_id,
        "mode": mode,
        "action": action,
        "args": args,
    }
    payload = envelope if not db_path else {"db_path": db_path, **envelope}

    resp = requests.post(f"{BASE}/v1/action", json=payload, timeout=timeout)
    body = resp.json()
    if resp.status_code != 200:
        raise RuntimeError(
            f"HTTP {resp.status_code}: {json.dumps(body, ensure_ascii=False)}"
        )
    return body


def pretty_print(title, payload):
    print(f"\n=== {title} ===")
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def main():
    table = f"characters_demo_{uuid.uuid4().hex[:6]}"
    index = f"idx_{table}_id"
    db_path = DB_PATH or None

    pretty_print(
        "list_tables",
        execute("list_tables", {}, mode="read_only", request_id="demo-list", db_path=db_path),
    )

    pretty_print(
        "batch(create_table + create_primary_int_index)",
        execute(
            "batch",
            {
                "stop_on_error": True,
                "requests": [
                    {
                        "action": "create_table",
                        "args": {
                            "table": table,
                            "columns": [
                                {"name": "id", "type": "INTEGER", "nullable": False},
                                {
                                    "name": "name",
                                    "type": "VARCHAR",
                                    "length": 64,
                                    "nullable": False,
                                },
                            ],
                        },
                    },
                    {
                        "action": "create_primary_int_index",
                        "args": {"table": table, "index": index},
                    },
                ],
            },
            mode="read_write",
            request_id="demo-init",
            db_path=db_path,
        ),
    )

    pretty_print(
        "insert_row",
        execute(
            "insert_row",
            {"table": table, "values": {"id": 1, "name": "haruhi"}},
            mode="read_write",
            request_id="demo-insert",
            db_path=db_path,
        ),
    )

    pretty_print(
        "get_by_primary_int",
        execute(
            "get_by_primary_int",
            {"table": table, "key": 1},
            mode="read_only",
            request_id="demo-get",
            db_path=db_path,
        ),
    )


if __name__ == "__main__":
    main()
