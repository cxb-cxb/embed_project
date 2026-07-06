#!/usr/bin/env python3
"""Retail voice-query assistant.

First-stage runtime uses terminal text input as the ASR result. Speaker/TTS can
be added later by consuming the same answer text printed by this script.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = PROJECT_ROOT / "catalog.json"

PRODUCT_ALIASES = {
    "cola": ["可乐", "cola", "coke"],
    "noodle": ["方便面", "泡面", "面", "noodle", "instant noodle"],
    "chips": ["薯片", "chips"],
    "biscuit": ["饼干", "biscuit", "cookie"],
    "milk": ["牛奶", "milk"],
    "bread": ["面包", "bread"],
    "toothpaste": ["牙膏", "toothpaste"],
    "water": ["矿泉水", "水", "water"],
    "tissue": ["纸巾", "抽纸", "tissue"],
    "soap": ["肥皂", "香皂", "soap"],
}

DEFAULT_STOCK = {
    "cola": 18,
    "noodle": 15,
    "chips": 10,
    "biscuit": 12,
    "milk": 14,
    "bread": 9,
    "toothpaste": 8,
    "water": 24,
    "tissue": 16,
    "soap": 11,
}

DEFAULT_DESC = {
    "cola": "碳酸饮料，适合搭配零食。",
    "noodle": "速食方便面，适合快速充饥。",
    "chips": "休闲薯片，适合搭配饮料。",
    "biscuit": "饼干点心，适合早餐或加餐。",
    "milk": "牛奶饮品，适合早餐搭配。",
    "bread": "面包食品，适合作为早餐或简餐。",
    "toothpaste": "日用品牙膏，用于口腔清洁。",
    "water": "瓶装矿泉水，适合日常饮用。",
    "tissue": "生活用纸，适合日常清洁使用。",
    "soap": "清洁用品，适合洗手和日常清洁。",
}


def load_catalog(path: Path) -> dict[str, dict[str, Any]]:
    with path.open("r", encoding="utf-8") as f:
        catalog = json.load(f)
    for product_id, item in catalog.items():
        item.setdefault("id", product_id)
        item.setdefault("stock", DEFAULT_STOCK.get(product_id, 10))
        item.setdefault("desc", DEFAULT_DESC.get(product_id, "暂无详细介绍。"))
    return catalog


def normalize_text(text: str) -> str:
    return re.sub(r"\s+", " ", text.strip().lower())


def product_display(item: dict[str, Any], product_id: str) -> str:
    return f"{item.get('name_zh', product_id)} / {item.get('name', product_id)}"


def find_product_id(question: str, catalog: dict[str, dict[str, Any]], current_product: str | None) -> str | None:
    normalized = normalize_text(question)
    if any(word in normalized for word in ["这个", "当前", "this", "current"]) and current_product in catalog:
        return current_product

    for product_id, item in catalog.items():
        candidates = list(PRODUCT_ALIASES.get(product_id, []))
        candidates.extend([product_id, str(item.get("name", "")), str(item.get("name_zh", ""))])
        if any(alias and normalize_text(alias) in normalized for alias in candidates):
            return product_id
    return current_product if current_product in catalog else None


def detect_intent(question: str) -> str:
    normalized = normalize_text(question)
    cart_keywords = [
        ("cart_clear", ["清空购物车", "清空", "cart clear", "clear cart"]),
        ("cart_checkout", ["结算", "付款", "支付", "买单", "checkout", "pay"]),
        ("cart_total", ["总价", "一共", "合计", "total"]),
        ("cart_list", ["购物车", "车里", "买了什么", "有哪些商品", "cart"]),
    ]
    for intent, keywords in cart_keywords:
        if any(keyword in normalized for keyword in keywords):
            return intent

    intent_keywords = [
        ("list", ["有哪些", "都有", "商品列表", "卖什么", "list"]),
        ("price", ["多少钱", "价格", "售价", "几块", "price", "cost"]),
        ("stock", ["库存", "还有吗", "有货", "剩多少", "stock"]),
        ("recommend", ["推荐", "搭配", "建议", "早餐", "买什么", "recommend"]),
        ("desc", ["特点", "介绍", "怎么样", "是什么", "用途", "desc"]),
    ]
    for intent, keywords in intent_keywords:
        if any(keyword in normalized for keyword in keywords):
            return intent
    return "general"


def format_price(value: float) -> str:
    return f"{value:g}"


def summarize_cart(cart_state: dict[str, Any] | None, catalog: dict[str, dict[str, Any]]) -> str | None:
    if not cart_state:
        return None
    lines = cart_state.get("lines") or []
    if not lines:
        return "当前购物车是空的。"
    parts = []
    for line in lines:
        product_id = str(line.get("id", ""))
        item = catalog.get(product_id, {})
        name = item.get("name_zh") or line.get("name") or product_id
        qty = int(line.get("quantity", 0))
        subtotal = float(line.get("subtotal", 0.0))
        parts.append(f"{name} {qty} 件，小计 {format_price(subtotal)} 元")
    total = float(cart_state.get("total", 0.0))
    return "购物车里有：" + "；".join(parts) + f"。合计 {format_price(total)} 元。"


def cart_reply(intent: str, cart_state: dict[str, Any] | None, catalog: dict[str, dict[str, Any]]) -> str | None:
    if intent == "cart_list":
        return summarize_cart(cart_state, catalog)
    if intent == "cart_total":
        if not cart_state or not cart_state.get("lines"):
            return "当前购物车是空的，合计 0 元。"
        total = float(cart_state.get("total", 0.0))
        count = int(cart_state.get("item_count", 0))
        return f"购物车共有 {count} 件商品，合计 {format_price(total)} 元。"
    if intent == "cart_checkout":
        if not cart_state or float(cart_state.get("total", 0.0)) <= 0:
            return "购物车还是空的，请先扫码加入商品。"
        total = float(cart_state.get("total", 0.0))
        status = str(cart_state.get("status", ""))
        if "CHECKOUT" in status.upper():
            return f"订单已准备结算，需支付 {format_price(total)} 元。"
        return f"购物车合计 {format_price(total)} 元。请扫描 checkout 二维码完成结算。"
    if intent == "cart_clear":
        return "清空购物车需要扫描 clear 二维码，语音端当前只做状态查询。"
    return None


def list_products(catalog: dict[str, dict[str, Any]]) -> str:
    names = [item.get("name_zh", product_id) for product_id, item in catalog.items()]
    return "当前已录入商品有：" + "、".join(names) + "。"


def offline_reply(
    question: str,
    catalog: dict[str, dict[str, Any]],
    current_product: str | None,
    cart_state: dict[str, Any] | None = None,
) -> str:
    intent = detect_intent(question)
    reply = cart_reply(intent, cart_state, catalog)
    if reply:
        return reply

    if intent == "list":
        return list_products(catalog)

    product_id = find_product_id(question, catalog, current_product)
    if not product_id:
        return "我还没有识别到具体商品。可以说“牛奶多少钱”或“这个商品有什么特点”。"

    item = catalog[product_id]
    name = item.get("name_zh", product_id)
    price = float(item.get("price", 0))
    stock = int(item.get("stock", 0))
    desc = item.get("desc", "暂无详细介绍。")

    if intent == "price":
        return f"{name}售价 {price:g} 元。"
    if intent == "stock":
        state = "库存充足" if stock >= 10 else "库存偏少"
        return f"{name}当前库存 {stock} 件，{state}。"
    if intent == "recommend":
        return recommendation(product_id, name)
    if intent == "desc":
        return f"{name}：{desc}售价 {price:g} 元，当前库存 {stock} 件。"

    return f"识别到的商品是{name}，售价 {price:g} 元，当前库存 {stock} 件。"


def recommendation(product_id: str, name: str) -> str:
    pairs = {
        "milk": "可以搭配面包或饼干，适合作为早餐。",
        "bread": "可以搭配牛奶，适合作为早餐或简餐。",
        "biscuit": "可以搭配牛奶或矿泉水，适合加餐。",
        "chips": "可以搭配可乐，适合休闲场景。",
        "cola": "可以搭配薯片，但建议适量饮用。",
        "noodle": "可以搭配矿泉水，适合快速就餐。",
        "toothpaste": "建议和纸巾、肥皂等日用品一起购买。",
        "water": "适合搭配方便面、面包或饼干。",
        "tissue": "适合和肥皂等日用品一起购买。",
        "soap": "适合和纸巾、牙膏等日用品一起购买。",
    }
    return f"推荐：{name}{pairs.get(product_id, '可以根据需要搭配其他商品。')}"


def build_llm_messages(question: str, catalog: dict[str, dict[str, Any]], current_product: str | None) -> list[dict[str, str]]:
    compact_catalog = {
        product_id: {
            "中文名": item.get("name_zh", product_id),
            "英文名": item.get("name", product_id),
            "价格": item.get("price"),
            "库存": item.get("stock"),
            "介绍": item.get("desc"),
        }
        for product_id, item in catalog.items()
    }
    system = (
        "你是智能零售终端的中文导购助手。"
        "只根据给定商品目录回答，回答要简短自然，适合终端屏幕显示。"
        "如果用户问未录入商品或目录里没有的信息，要说明暂未录入。"
    )
    user = {
        "当前视觉识别商品": current_product or "未指定",
        "商品目录": compact_catalog,
        "客户问题": question,
    }
    return [
        {"role": "system", "content": system},
        {"role": "user", "content": json.dumps(user, ensure_ascii=False)},
    ]


def llm_reply(question: str, catalog: dict[str, dict[str, Any]], current_product: str | None, timeout: float) -> str:
    api_key = os.environ.get("RETAIL_LLM_API_KEY") or os.environ.get("OPENAI_API_KEY")
    base_url = os.environ.get("RETAIL_LLM_BASE_URL", "https://api.openai.com/v1").rstrip("/")
    model = os.environ.get("RETAIL_LLM_MODEL", "gpt-4o-mini")
    if not api_key:
        raise RuntimeError("missing RETAIL_LLM_API_KEY or OPENAI_API_KEY")

    payload = {
        "model": model,
        "messages": build_llm_messages(question, catalog, current_product),
        "temperature": 0.2,
        "max_tokens": 120,
    }
    req = urllib.request.Request(
        f"{base_url}/chat/completions",
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    return data["choices"][0]["message"]["content"].strip()


def answer_question(
    question: str,
    catalog: dict[str, dict[str, Any]],
    current_product: str | None,
    mode: str,
    timeout: float,
    cart_state: dict[str, Any] | None = None,
) -> tuple[str, str]:
    if mode in {"auto", "llm"}:
        try:
            return llm_reply(question, catalog, current_product, timeout), "llm"
        except (RuntimeError, urllib.error.URLError, urllib.error.HTTPError, TimeoutError, KeyError, IndexError) as exc:
            if mode == "llm":
                raise RuntimeError(f"LLM reply failed: {exc}") from exc
            return offline_reply(question, catalog, current_product, cart_state), f"offline fallback: {exc}"
    return offline_reply(question, catalog, current_product, cart_state), "offline"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Terminal retail voice-query assistant")
    parser.add_argument("--catalog", default=str(DEFAULT_CATALOG), help="catalog.json path")
    parser.add_argument("--mode", choices=["offline", "auto", "llm"], default="auto")
    parser.add_argument("--current-product", default=None, help="current visual recognition product id")
    parser.add_argument("--question", default=None, help="run one question and exit")
    parser.add_argument("--timeout", type=float, default=8.0, help="LLM request timeout seconds")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    catalog = load_catalog(Path(args.catalog))
    current_product = args.current_product if args.current_product in catalog else None

    print("Retail voice assistant is ready.")
    print(f"Mode: {args.mode}; current product: {current_product or 'none'}")
    print("Type a customer question. Type q/quit/exit to stop.")

    questions = [args.question] if args.question else None
    while True:
        if questions is None:
            try:
                question = input("\n客户> ").strip()
            except EOFError:
                break
        else:
            question = questions.pop(0) if questions else ""

        if not question:
            break
        if normalize_text(question) in {"q", "quit", "exit"}:
            break

        try:
            reply, source = answer_question(question, catalog, current_product, args.mode, args.timeout)
        except RuntimeError as exc:
            print(f"助手> 错误：{exc}")
            return 1
        print(f"助手> {reply}")
        print(f"[reply_source={source}]")

        if questions is not None:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
