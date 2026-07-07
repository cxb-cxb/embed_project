# 火山 ASR 语音问答配置

当前程序支持命令：

```text
voiceask [seconds]
```

执行流程：

```text
录音 -> 上传火山 ASR -> 得到文字 -> 调用 ask 问答引擎 -> 终端显示答案
```

## 1. 板端配置

在板子的 shell 中设置环境变量：

```bash
export VOLCANO_APP_ID="你的 App ID"
export VOLCANO_ACCESS_TOKEN="你的 Access Token"
export VOLCANO_ASR_RESOURCE_ID="volc.bigasr.auc_turbo"
export VOLCANO_ASR_URL="https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash"
```

如果控制台给的是 cluster，也可以先设置：

```bash
export VOLCANO_ASR_CLUSTER="你的 ASR cluster"
```

程序会优先使用 `VOLCANO_ASR_RESOURCE_ID`，如果没有设置，就使用 `VOLCANO_ASR_CLUSTER`。
极速识别接口通常使用 `volc.bigasr.auc_turbo`，不要直接填 TTS 资源名。

## 2. 运行

```bash
cd /userdata/Embed_project
./bin/embed_project data/products.csv
```

进入程序后输入：

```text
voiceask 3
```

程序会录音 3 秒，然后识别并回答。

## 3. 注意

- TTS 资源和 ASR 资源不是同一个能力。你之前的 TTS voice type 只能用于文字转语音，不能直接用于语音转文字。
- 板端麦克风参数已按实测配置为 `hw:0,0 / 48000Hz / 双声道`。
- 如果火山控制台给出的 endpoint 或资源 ID 与默认值不同，以控制台为准。
