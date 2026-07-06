#!/usr/bin/env python3
"""Record from the board microphone, transcribe on the PC, then answer."""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import time
import uuid
import wave
import audioop
from pathlib import Path

from voice_retail_assistant import answer_question, load_catalog


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = PROJECT_ROOT / "catalog.json"
DEFAULT_OUTPUT_DIR = Path(r"D:\qsm_embed_dataset\voice_samples")
DEFAULT_VISUAL_STATE = Path(r"D:\qsm_embed_dataset\lvds_overlay_runtime\current_product.json")
DEFAULT_VOICE_REPLY_STATE = Path(r"D:\qsm_embed_dataset\lvds_overlay_runtime\voice_reply.json")
DEFAULT_VOICE_OUTPUT_DIR = Path(r"D:\qsm_embed_dataset\voice_reply_output")
DEFAULT_BOARD_CART_LOG = "/tmp/qsm_lvds_demo.log"
VOLCENGINE_ASR_ENDPOINT = os.environ.get(
    "VOLCENGINE_ASR_ENDPOINT",
    "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash",
)
VOLCENGINE_ASR_RESOURCE_ID = os.environ.get(
    "VOLCENGINE_ASR_RESOURCE_ID",
    "volc.bigasr.auc_turbo",
)


def find_adb() -> str:
    bundled_adb = PROJECT_ROOT / "tools" / "adb" / "adb.exe"
    if bundled_adb.exists():
        return str(bundled_adb)
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


def record_from_board(adb: str, seconds: int, output_dir: Path, mic_path: str) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    remote_wav = f"/tmp/qsm_voice_query_{stamp}.wav"
    local_wav = output_dir / f"qsm_voice_query_{stamp}.wav"
    shell_cmd = (
        f"amixer -c 0 cset name='Capture MIC Path' '{mic_path}' >/dev/null; "
        f"arecord -D hw:0,0 -f S16_LE -c 2 -r 16000 -d {seconds} '{remote_wav}'"
    )
    print(f"请对着板载麦克风说话，录音 {seconds} 秒...")
    run([adb, "shell", shell_cmd])
    run([adb, "pull", remote_wav, str(local_wav)])
    print(f"录音文件: {local_wav}")
    return local_wav


def wav_level(path: Path) -> tuple[int, int, float]:
    with wave.open(str(path), "rb") as wav:
        frames = wav.getnframes()
        rate = wav.getframerate() or 1
        width = wav.getsampwidth()
        data = wav.readframes(frames)
    duration = frames / float(rate)
    if not data:
        return 0, 0, duration
    return audioop.rms(data, width), audioop.max(data, width), duration


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
    parser.add_argument("--mic-path", choices=["Main Mic", "Hands Free Mic"], default="Main Mic")
    parser.add_argument("--min-rms", type=int, default=35, help="minimum recording RMS before calling ASR")
    parser.add_argument("--catalog", default=str(DEFAULT_CATALOG))
    parser.add_argument("--current-product", default=None)
    parser.add_argument("--visual-state", default=str(DEFAULT_VISUAL_STATE))
    parser.add_argument("--voice-reply-state", default=str(DEFAULT_VOICE_REPLY_STATE))
    parser.add_argument("--board-cart-log", default=DEFAULT_BOARD_CART_LOG)
    parser.add_argument("--ignore-cart-state", action="store_true")
    parser.add_argument("--ignore-visual-state", action="store_true")
    parser.add_argument("--reply-mode", choices=["offline", "auto", "llm"], default="offline")
    parser.add_argument("--asr-provider", choices=["volcengine", "openai"], default="volcengine")
    parser.add_argument("--asr-model", default=os.environ.get("RETAIL_ASR_MODEL", "whisper-1"))
    parser.add_argument("--language", default="zh")
    parser.add_argument("--asr-text", default=None, help="skip ASR and use this text for pipeline testing")
    parser.add_argument("--keep-audio-dir", default=str(DEFAULT_OUTPUT_DIR))
    parser.add_argument("--voice-output-dir", default=str(DEFAULT_VOICE_OUTPUT_DIR))
    parser.add_argument(
        "--voice-output",
        choices=["terminal", "wav", "board"],
        default="terminal",
        help="terminal: text only; wav: also synthesize Windows WAV; board: synthesize and play on board audio",
    )
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


