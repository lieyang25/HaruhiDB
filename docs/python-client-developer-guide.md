# HaruhiDB Python Client Developer Guide

This document is a minimal developer guide for integrating with HaruhiDB via Python.
It focuses on one endpoint: `POST /v1/action`.

## 1. Endpoint

- Method: `POST`
- URL: `http://127.0.0.1:8080/v1/action`
- Header: `Content-Type: application/json`

## 2. Request Format

`/v1/action` accepts two payload shapes.

### 2.1 Action envelope only

```json
{
  "version": "v3",
  "request_id": "req-001",
  "mode": "read_only",
  "action": "list_tables",
  "args": {}
}
```

### 2.2 Action envelope with `db_path` override

```json
{
  "db_path": "/tmp/haruhidb-demo.db",
  "version": "v3",
  "request_id": "req-001",
  "mode": "read_only",
  "action": "list_tables",
  "args": {}
}
```

Field rules:

- `version`: accepts `v1`/`v2`/`v3` (recommend sending `v3`)
- `request_id`: non-empty string
- `mode`: `read_only` or `read_write`
- `action`: one of supported action names
- `args`: must be a JSON object (`{}` for no-arg actions)
- `db_path`: optional top-level string for runtime DB override

## 3. Response Format

Success response:

```json
{
  "ok": true,
  "request_id": "req-001",
  "action": "list_tables",
  "data": {
    "tables": []
  },
  "error": null,
  "meta": {}
}
```

Action-level failure response:

```json
{
  "ok": false,
  "request_id": "req-001",
  "action": "insert_row",
  "data": null,
  "error": {
    "code": "INVALID_REQUEST",
    "message": "action \"insert_row\" requires mode \"read_write\""
  },
  "meta": {}
}
```

HTTP notes:

- `200`: request reached action layer (`ok` can be `true` or `false`)
- `400`: malformed JSON / invalid top-level request body / invalid `db_path`
- `405`: method not allowed

## 4. Minimal Python Client

```python
import requests

BASE = "http://127.0.0.1:8080"

def execute(action, args, mode="read_only", request_id="demo", db_path=None):
    envelope = {
        "version": "v3",
        "request_id": request_id,
        "mode": mode,
        "action": action,
        "args": args,
    }
    payload = envelope if not db_path else {"db_path": db_path, **envelope}
    resp = requests.post(f"{BASE}/v1/action", json=payload, timeout=10)
    return resp.json()
```

## 5. Call Examples

```python
import uuid

table = f"characters_demo_{uuid.uuid4().hex[:6]}"
index = f"idx_{table}_id"

# 1) list tables
print(execute("list_tables", {}, mode="read_only", request_id="demo-list"))

# 2) create table + index
print(execute(
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
                        {"name": "name", "type": "VARCHAR", "length": 64, "nullable": False}
                    ]
                }
            },
            {
                "action": "create_primary_int_index",
                "args": {"table": table, "index": index}
            }
        ]
    },
    mode="read_write",
    request_id="demo-init"
))

# 3) insert row
print(execute(
    "insert_row",
    {"table": table, "values": {"id": 1, "name": "haruhi"}},
    mode="read_write",
    request_id="demo-insert"
))

# 4) get by primary int
print(execute(
    "get_by_primary_int",
    {"table": table, "key": 1},
    mode="read_only",
    request_id="demo-get"
))
```

For a full runnable sample script, see:

- `examples/python/action_client_demo.py`
