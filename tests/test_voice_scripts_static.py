import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class VoiceAutoListenScriptTest(unittest.TestCase):
    def test_auto_listen_script_runs_voiceask_without_read_prompt(self):
        script = ROOT / "scripts" / "start_voice_auto_listen.sh"
        self.assertTrue(script.exists())

        text = script.read_text(encoding="utf-8")
        self.assertIn("run_voiceask_speaker.sh", text)
        self.assertIn("--once", text)
        self.assertIn("while true", text)
        self.assertNotIn("read -r", text)
        self.assertIn("trap", text)
        self.assertIn("welcome_tts.mp3", text)

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

    def test_speaker_config_uses_slightly_louder_lp_ln_volume(self):
        script = ROOT / "scripts" / "configure_board_voice_speaker.sh"
        self.assertTrue(script.exists())
        text = script.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("export VOICE_PLAYBACK_PATH=3", text)
        self.assertIn("export VOICE_SPK_VOLUME=200", text)
        self.assertIn("export VOICE_GAIN_DB=1", text)
        self.assertIn("export VOICE_SECONDS=4", text)


if __name__ == "__main__":
    unittest.main()