def build_product_name_index(catalog: dict[str, dict]) -> dict[str, str]:
    aliases = {
        "mineral water": "water",
        "water": "water",
        "cola": "cola",
        "milk": "milk",
        "bread": "bread",
        "instant noodles": "noodle",
        "instant noodle": "noodle",
        "noodle": "noodle",
        "potato chips": "chips",
        "chips": "chips",
        "cookies": "biscuit",
        "cookie": "biscuit",
        "biscuit": "biscuit",
        "toothpaste": "toothpaste",
        "tissue": "tissue",
        "soap": "soap",
    }
    for product_id, item in catalog.items():
        aliases[product_id.lower()] = product_id
        aliases[str(item.get("name", "")).strip().lower()] = product_id
        aliases[str(item.get("name_zh", "")).strip().lower()] = product_id
    return {key: value for key, value in aliases.items() if key}


def read_board_cart_log(adb: str, remote_log: str) -> str:
    completed = subprocess.run(
        [adb, "shell", f"cat {remote_log} 2>/dev/null || true"],
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=8,
    )
    return completed.stdout or ""


def parse_cart_state_from_log(log_text: str, catalog: dict[str, dict]) -> dict | None:
    if not log_text.strip():
        return None
    name_index = build_product_name_index(catalog)
    quantities: dict[str, int] = {}
    total_from_log: float | None = None
    status = "READY"
    checkout_done = False

    for raw_line in log_text.splitlines():
        line = raw_line.strip()
        control = re.search(r"control:([A-Z _-]+)", line)
        if control:
            status = control.group(1).strip()
            if "CLEARED" in status:
                quantities.clear()
                total_from_log = 0.0
                checkout_done = False
            elif "CHECKOUT" in status:
                checkout_done = True
            continue

        added = re.search(r"added:([^\r\n]+?)\s+total:(\d+)\.(\d{2})", line, re.IGNORECASE)
        if added:
            product_name = added.group(1).strip().lower()
            product_id = name_index.get(product_name)
            if product_id:
                quantities[product_id] = quantities.get(product_id, 0) + 1
            total_from_log = float(f"{added.group(2)}.{added.group(3)}")
            status = f"ADDED {added.group(1).strip()}"
            checkout_done = False

    lines = []
    item_count = 0
    computed_total = 0.0
    for product_id, quantity in quantities.items():
        item = catalog.get(product_id, {})
        price = float(item.get("price", 0.0))
        subtotal = round(price * quantity, 2)
        item_count += quantity
        computed_total += subtotal
        lines.append(
            {
                "id": product_id,
                "name": item.get("name", product_id),
                "name_zh": item.get("name_zh", product_id),
                "quantity": quantity,
                "price": price,
                "subtotal": subtotal,
            }
        )

    total = round(total_from_log if total_from_log is not None else computed_total, 2)
    return {
        "lines": lines,
        "item_count": item_count,
        "total": total,
        "status": status,
        "checkout_done": checkout_done,
    }


def read_cart_state_from_board(adb: str | None, remote_log: str, catalog: dict[str, dict]) -> dict | None:
    if not adb:
        return None
    try:
        log_text = read_board_cart_log(adb, remote_log)
    except Exception:
        return None
    return parse_cart_state_from_log(log_text, catalog)


def write_voice_reply_state(path: Path, question: str, reply: str, source: str, current_product: str | None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "question": question,
        "reply": reply,
        "source": source,
        "current_product": current_product or "",
        "updated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    path.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")


