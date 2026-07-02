from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "qr_scanner_display.c"


class QrDisplayStaticTests(unittest.TestCase):
    def test_qr_outline_is_drawn_before_payload_decode_gate(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        extract_pos = code.index("quirc_extract(qr, i, &code);")
        decode_pos = code.index("quirc_decode(&code, &data)", extract_pos)
        draw_pos = code.index("draw_qr_outline(&code, cam_w, cam_h);", extract_pos)

        self.assertLess(
            draw_pos,
            decode_pos,
            "QR outline must be drawn for every detected QR candidate, before "
            "payload decode/new-payload filtering can suppress drawing.",
        )


if __name__ == "__main__":
    unittest.main()
