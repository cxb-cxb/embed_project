import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class VoiceAutoListenScriptTest(unittest.TestCase):
    def test_run_mission_starts_screen_and_voice(self):
        script = ROOT / "scripts" / "run_mission.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("start_retail_lvds_ui.sh", text)
        self.assertIn("start_voice_auto_listen.sh", text)
        self.assertIn("configure_board_voice_speaker.sh", text)
        self.assertIn("/tmp/qsm_auto_voice.log", text)
        self.assertIn("/tmp/qsm_auto_voice.pid", text)
        self.assertIn("run_mission is ready", text)

    def test_payment_done_enter_script_triggers_ui_reset_file(self):
        script = ROOT / "scripts" / "payment_done_enter.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("read -r", text)
        self.assertIn("/tmp/qsm_payment_done", text)
        self.assertIn(": > \"$PAYMENT_DONE_FILE\"", text)

    def test_run_mission_stops_previous_shell_script_listeners_by_command_line(self):
        script = ROOT / "scripts" / "run_mission.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("pkill -f", text)
        self.assertIn("scripts/start_voice_auto_listen.sh", text)
        self.assertIn("scripts/run_voiceask_speaker.sh", text)
        self.assertIn("tinycap", text)
        self.assertNotIn("killall start_voice_auto_listen.sh", text)

    def test_auto_listen_has_single_instance_lock(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("LOCK_DIR=", text)
        self.assertIn("mkdir \"$LOCK_DIR\"", text)
        self.assertIn("another auto voice listener is already running", text)
        self.assertIn("rmdir \"$LOCK_DIR\"", text)

    def test_auto_listen_script_runs_voiceask_without_read_prompt(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8")
        self.assertIn("run_voiceask_speaker.sh", text)
        self.assertIn("--wake-once", text)
        self.assertIn("while true", text)
        self.assertNotIn("read -r", text)
        self.assertIn("trap", text)
        self.assertIn("welcome_tts.mp3", text)

    def test_auto_listen_has_wake_word_flow_with_chinese_ack(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("--wake-once", text)
        self.assertIn("WAKE_ACK_TEXT", text)
        self.assertIn("我在", text)
        self.assertIn("VOICE_WAKE_SECONDS", text)
        self.assertIn("VOICE_COMMAND_SECONDS", text)
        self.assertIn("Listening for wake word", text)

    def test_voiceask_speaker_writes_ui_state_file(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("VOICE_STATE_FILE", text)
        self.assertIn("/tmp/qsm_retail_voice_state", text)
        self.assertIn("write_voice_state", text)
        self.assertIn("voice_cart_command", text)
        self.assertIn("QUESTION=", text)
        self.assertIn("ANSWER=", text)
        self.assertIn("CART_CMD=", text)
        self.assertIn("小智小智", text)
        self.assertIn("contains_wake_word", text)
        self.assertIn("未检测到唤醒词", text)
        self.assertNotIn("CHINESE REPLY PLAYED", text)

    def test_voice_checkout_command_updates_ui_cart_command(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("checkout", text)
        self.assertIn("结账", text)
        self.assertIn("支付", text)
        self.assertIn("买单", text)

    def test_voice_add_to_cart_keywords_cover_natural_chinese_requests(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        for keyword in [
            "加入购物车",
            "加一瓶",
            "加一包",
            "我要",
            "来个",
            "拿一瓶",
            "来一袋",
        ]:
            with self.subTest(keyword=keyword):
                self.assertIn(keyword, text)
        for command in [
            "add:cola",
            "add:milk",
            "add:water",
            "add:bread",
            "add:noodle",
            "add:chips",
            "add:biscuit",
            "add:toothpaste",
            "add:tissue",
            "add:soap",
        ]:
            with self.subTest(command=command):
                self.assertIn(command, text)

    def test_local_cart_commands_use_fast_reply_path(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("play_local_cart_reply()", text)
        self.assertIn("VOICE_FAST_CART_REPLY", text)
        self.assertIn('play_local_cart_reply "$answer" || true', text)
        self.assertIn("Starting async local cart reply TTS.", text)
        self.assertIn("--speak-local-reply", text)
        self.assertIn("nohup", text)
        self.assertIn("/tmp/qsm_local_cart_tts.log", text)

    def test_cart_commands_bypass_open_chat(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("local_cart_reply()", text)
        self.assertIn("cart_cmd=\"$(voice_cart_command \"$question\")\"", text)
        cart_pos = text.index("cart_cmd=\"$(voice_cart_command \"$question\")\"")
        open_pos = text.index("answer=\"$(request_open_chat")
        self.assertLess(cart_pos, open_pos)
        self.assertIn("Retail command:", text)

    def test_retail_lexicon_has_priority_before_open_chat(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("retail_lexicon_command()", text)
        self.assertIn("product_from_retail_lexicon()", text)
        self.assertIn("Retail lexicon hit:", text)
        self.assertIn('lexicon_cmd="$(retail_lexicon_command "$question")"', text)

        lexicon_pos = text.index('lexicon_cmd="$(retail_lexicon_command "$question")"')
        open_pos = text.index('answer="$(request_open_chat')
        self.assertLess(lexicon_pos, open_pos)

        for keyword in [
            "可乐", "扣乐", "可落", "阔乐", "cola", "coke",
            "牛奶", "奶", "矿泉水", "冰露", "面包", "泡面", "杯面",
            "薯片", "薯条", "饼干", "曲奇", "牙膏", "纸巾", "抽纸", "香皂", "肥皂",
            "多少钱", "价格", "库存", "推荐", "有啥", "来一个", "拿一个"
        ]:
            with self.subTest(keyword=keyword):
                self.assertIn(keyword, text)

    def test_checkout_asks_for_payment_method_before_showing_code(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("checkout_pending", text)
        self.assertIn("pay:wechat", text)
        self.assertIn("pay:alipay", text)
        self.assertIn("pay:unionpay", text)
        self.assertIn("PAYMENT_WAIT_FILE", text)
        self.assertIn("voice_payment_method_command()", text)
        self.assertIn("请选择微信支付、支付宝支付或银联云闪付", text)
        self.assertIn("银联云闪付暂不可用", text)
        payment_pos = text.index('cart_cmd="$(voice_payment_method_command "$question")"')
        checkout_pos = text.index('cart_cmd="$(voice_cart_command "$question")"')
        self.assertLess(payment_pos, checkout_pos)

    def test_checkout_and_payment_keywords_cover_common_asr_variants(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        for keyword in [
            "结账", "结帐", "结算", "买单", "车", "付钱", "买好了", "付一下",
            "扫微信", "用微信", "扫支付宝", "用支付宝", "支付宝码", "支付宝收款码",
            "宝支付", "之付宝", "支护宝", "蓝色支付", "用蓝色的",
            "云闪付", "用银联"
        ]:
            with self.subTest(keyword=keyword):
                self.assertIn(keyword, text)

    def test_tts_playback_blocks_microphone_recording(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn('TTS_PLAYING_FILE="${TTS_PLAYING_FILE:-/tmp/qsm_tts_playing}"', text)
        self.assertIn("with_tts_playback_lock()", text)
        self.assertIn("wait_for_tts_playback_idle()", text)
        self.assertIn('printf "%s\\n" "$$" > "$TTS_PLAYING_FILE"', text)
        self.assertIn('sleep "${VOICE_TTS_POST_DELAY_SECONDS:-1.5}"', text)
        self.assertIn('rm -f "$TTS_PLAYING_FILE"', text)
        self.assertIn('wait_for_tts_playback_idle "$label"', text)

        play_pos = text.index("play_wav_file()")
        play_block = text[play_pos: play_pos + 360]
        self.assertIn("with_tts_playback_lock", play_block)

        recognize_pos = text.index("recognize_voice_once()")
        recognize_block = text[recognize_pos: recognize_pos + 420]
        self.assertIn('wait_for_tts_playback_idle "$label"', recognize_block)
        self.assertLess(
            recognize_block.index('wait_for_tts_playback_idle "$label"'),
            recognize_block.index("prepare_mic"),
        )

    def test_async_tts_locks_before_auto_listener_can_record(self):
        speaker_script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        auto_script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(speaker_script.exists())
        self.assertTrue(auto_script.exists())

        speaker_text = speaker_script.read_text(encoding="utf-8", errors="ignore")
        auto_text = auto_script.read_text(encoding="utf-8", errors="ignore")

        self.assertIn('start_async_local_cart_reply_tts()', speaker_text)
        async_pos = speaker_text.index('start_async_local_cart_reply_tts()')
        async_block = speaker_text[async_pos: async_pos + 620]
        self.assertIn('printf "%s\\n" "$$" > "$TTS_PLAYING_FILE"', async_block)
        self.assertIn('--speak-local-reply "$answer"', async_block)
        self.assertLess(
            async_block.index('printf "%s\\n" "$$" > "$TTS_PLAYING_FILE"'),
            async_block.index('--speak-local-reply "$answer"'),
        )

        speak_case_pos = speaker_text.index('--speak-local-reply)')
        speak_case_block = speaker_text[speak_case_pos: speak_case_pos + 180]
        self.assertIn('with_tts_playback_lock play_text_tts "$*"', speak_case_block)

        self.assertIn('TTS_PLAYING_FILE="${TTS_PLAYING_FILE:-/tmp/qsm_tts_playing}"', auto_text)
        self.assertIn('wait_for_tts_playback_idle()', auto_text)
        loop_pos = auto_text.index('while true; do')
        loop_block = auto_text[loop_pos: loop_pos + 360]
        self.assertIn('wait_for_tts_playback_idle wake', loop_block)
        self.assertLess(loop_block.index('wait_for_tts_playback_idle wake'), loop_block.index('prepare_audio'))

    def test_payment_reply_echo_does_not_retrigger_payment_loop(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("is_payment_reply_echo()", text)
        self.assertIn('if is_payment_reply_echo "$question"; then', text)
        self.assertIn("Ignoring payment reply echo.", text)
        filter_pos = text.index("is_payment_reply_echo()")
        filter_block = text[filter_pos: filter_pos + 520]
        self.assertIn("请选择微信支付、支付宝支付或银联云闪付", filter_block)

        echo_pos = text.index('if is_payment_reply_echo "$question"; then')
        payment_pos = text.index('cart_cmd="$(voice_payment_method_command "$question")"')
        self.assertLess(echo_pos, payment_pos)

        retail_pos = text.index('echo "Retail command detected without wake word."')
        retail_block = text[retail_pos: retail_pos + 320]
        self.assertIn('run_open_chat_reply "$wake_text" || true', retail_block)
        self.assertIn("run_active_session", retail_block)

    def test_retail_commands_without_wake_continue_voice_detection(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        retail_pos = text.index('echo "Retail command detected without wake word."')
        retail_block = text[retail_pos: retail_pos + 360]
        self.assertIn('run_open_chat_reply "$wake_text" || true', retail_block)
        self.assertIn("run_active_session", retail_block)

    def test_payment_finish_signal_triggers_voice_prompt(self):
        speaker_script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        auto_script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(speaker_script.exists())
        self.assertTrue(auto_script.exists())

        speaker_text = speaker_script.read_text(encoding="utf-8", errors="ignore")
        auto_text = auto_script.read_text(encoding="utf-8", errors="ignore")

        self.assertIn('PAYMENT_FINISHED_VOICE_FILE="${PAYMENT_FINISHED_VOICE_FILE:-/tmp/qsm_payment_finished_voice}"', speaker_text)
        self.assertIn("consume_payment_finished_voice_prompt()", speaker_text)
        self.assertIn('rm -f "$PAYMENT_FINISHED_VOICE_FILE"', speaker_text)
        self.assertIn('play_local_cart_reply "$PAYMENT_FINISHED_TEXT" || true', speaker_text)
        self.assertIn('consume_payment_finished_voice_prompt "$label"', speaker_text)
        self.assertIn("--payment-finished-prompt", speaker_text)

        self.assertIn('PAYMENT_FINISHED_VOICE_FILE="${PAYMENT_FINISHED_VOICE_FILE:-/tmp/qsm_payment_finished_voice}"', auto_text)
        self.assertIn("--payment-finished-prompt", auto_text)

    def test_checkout_keywords_cover_more_short_payment_phrases(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        for keyword in ["结一下账", "帮我结账", "我要结账了", "算一下多少钱", "可以付款了", "我要付款", "我要买单", "支付一下"]:
            with self.subTest(keyword=keyword):
                self.assertIn(keyword, text)

    def test_voice_recording_windows_are_long_enough_for_checkout(self):
        speaker_config = ROOT / "scripts" / "configure_board_voice_speaker.sh"
        auto_listen = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(speaker_config.exists())
        self.assertTrue(auto_listen.exists())

        speaker_text = speaker_config.read_text(encoding="utf-8", errors="ignore")
        auto_text = auto_listen.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("export VOICE_WAKE_SECONDS=5", speaker_text)
        self.assertIn("export VOICE_COMMAND_SECONDS=7", speaker_text)
        self.assertIn("export VOICE_LOOP_PAUSE_SECONDS=0.1", speaker_text)
        self.assertIn('VOICE_WAKE_SECONDS="${VOICE_WAKE_SECONDS:-5}"', auto_text)
        self.assertIn('VOICE_COMMAND_SECONDS="${VOICE_COMMAND_SECONDS:-7}"', auto_text)

    def test_voice_state_keeps_utf8_chinese_for_ui(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertNotIn("CHINESE REPLY PLAYED", text)
        self.assertNotIn("VOICE INPUT", text)
        self.assertIn('question="$(printf', text)
        self.assertIn('answer="$(printf', text)

    def test_voiceask_speaker_can_extract_wake_word_commands(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("is_wake_text()", text)
        self.assertIn("extract_wake_command()", text)
        self.assertIn("run_wake_once()", text)
        self.assertIn("WAKE_ACK_TEXT", text)
        self.assertIn("play_wake_ack", text)
        self.assertIn("Wake word detected", text)
        self.assertIn("Retail command detected without wake word.", text)
        self.assertIn('voice_cart_command "$wake_text"', text)
        self.assertIn('voice_payment_method_command "$wake_text"', text)
        self.assertIn("run_voice_question_once", text)
        for wake_variant in ["小志", "晓智", "小芝", "小只", "信息机", "智能售后"]:
            with self.subTest(wake_variant=wake_variant):
                self.assertIn(wake_variant, text)

    def test_voiceask_uses_cached_wake_ack_and_two_minute_session(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("WAKE_ACK_WAV", text)
        self.assertIn("build_wake_ack_audio()", text)
        self.assertIn("play_wake_ack()", text)
        self.assertIn("run_active_session()", text)
        self.assertIn("VOICE_SESSION_SECONDS", text)
        self.assertIn("session_end=$((now + session_seconds))", text)
        self.assertIn("run_voice_question_once", text)
        self.assertNotIn("play_text_tts \"$WAKE_ACK_TEXT\" || true", text)

    def test_wake_recognition_stdout_contains_only_asr_text(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn('echo "Listening for ${label}. Recording ${seconds} seconds..." >&2', text)

    def test_voiceask_prompt_uses_actual_command_seconds(self):
        script = ROOT / "scripts" / "run_voiceask_speaker.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("display_seconds=", text)
        self.assertIn('echo "Please speak now. Recording ${display_seconds} seconds..."', text)

    def test_speaker_config_uses_slightly_louder_lp_ln_volume(self):
        script = ROOT / "scripts" / "configure_board_voice_speaker.sh"
        self.assertTrue(script.exists())
        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("export VOICE_PLAYBACK_PATH=3", text)
        self.assertIn("export VOICE_SPK_VOLUME=200", text)
        self.assertIn("export VOICE_GAIN_DB=1", text)
        self.assertIn("export VOICE_SECONDS=4", text)
        self.assertIn("export VOICE_WAKE_SECONDS=5", text)
        self.assertIn("export VOICE_COMMAND_SECONDS=7", text)
        self.assertIn("export VOICE_LOOP_PAUSE_SECONDS=0.1", text)
        self.assertIn("export VOICE_SESSION_SECONDS=60", text)
        self.assertIn('export WAKE_ACK_TEXT="我在"', text)


if __name__ == "__main__":
    unittest.main()