def synthesize_windows_wav(text: str, wav_path: Path) -> bool:
    wav_path.parent.mkdir(parents=True, exist_ok=True)
    text_path = wav_path.with_suffix(".txt")
    script_path = wav_path.with_suffix(".sapi.ps1")
    text_path.write_text(text, encoding="utf-8")
    script_path.write_text(
        "\n".join(
            [
                "param([string]$TextPath, [string]$WavPath)",
                "Add-Type -AssemblyName System.Speech",
                "$text = Get-Content -LiteralPath $TextPath -Raw -Encoding UTF8",
                "$speaker = New-Object System.Speech.Synthesis.SpeechSynthesizer",
                "$speaker.Rate = 0",
                "$speaker.Volume = 100",
                "$speaker.SetOutputToWaveFile($WavPath)",
                "$speaker.Speak($text)",
                "$speaker.Dispose()",
            ]
        ),
        encoding="utf-8",
    )
    try:
        completed = subprocess.run(
            [
                "powershell.exe",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(script_path),
                "-TextPath",
                str(text_path),
                "-WavPath",
                str(wav_path),
            ],
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
    except Exception as exc:
        print(f"[voice_output=wav_failed: {exc}]")
        return False
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()
        print(f"[voice_output=wav_failed: {detail}]")
        return False
    return wav_path.exists() and wav_path.stat().st_size > 44


def convert_wav_for_board(src_wav: Path, dst_wav: Path) -> bool:
    try:
        with wave.open(str(src_wav), "rb") as src:
            channels = src.getnchannels()
            sample_width = src.getsampwidth()
            frame_rate = src.getframerate()
        if channels == 1 and sample_width == 2 and frame_rate == 16000:
            dst_wav.write_bytes(src_wav.read_bytes())
            return True
    except wave.Error:
        pass

    try:
        completed = subprocess.run(
            ["ffmpeg", "-y", "-i", str(src_wav), "-ac", "1", "-ar", "16000", "-sample_fmt", "s16", str(dst_wav)],
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
        )
    except FileNotFoundError:
        print("[voice_output=board_wav_convert_skipped: ffmpeg not found on PC]")
        return False
    if completed.returncode != 0:
        print(f"[voice_output=board_wav_convert_failed: {(completed.stderr or '').strip()[:300]}]")
        return False
    return dst_wav.exists() and dst_wav.stat().st_size > 44


def write_software_voice_output(
    adb: str | None,
    output_dir: Path,
    question: str,
    reply: str,
    source: str,
    mode: str,
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    payload = {
        "question": question,
        "reply": reply,
        "source": source,
        "updated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "speaker_ready": mode == "board",
    }
    json_path = output_dir / "last_reply.json"
    text_path = output_dir / "last_reply.txt"
    json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    text_path.write_text(reply, encoding="utf-8")
    print(f"[voice_output_text={text_path}]")

    if mode == "terminal":
        return

    sapi_wav = output_dir / "last_reply_sapi.wav"
    if not synthesize_windows_wav(reply, sapi_wav):
        return
    print(f"[voice_output_wav={sapi_wav}]")

    if mode != "board":
        return
    if not adb:
        print("[voice_output=board_play_skipped: adb unavailable]")
        return
    board_wav = output_dir / "last_reply_board.wav"
    if not convert_wav_for_board(sapi_wav, board_wav):
        return
    remote_wav = "/tmp/qsm_last_voice_reply.wav"
    run([adb, "push", str(board_wav), remote_wav], check=True)
    run([adb, "shell", "amixer -c 0 cset name='Playback Path' 'SPK_HP' >/dev/null 2>&1 || true"], check=False)
    run([adb, "shell", f"aplay -D hw:0,0 '{remote_wav}'"], check=False)


def main() -> int:
    args = parse_args()
    catalog = load_catalog(Path(args.catalog))
    current_product = args.current_product if args.current_product in catalog else None
    if not current_product and not args.ignore_visual_state:
        current_product = read_current_product_from_visual_state(Path(args.visual_state), catalog)
        if current_product:
            item = catalog[current_product]
            print(f"当前视觉商品> {item.get('name_zh', current_product)} / {current_product}")

    adb = None if args.ignore_cart_state else find_adb()
    cart_state = None if args.ignore_cart_state else read_cart_state_from_board(adb, args.board_cart_log, catalog)
    if cart_state and cart_state.get("lines"):
        print(f"购物车状态> {cart_state.get('item_count', 0)} 件，合计 {cart_state.get('total', 0):g} 元")

    if args.asr_text:
        wav_path = None
        question = args.asr_text.strip()
    else:
        if adb is None:
            adb = find_adb()
        wav_path = record_from_board(adb, args.seconds, Path(args.keep_audio_dir), args.mic_path)
        rms, peak, duration = wav_level(wav_path)
        print(f"[record_level rms={rms} peak={peak} duration={duration:.1f}s mic='{args.mic_path}']")
        if rms < args.min_rms:
            print(
                "录音音量太低，ASR 会判断为静音。请靠近麦克风，"
                "或尝试 --mic-path 'Hands Free Mic'。"
            )
            return 4
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
    reply, source = answer_question(question, catalog, current_product, args.reply_mode, timeout=8.0, cart_state=cart_state)
    write_voice_reply_state(Path(args.voice_reply_state), question, reply, source, current_product)
    print(f"助手> {reply}")
    print(f"[reply_source={source}]")
    write_software_voice_output(adb, Path(args.voice_output_dir), question, reply, source, args.voice_output)
    if wav_path:
        print(f"[audio={wav_path}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
