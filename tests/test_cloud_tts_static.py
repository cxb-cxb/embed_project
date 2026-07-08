import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CloudTtsStaticTests(unittest.TestCase):
    def test_cpp_tts_playback_holds_shared_voice_input_lock(self):
        source = ROOT / "src" / "cloud_tts.cpp"
        header = ROOT / "include" / "cloud_tts.hpp"
        self.assertTrue(source.exists())
        self.assertTrue(header.exists())

        source_text = source.read_text(encoding="utf-8", errors="ignore")
        header_text = header.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("buildPlayCommand", header_text)
        self.assertIn("buildPlayCommand", source_text)
        self.assertIn("/tmp/qsm_tts_playing", source_text)
        self.assertIn("lock_owner=$$", source_text)
        self.assertIn("sleep ${VOICE_TTS_POST_DELAY_SECONDS:-2.5}", source_text)
        self.assertIn("current_owner=$(cat", source_text)
        self.assertIn("$lock_file", source_text)
        self.assertIn("$current_owner", source_text)
        self.assertIn("$lock_owner", source_text)
        self.assertIn("CloudTtsClient::buildPlayCommand(audio_path)", source_text)


if __name__ == "__main__":
    unittest.main()
