# Embed_project 智能零售系统

这是面向 QSM368ZP-WF 开发板的智能零售赛题示例项目。项目使用 C++17 编写，核心流程包含商品识别、购物车计价、支付链接生成、语音指令解析，并预留摄像头、二维码、RKNN 模型和真实支付接口的二次开发入口。

## 功能对应

- 商品信息识别：支持条码、二维码 payload、外观关键词三种输入方式，默认内置 10 种商品。
- 智能计价与支付：购物车自动累计数量和金额，生成订单号与支付 URL。
- 语音交互查询：支持加入商品、查询价格、查看购物车、清空购物车、结算等命令。
- 低延迟扩展：核心逻辑无外部依赖，便于在板端和摄像头采集线程中直接调用。

## 目录

```text
Embed_project/
  CMakeLists.txt
  data/products.csv
  docs/二次开发使用文档.md
  include/
  scripts/
  src/
  tests/
```

## 编译

在 Linux 主机或 QSM368ZP-WF 板端执行：

```bash
cd Embed_project
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

如果板端没有 CMake，但有 g++：

```bash
cd Embed_project
mkdir -p build
g++ -std=c++17 -Iinclude src/*.cpp -o build/embed_project
g++ -std=c++17 -Iinclude tests/test_core.cpp src/catalog.cpp src/cart.cpp src/payment.cpp src/recognizer.cpp src/voice.cpp -o build/test_core
./build/test_core
```

## 运行

```bash
./build/embed_project data/products.csv
```

示例命令：

```text
list
scan 690100000002
see red soda can
voice add milk
voice price bread
cart
checkout
```

## 板端摄像头预览

当前已验证的摄像头 0 + LVDS 屏幕预览命令放在：

```bash
scripts/camera_preview_lvds.sh
```

如果连接参数没有变化，可以在板端运行：

```bash
sh scripts/camera_preview_lvds.sh
```

## 板端二维码识别 + LVDS 实时显示

当前推荐的扫码演示入口是：

```bash
cd /userdata/Embed_project
sh scripts/qr_realtime_lvds.sh
```

该脚本会启动 `/userdata/Embed_project/bin/qr_scanner_display`，同时使用摄像头和 LVDS 屏幕。

2026-07-04 已验证完整演示闭环：

```text
product:cola -> added:Cola total:3.50
checkout     -> CHECKOUT READY
clear        -> CART CLEARED
```

LVDS 屏幕会显示实时摄像头画面、绿色二维码框、商品信息、总价、结算状态、订单号和支付链接摘要。测试二维码可用任意二维码生成器生成，内容分别填入 `product:cola`、`checkout`、`clear`。

停止程序：

```bash
pkill -9 qr_scanner_display
```

详细二次开发说明见 `docs/二次开发使用文档.md`。
