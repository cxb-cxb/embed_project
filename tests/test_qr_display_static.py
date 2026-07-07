from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "qr_scanner_display.c"


class QrDisplayStaticTests(unittest.TestCase):
    def test_retail_overlay_is_drawn_after_camera_frame_conversion(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        function_pos = code.index("static void draw_retail_ui_overlay(")
        conversion_pos = code.index("nv12_to_xrgb_scaled_rect(yp, uvp, cam_w, cam_h")
        call_pos = code.index("draw_retail_ui_overlay(last, found,")

        self.assertGreater(function_pos, 0)
        self.assertLess(
            call_pos,
            conversion_pos,
            "Retail UI modules should be drawn before the camera viewport is refreshed.",
        )

    def test_camera_frame_is_rendered_into_fixed_dashboard_viewport(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("static void dashboard_layout(", code)
        self.assertIn("nv12_to_xrgb_scaled_rect(", code)
        self.assertNotIn("nv12_to_xrgb_scaled(yp, uvp, cam_w, cam_h", code)
        self.assertIn("draw_dashboard_background(", code)
        self.assertIn("CAMERA_X", code)

    def test_dashboard_modules_are_not_redrawn_unconditionally_each_frame(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        loop_pos = code.index("while (g_running)")
        conversion_pos = code.index("nv12_to_xrgb_scaled_rect(yp, uvp, cam_w, cam_h", loop_pos)
        frame_setup = code[loop_pos:conversion_pos]

        self.assertIn("int redraw_dashboard = 1;", code)
        self.assertIn("if (redraw_dashboard ||", frame_setup)
        self.assertIn("redraw_dashboard = 0;", frame_setup)
        self.assertNotIn("draw_dashboard_background();\n            nv12_to_xrgb_scaled_rect", frame_setup)

    def test_retail_overlay_contains_competition_ui_labels(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        assets = (ROOT / "src" / "retail_ui_assets.h").read_text(encoding="utf-8", errors="ignore")

        for label in [
            "智慧零售终端",
            "摄像头",
            "21fps",
            "语音",
            "可识别商品分类",
            "购物车清单",
            "扫码支付",
            "客户：多少钱？",
            "助手：矿泉水",
        ]:
            with self.subTest(label=label):
                self.assertIn(label, code)

        for symbol in [
            "UI_GLYPHS",
            "UI_ICON_WATER",
            "UI_ICON_COLA",
            "UI_ICON_MILK",
            "UI_ICON_BREAD",
            "UI_ICON_NOODLE",
            "UI_ICON_CHIPS",
            "UI_ICON_BISCUIT",
            "UI_ICON_TOOTHPASTE",
            "UI_ICON_TISSUE",
            "UI_ICON_SOAP",
        ]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, assets)

    def test_reference_style_sections_are_explicitly_drawn(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for fn in [
            "draw_top_status_bar(",
            "draw_product_categories(",
            "draw_cart_table(",
            "draw_payment_panel(",
            "draw_voice_dialog_panel(",
        ]:
            with self.subTest(fn=fn):
                self.assertIn(fn, code)

    def test_landscape_canvas_is_rotated_to_portrait_lvds(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for symbol in [
            "g_ui_map",
            "g_ui_w",
            "g_ui_h",
            "display_prepare_landscape_canvas(",
            "display_flush_landscape_canvas(",
            "rotate_landscape_to_portrait(",
            "g_ui_w = g_drm_h",
            "g_ui_h = g_drm_w",
        ]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, code)

        self.assertIn("nv12_to_xrgb_scaled_rect(yp, uvp, cam_w, cam_h", code)
        self.assertIn("(uint32_t *)g_ui_map", code)
        self.assertIn("int px = dst_w - 1 - y;", code)
        self.assertIn("int py = x;", code)

    def test_retail_overlay_avoids_large_full_width_fills(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("draw_dashboard_background();", code)
        self.assertIn("fill_rect_rgb(fb, fw, fh, 0, 0, fw, fh", code)
        self.assertIn("安全认证，请放心使用", code)
        self.assertIn("遇到问题？点帮助或呼叫店员", code)

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

    def test_dynamic_cart_and_voice_state_are_used_by_ui(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for symbol in [
            "VOICE_STATE_FILE",
            "/tmp/qsm_retail_voice_state",
            "struct retail_cart_line",
            "retail_cart_add_product(",
            "retail_apply_voice_state(",
            "draw_cart_table(side_x",
            "draw_voice_dialog_panel(24",
        ]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, code)

        self.assertNotIn("(void)product;\n    (void)qr_count;", code)

    def test_qr_decode_updates_cart_and_speaks(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        decode_pos = code.index("quirc_decode(&code, &data)")
        qr_block = code[decode_pos: decode_pos + 2200]

        self.assertIn("retail_product_from_payload(", qr_block)
        self.assertIn("retail_cart_add_product(product);", qr_block)
        self.assertIn("retail_speak_product_added(product);", qr_block)
        self.assertIn("redraw_dashboard = 1;", qr_block)

    def test_voice_panel_uses_scroll_history_and_qr_repeat_can_accumulate(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for symbol in [
            "VOICE_HISTORY_LINES",
            "g_voice_history",
            "retail_push_voice_history(",
            "draw_voice_history_panel(",
            "draw_text_utf8_wrapped_rgb(",
            "问：",
            "答：",
            "g_last_qr_payload",
            "g_last_qr_add_ms",
            "QR_REPEAT_ADD_COOLDOWN_MS",
            "same_qr_can_add",
        ]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, code)


if __name__ == "__main__":
    unittest.main()
