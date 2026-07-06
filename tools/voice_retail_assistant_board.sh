#!/bin/sh
set -eu

CURRENT_PRODUCT="${CURRENT_PRODUCT:-}"
QUESTION="${QUESTION:-}"
if [ -z "$QUESTION" ] && [ "$#" -gt 0 ]; then
    QUESTION="$*"
fi

product_name() {
    case "$1" in
        cola) echo "可乐" ;;
        noodle) echo "方便面" ;;
        chips) echo "薯片" ;;
        biscuit) echo "饼干" ;;
        milk) echo "牛奶" ;;
        bread) echo "面包" ;;
        toothpaste) echo "牙膏" ;;
        water) echo "矿泉水" ;;
        tissue) echo "纸巾" ;;
        soap) echo "肥皂" ;;
        *) echo "" ;;
    esac
}

product_price() {
    case "$1" in
        cola) echo "3.5" ;;
        noodle) echo "4" ;;
        chips) echo "6" ;;
        biscuit) echo "5.5" ;;
        milk) echo "4.5" ;;
        bread) echo "5" ;;
        toothpaste) echo "8" ;;
        water) echo "2" ;;
        tissue) echo "4" ;;
        soap) echo "3" ;;
        *) echo "" ;;
    esac
}

product_stock() {
    case "$1" in
        cola) echo "18" ;;
        noodle) echo "15" ;;
        chips) echo "10" ;;
        biscuit) echo "12" ;;
        milk) echo "14" ;;
        bread) echo "9" ;;
        toothpaste) echo "8" ;;
        water) echo "24" ;;
        tissue) echo "16" ;;
        soap) echo "11" ;;
        *) echo "" ;;
    esac
}

find_product() {
    q="$1"
    case "$q" in
        *可乐*|*cola*|*coke*) echo "cola" ;;
        *方便面*|*泡面*|*noodle*) echo "noodle" ;;
        *薯片*|*chips*) echo "chips" ;;
        *饼干*|*biscuit*|*cookie*) echo "biscuit" ;;
        *牛奶*|*milk*) echo "milk" ;;
        *面包*|*bread*) echo "bread" ;;
        *牙膏*|*toothpaste*) echo "toothpaste" ;;
        *矿泉水*|*water*) echo "water" ;;
        *纸巾*|*抽纸*|*tissue*) echo "tissue" ;;
        *肥皂*|*香皂*|*soap*) echo "soap" ;;
        *这个*|*当前*) echo "$CURRENT_PRODUCT" ;;
        *) echo "$CURRENT_PRODUCT" ;;
    esac
}

recommendation() {
    case "$1" in
        milk) echo "推荐：牛奶可以搭配面包或饼干，适合作为早餐。" ;;
        bread) echo "推荐：面包可以搭配牛奶，适合作为早餐或简餐。" ;;
        biscuit) echo "推荐：饼干可以搭配牛奶或矿泉水，适合加餐。" ;;
        chips) echo "推荐：薯片可以搭配可乐，适合休闲场景。" ;;
        cola) echo "推荐：可乐可以搭配薯片，但建议适量饮用。" ;;
        noodle) echo "推荐：方便面可以搭配矿泉水，适合快速就餐。" ;;
        toothpaste) echo "推荐：牙膏可以和纸巾、肥皂等日用品一起购买。" ;;
        water) echo "推荐：矿泉水适合搭配方便面、面包或饼干。" ;;
        tissue) echo "推荐：纸巾适合和肥皂等日用品一起购买。" ;;
        soap) echo "推荐：肥皂适合和纸巾、牙膏等日用品一起购买。" ;;
        *) echo "我还没有识别到具体商品，暂时不能推荐搭配。" ;;
    esac
}

answer() {
    q="$1"
    case "$q" in
        *有哪些*|*都有*|*商品列表*|*卖什么*)
            echo "当前已录入商品有：可乐、方便面、薯片、饼干、牛奶、面包、牙膏、矿泉水、纸巾、肥皂。"
            return
            ;;
    esac

    pid="$(find_product "$q")"
    name="$(product_name "$pid")"
    if [ -z "$name" ]; then
        echo "我还没有识别到具体商品。可以说“牛奶多少钱”或“这个商品有什么特点”。"
        return
    fi

    price="$(product_price "$pid")"
    stock="$(product_stock "$pid")"
    case "$q" in
        *多少钱*|*价格*|*售价*|*几块*)
            echo "${name}售价 ${price} 元。"
            ;;
        *库存*|*还有吗*|*有货*|*剩多少*)
            echo "${name}当前库存 ${stock} 件，库存充足。"
            ;;
        *推荐*|*搭配*|*早餐*|*建议*)
            recommendation "$pid"
            ;;
        *特点*|*介绍*|*是什么*|*用途*)
            echo "${name}售价 ${price} 元，当前库存 ${stock} 件，适合智能零售演示查询。"
            ;;
        *)
            echo "识别到的商品是${name}，售价 ${price} 元，当前库存 ${stock} 件。"
            ;;
    esac
}

echo "Retail voice assistant board offline mode is ready."
echo "Current product: ${CURRENT_PRODUCT:-none}"

if [ -n "$QUESTION" ]; then
    echo "客户> $QUESTION"
    printf "助手> "
    answer "$QUESTION"
    exit 0
fi

while true; do
    printf "\n客户> "
    if ! read -r q; then
        break
    fi
    case "$q" in
        q|quit|exit) break ;;
    esac
    printf "助手> "
    answer "$q"
done
