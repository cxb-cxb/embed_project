#!/usr/bin/env python3
"""Record from the board microphone, transcribe on the PC, then answer."""

from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import time
import uuid
from pathlib import Path

from voice_retail_assistant import answer_question, load_catalog


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = PROJECT_ROOT / "catalog.json"
DEFAULT_OUTPUT_DIR = Path(r"D:\qsm_embed_dataset\voice_samples")
DEFAULT_VISUAL_STATE = Path(r"D:\qsm_embed_dataset\lvds_overlay_runtime\current_product.json")
VOLCENGINE_ASR_ENDPOINT = os.environ.get(
    "VOLCENGINE_ASR_ENDPOINT",
    "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash",
)
VOLCENGINE_ASR_RESOURCE_ID = os.environ.get(
    "VOLCENGINE_ASR_RESOURCE_ID",
    "volc.bigasr.auc_turbo",
)


def find_adb() -> str:
    winget_adb = (
        Path.home()
        / "AppData"
        / "Local"
        / "Microsoft"
        / "WinGet"
        / "Packages"
        / "Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe"
        / "platform-tools"
        / "adb.exe"
    )
    if winget_adb.exists():
        return str(winget_adb)
    return "adb"


def run(cmd: list[str], *, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, text=True, encoding="utf-8", errors="replace", check=check)


