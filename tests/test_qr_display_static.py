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

    def test_checkout_payment_summary_contains_order_and_amount(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("ORDER:QSM%04u", code)
        self.assertIn("order=QSM%04u&amount=%d", code)
        self.assertIn("retail_create_payment_order();", code)

    def test_qr_seen_flag_resets_each_frame(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        loop_pos = code.index("while (g_running) {")
        reset_pos = code.index("saw_qr_this_frame = 0;", loop_pos)
        grab_pos = code.index("camera_grab(&idx, &yp, &yl, &uvp, &uvl)", loop_pos)

        self.assertLess(
            reset_pos,
            grab_pos,
            "The per-frame QR seen flag must reset before camera_grab so removing "
            "a QR from the scan area releases duplicate suppression.",
        )

    def test_current_product_qr_ids_are_mapped(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for product_id in [
            "cola",
            "noodle",
            "chips",
            "biscuit",
            "milk",
            "bread",
            "toothpaste",
            "water",
            "tissue",
            "soap",
        ]:
            self.assertIn(f'{{"{product_id}",', code)

        self.assertIn('equals_ignore_case(value, "mineral_water")', code)
        self.assertIn('equals_ignore_case(value, "instant_noodles")', code)


if __name__ == "__main__":
    unittest.main()
