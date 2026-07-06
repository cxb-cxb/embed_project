#!/usr/bin/env python3
"""Local HTTP proxy for board-side ASR requests.

The board only has BusyBox wget without HTTPS support. This proxy receives
plain HTTP from the board through adb reverse and forwards it to Volcengine ASR.
"""

from __future__ import annotations

import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib import request, error


VOLCENGINE_ASR_ENDPOINT = os.environ.get(
    "VOLCENGINE_ASR_ENDPOINT",
    "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash",
)
VOLCENGINE_ASR_RESOURCE_ID = os.environ.get(
    "VOLCENGINE_ASR_RESOURCE_ID",
    "volc.bigasr.auc_turbo",
)


class Handler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        if self.path != "/asr":
            self.send_error(404, "not found")
            return

        api_key = os.environ.get("VOLCENGINE_ASR_API_KEY")
        if not api_key:
            self.send_error(500, "VOLCENGINE_ASR_API_KEY is not set on PC")
            return

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        request_id = self.headers.get("X-Api-Request-Id", "qsm_proxy_request")
        headers = {
            "Content-Type": "application/json",
            "X-Api-Key": api_key,
            "X-Api-Resource-Id": VOLCENGINE_ASR_RESOURCE_ID,
            "X-Api-Request-Id": request_id,
            "X-Api-Sequence": "-1",
        }
        req = request.Request(VOLCENGINE_ASR_ENDPOINT, data=body, headers=headers, method="POST")
        try:
            with request.urlopen(req, timeout=60) as resp:
                resp_body = resp.read()
                status_code = resp.headers.get("X-Api-Status-Code", "20000000")
                message = resp.headers.get("X-Api-Message", "")
        except error.HTTPError as exc:
            resp_body = exc.read() or json.dumps({"error": str(exc)}).encode("utf-8")
            status_code = exc.headers.get("X-Api-Status-Code", str(exc.code))
            message = exc.headers.get("X-Api-Message", exc.reason)
        except Exception as exc:
            resp_body = json.dumps({"error": str(exc)}, ensure_ascii=False).encode("utf-8")
            status_code = "proxy_error"
            message = str(exc)

        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("X-Api-Status-Code", status_code)
        self.send_header("X-Api-Message", message)
        self.send_header("Content-Length", str(len(resp_body)))
        self.end_headers()
        self.wfile.write(resp_body)

    def log_message(self, fmt: str, *args: object) -> None:
        print("[asr-proxy] " + fmt % args)


def main() -> None:
    host = os.environ.get("QSM_ASR_PROXY_HOST", "127.0.0.1")
    port = int(os.environ.get("QSM_ASR_PROXY_PORT", "8787"))
    print(f"ASR proxy listening on http://{host}:{port}/asr")
    ThreadingHTTPServer((host, port), Handler).serve_forever()


if __name__ == "__main__":
    main()