def record_from_board(adb: str, seconds: int, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    remote_wav = f"/tmp/qsm_voice_query_{stamp}.wav"
    local_wav = output_dir / f"qsm_voice_query_{stamp}.wav"
    shell_cmd = (
        "amixer -c 0 cset name='Capture MIC Path' 'Main Mic' >/dev/null; "
        f"arecord -D hw:0,0 -f S16_LE -c 2 -r 16000 -d {seconds} '{remote_wav}'"
    )
    print(f"请对着板载麦克风说话，录音 {seconds} 秒...")
    run([adb, "shell", shell_cmd])
    run([adb, "pull", remote_wav, str(local_wav)])
    print(f"录音文件: {local_wav}")
    return local_wav


def transcribe_with_openai(wav_path: Path, model: str, language: str | None) -> str:
    from openai import OpenAI

    api_key = os.environ.get("RETAIL_ASR_API_KEY") or os.environ.get("OPENAI_API_KEY")
    base_url = os.environ.get("RETAIL_ASR_BASE_URL") or os.environ.get("OPENAI_BASE_URL")
    if not api_key:
        raise RuntimeError("未设置 RETAIL_ASR_API_KEY 或 OPENAI_API_KEY，无法执行 ASR。")

    client_kwargs = {"api_key": api_key}
    if base_url:
        client_kwargs["base_url"] = base_url.rstrip("/")
    client = OpenAI(**client_kwargs)

    kwargs = {"model": model, "file": wav_path.open("rb")}
    if language:
        kwargs["language"] = language
    try:
        result = client.audio.transcriptions.create(**kwargs)
    finally:
        kwargs["file"].close()
    return (getattr(result, "text", None) or "").strip()


def transcribe_with_volcengine(wav_path: Path, language: str | None) -> str:
    import requests

    api_key = (
        os.environ.get("VOLCENGINE_ASR_API_KEY")
        or os.environ.get("MODEL_SPEECH_API_KEY")
        or os.environ.get("RETAIL_ASR_API_KEY")
    )
    app_id = os.environ.get("VOLCENGINE_ASR_APP_ID") or os.environ.get("MODEL_SPEECH_APP_ID")
    if not api_key:
        raise RuntimeError("未设置 VOLCENGINE_ASR_API_KEY。请先运行 configure_volcengine_asr.ps1。")
    if api_key.startswith("ark-"):
        raise RuntimeError(
            "当前 Key 看起来是火山方舟/大模型推理 Key，不是豆包语音 ASR 的 X-Api-Key。"
            "请到火山引擎控制台的豆包语音服务中获取 ASR 专用 X-Api-Key。"
        )

    headers = {
        "X-Api-Resource-Id": VOLCENGINE_ASR_RESOURCE_ID,
        "X-Api-Request-Id": str(uuid.uuid4()),
        "X-Api-Sequence": "-1",
    }
    if app_id:
        headers["X-Api-App-Key"] = app_id
        headers["X-Api-Access-Key"] = api_key
    else:
        headers["X-Api-Key"] = api_key

    audio_payload = {"data": base64.b64encode(wav_path.read_bytes()).decode("utf-8")}
    if language:
        audio_payload["language"] = language
    payload = {
        "user": {"uid": app_id or "qsm_retail_voice_user"},
        "audio": audio_payload,
        "request": {
            "model_name": "bigmodel",
            "enable_itn": True,
            "enable_punc": True,
        },
    }

    resp = requests.post(
        VOLCENGINE_ASR_ENDPOINT,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers=headers,
        timeout=60,
    )
    status_code = resp.headers.get("X-Api-Status-Code", "")
    if status_code != "20000000":
        msg = resp.headers.get("X-Api-Message", resp.text[:300] or "未知错误")
        logid = resp.headers.get("X-Tt-Logid", "N/A")
        raise RuntimeError(f"火山 ASR 请求失败: code={status_code}, msg={msg}, logid={logid}")

    result = resp.json()
    text_parts: list[str] = []
    if isinstance(result.get("result"), list):
        for item in result["result"]:
            if isinstance(item, dict) and item.get("text"):
                text_parts.append(item["text"])
    elif isinstance(result.get("result"), dict):
        res = result["result"]
        if res.get("text"):
            text_parts.append(res["text"])
        for utt in res.get("utterances", []) or []:
            if utt.get("text"):
                text_parts.append(utt["text"])
    for utt in result.get("utterances", []) or []:
        if utt.get("text"):
            text_parts.append(utt["text"])
    text = "".join(text_parts).strip()
    if not text:
        raise RuntimeError(f"火山 ASR 未返回可用文本: {json.dumps(result, ensure_ascii=False)[:500]}")
    return text


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Board microphone -> PC ASR -> retail answer")
    parser.add_argument("--seconds", type=int, default=5, help="recording duration")
    parser.add_argument("--catalog", default=str(DEFAULT_CATALOG))
    parser.add_argument("--current-product", default=None)
    parser.add_argument("--visual-state", default=str(DEFAULT_VISUAL_STATE))
    parser.add_argument("--ignore-visual-state", action="store_true")
    parser.add_argument("--reply-mode", choices=["offline", "auto", "llm"], default="offline")
    parser.add_argument("--asr-provider", choices=["volcengine", "openai"], default="volcengine")
    parser.add_argument("--asr-model", default=os.environ.get("RETAIL_ASR_MODEL", "whisper-1"))
    parser.add_argument("--language", default="zh")
    parser.add_argument("--asr-text", default=None, help="skip ASR and use this text for pipeline testing")
    parser.add_argument("--keep-audio-dir", default=str(DEFAULT_OUTPUT_DIR))
    return parser.parse_args()


def read_current_product_from_visual_state(path: Path, catalog: dict[str, dict]) -> str | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return None
    label = data.get("label")
    if label in catalog and label != "unknown":
        return label
    return None


def main() -> int:
    args = parse_args()
    catalog = load_catalog(Path(args.catalog))
    current_product = args.current_product if args.current_product in catalog else None
    if not current_product and not args.ignore_visual_state:
        current_product = read_current_product_from_visual_state(Path(args.visual_state), catalog)
        if current_product:
            item = catalog[current_product]
            print(f"当前视觉商品> {item.get('name_zh', current_product)} / {current_product}")

    if args.asr_text:
        wav_path = None
        question = args.asr_text.strip()
    else:
        adb = find_adb()
        wav_path = record_from_board(adb, args.seconds, Path(args.keep_audio_dir))
        try:
            if args.asr_provider == "volcengine":
                question = transcribe_with_volcengine(wav_path, args.language).strip()
            else:
                question = transcribe_with_openai(wav_path, args.asr_model, args.language).strip()
        except Exception as exc:
            print(f"ASR失败: {exc}")
            print("录音已保存，可先配置 ASR Key 后重试，或用 --asr-text 验证问答链路。")
            return 2

    if not question:
        print("ASR没有识别到有效文字，请靠近麦克风重试。")
        return 3

    print(f"客户语音识别结果> {question}")
    reply, source = answer_question(question, catalog, current_product, args.reply_mode, timeout=8.0)
    print(f"助手> {reply}")
    print(f"[reply_source={source}]")
    if wav_path:
        print(f"[audio={wav_path}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
