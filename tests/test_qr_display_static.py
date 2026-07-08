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

    def test_qr_outline_is_drawn_for_known_product_before_cooldown_gate(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        extract_pos = code.index("quirc_extract(qr, i, &code);")
        decode_pos = code.index("quirc_decode(&code, &data)", extract_pos)
        product_pos = code.index("const struct retail_product *product", decode_pos)
        gate_pos = code.index("retail_product_can_add_from_qr(product, qr_now_ms)", product_pos)
        draw_pos = code.index("draw_qr_outline(&code, cam_w, cam_h);", extract_pos)

        self.assertGreater(
            draw_pos,
            decode_pos,
            "QR outline should only be drawn for decoded product QR codes.",
        )
        self.assertGreater(
            draw_pos,
            product_pos,
            "Unknown QR payloads should not show the green product outline.",
        )
        self.assertLess(
            draw_pos,
            gate_pos,
            "Duplicate product QR scans inside the cooldown should still show recognition outline.",
        )

    def test_checkout_payment_summary_contains_order_and_amount(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn('snprintf(g_order_id, sizeof(g_order_id), "%06d"', code)
        self.assertIn("order=%s&amount=%d", code)
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
        self.assertIn("retail_payload_extract_value(", code)
        self.assertIn('"product="', code)
        self.assertIn('"id="', code)
        self.assertIn('"sku="', code)
        self.assertIn('"\\"product\\""', code)
        self.assertIn('"\\"id\\""', code)

    def test_cola_product_is_disabled_from_qr_payload_recognition(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("retail_product_is_enabled(", code)
        self.assertIn('equals_ignore_case(product->id, "cola")', code)
        self.assertIn("return 0;", code)

        from_payload_pos = code.index("static const struct retail_product *retail_product_from_payload(")
        from_payload_block = code[from_payload_pos: from_payload_pos + 1400]
        self.assertIn("if (!retail_product_is_enabled(&g_products[i])) continue;", from_payload_block)
        self.assertIn('{"cola", "Cola", "690100000002", 350}', code)

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

    def test_checkout_voice_command_creates_payment_summary(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn('equals_ignore_case(cmd, "checkout")', code)
        self.assertIn("retail_checkout_summary(", code)
        self.assertIn("g_checkout_requested", code)
        self.assertIn("retail_create_payment_order();", code)
        self.assertIn("PAYMENT READY", code)

    def test_payment_method_overlay_uses_real_qr_assets_and_timeout(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for symbol in [
            "PAYMENT_QR_W",
            "PAYMENT_QR_H",
            "PAYMENT_POPUP_MS",
            "PAYMENT_WECHAT_BGRA",
            "PAYMENT_ALIPAY_BGRA",
            "g_payment_method",
            "g_payment_popup_until_ms",
            "load_payment_qr_assets(",
            "draw_payment_popup(",
            "retail_hide_payment_popup(",
            "pay:wechat",
            "pay:alipay",
            "pay:unionpay",
            "该支付方式暂不可用",
        ]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, code)

    def test_payment_popup_timeout_resets_checkout_and_cart_after_thirty_seconds(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("#define PAYMENT_POPUP_MS 30000", code)
        self.assertIn("retail_finish_payment_and_reset();", code)

        timeout_pos = code.index("if (g_payment_popup_until_ms > 0 && now_ms() >= g_payment_popup_until_ms)")
        timeout_block = code[timeout_pos: timeout_pos + 260]
        self.assertIn("retail_finish_payment_and_reset();", timeout_block)
        self.assertIn("redraw_dashboard = 1;", timeout_block)

        reset_pos = code.index("static void retail_finish_payment_and_reset(void)\n{")
        reset_block = code[reset_pos: reset_pos + 900]
        self.assertIn("retail_cart_clear();", reset_block)
        self.assertIn("retail_hide_payment_popup();", reset_block)
        self.assertIn("unlink(PAYMENT_WAIT_FILE);", reset_block)
        self.assertIn("unlink(VOICE_STATE_FILE);", reset_block)
        self.assertIn("g_checkout_requested = 0;", reset_block)
        self.assertIn("g_last_total_cents = 0;", reset_block)
        self.assertIn("\"PAY: scan checkout\"", reset_block)
        self.assertIn("g_order_id[0] = '\\0';", reset_block)

    def test_payment_popup_countdown_is_large_and_outside_qr_panel(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        popup_pos = code.index("static void draw_payment_popup(void)")
        popup_block = code[popup_pos: popup_pos + 3200]
        self.assertIn("countdown_x = panel_x + panel_w + 28", popup_block)
        self.assertIn("countdown_y = panel_y + 96", popup_block)
        self.assertIn('"请扫码完成支付，稍后自动返回"', popup_block)
        self.assertNotIn("30秒后自动返回", popup_block)
        self.assertNotIn("60秒后自动返回", popup_block)
        self.assertIn('snprintf(remain_text, sizeof(remain_text), "%02dS", remain)', popup_block)
        self.assertIn("remain_text, 5, theme", popup_block)

    def test_payment_reset_signals_voice_completion_prompt(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn('#define PAYMENT_FINISHED_VOICE_FILE "/tmp/qsm_payment_finished_voice"', code)
        reset_pos = code.index("static void retail_finish_payment_and_reset(void)\n{")
        reset_block = code[reset_pos: reset_pos + 1100]
        self.assertIn("fopen(PAYMENT_FINISHED_VOICE_FILE, \"w\")", reset_block)
        self.assertIn("fputs(\"payment_finished\\n\", finish_voice)", reset_block)
        self.assertIn("fclose(finish_voice)", reset_block)

    def test_voice_cart_commands_are_consumed_once(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn('#define PAYMENT_WAIT_FILE "/tmp/qsm_payment_waiting_method"', code)
        self.assertIn("static void retail_consume_voice_state_command(void)\n{", code)

        consume_pos = code.index("static void retail_consume_voice_state_command(void)\n{")
        consume_block = code[consume_pos: consume_pos + 220]
        self.assertIn("unlink(VOICE_STATE_FILE);", consume_block)
        self.assertIn("g_voice_state_mtime = 0;", consume_block)

        apply_pos = code.index("retail_apply_voice_cart_command(cart_cmd);")
        apply_block = code[apply_pos: apply_pos + 180]
        self.assertIn("if (cart_cmd[0])", apply_block)
        self.assertIn("retail_consume_voice_state_command();", apply_block)

    def test_enter_key_completes_payment_popup_immediately(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("#include <sys/select.h>", code)
        self.assertIn('#define PAYMENT_DONE_FILE "/tmp/qsm_payment_done"', code)
        self.assertIn("static int retail_stdin_enter_pressed(void)", code)
        self.assertIn("static int retail_payment_done_requested(void)", code)
        self.assertIn("FD_SET(STDIN_FILENO", code)
        self.assertIn("fgets(line, sizeof(line), stdin)", code)
        self.assertIn("access(PAYMENT_DONE_FILE, F_OK)", code)
        self.assertIn("unlink(PAYMENT_DONE_FILE);", code)

        loop_pos = code.index("if (g_payment_popup_until_ms > 0 && now_ms() >= g_payment_popup_until_ms)")
        loop_block = code[loop_pos: loop_pos + 520]
        self.assertIn("retail_payment_done_requested()", loop_block)
        self.assertIn("retail_finish_payment_and_reset();", loop_block)
        self.assertIn("redraw_dashboard = 1;", loop_block)

    def test_qr_repeat_add_cooldown_uses_twenty_second_product_gate(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        self.assertIn("#define QR_PRODUCT_ADD_COOLDOWN_MS 20000", code)
        self.assertIn("g_product_last_add_ms", code)
        self.assertIn("retail_product_can_add_from_qr(product, qr_now_ms)", code)
        self.assertNotIn("#define QR_REPEAT_ADD_COOLDOWN_MS 800", code)

    def test_qr_outline_is_drawn_after_successful_product_decode_before_cooldown(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        decode_pos = code.index("static int decode_qr_candidates(")
        decode_block = code[decode_pos: decode_pos + 2600]

        draw_pos = decode_block.index("draw_qr_outline(&code, cam_w, cam_h);")
        success_pos = decode_block.index("if (quirc_decode(&code, &data) == QUIRC_SUCCESS)")
        product_pos = decode_block.index("const struct retail_product *product")
        can_add_pos = decode_block.index("retail_product_can_add_from_qr(product, qr_now_ms)")
        self.assertGreater(draw_pos, success_pos)
        self.assertGreater(draw_pos, product_pos)
        self.assertLess(draw_pos, can_add_pos)

    def test_qr_decode_updates_cart_and_speaks(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        decode_pos = code.index("quirc_decode(&code, &data)")
        qr_block = code[decode_pos: decode_pos + 2200]

        self.assertIn("retail_product_from_payload(", qr_block)
        self.assertIn("retail_cart_add_product(product);", qr_block)
        self.assertIn("retail_speak_product_added(product);", qr_block)
        self.assertIn("redraw_dashboard = 1;", qr_block)

    def test_qr_luma_copy_respects_camera_stride(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("static void copy_luma_for_qr(", code)
        copy_pos = code.index("static void copy_luma_for_qr(")
        copy_block = code[copy_pos: copy_pos + 500]
        self.assertIn("memcpy(dst + y * width, src + y * stride, width)", copy_block)
        self.assertNotIn("memcpy(qb, yp, cam_w * cam_h)", code)

    def test_qr_decode_has_enhanced_fallback_for_product_labels(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("static void enhance_luma_for_qr(", code)
        self.assertIn("struct quirc *qr_enhanced = quirc_new();", code)
        self.assertIn("quirc_resize(qr_enhanced, cam_w, cam_h)", code)
        self.assertIn("decode_qr_candidates(qr,", code)
        self.assertIn("decode_qr_candidates(qr_enhanced,", code)
        self.assertIn("enhance_luma_for_qr(qb2, yp, cam_w, cam_h, g_y_stride)", code)

    def test_qr_decode_has_center_crop_fallback_for_real_product_photos(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("crop_enhance_luma_for_qr(", code)
        self.assertIn("struct quirc *qr_crop = quirc_new();", code)
        self.assertIn("quirc_resize(qr_crop, cam_w, cam_h)", code)
        self.assertIn("crop_enhance_luma_for_qr(qb3, yp, cam_w, cam_h, g_y_stride, 88)", code)
        self.assertIn('continuous, "center88")', code)

    def test_qr_decode_rotates_shifted_crop_fallbacks_for_edge_labels(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        self.assertIn("#define QR_SHIFTED_CROP_COUNT", code)
        self.assertIn("struct qr_crop_region", code)
        self.assertIn("g_qr_shifted_crops", code)
        self.assertIn("crop_region_enhance_luma_for_qr(", code)
        self.assertIn("g_qr_shifted_crops[frame % QR_SHIFTED_CROP_COUNT]", code)
        self.assertIn('snprintf(pass_name, sizeof(pass_name), "shift%d"', code)

    def test_voice_panel_uses_scroll_history_and_qr_product_cooldown(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")

        for symbol in [
            "VOICE_HISTORY_LINES",
            "g_voice_history",
            "retail_push_voice_history(",
            "draw_voice_history_panel(",
            "draw_text_utf8_wrapped_rgb(",
            "客户：",
            "AI：",
            "g_product_last_add_ms",
            "QR_PRODUCT_ADD_COOLDOWN_MS",
            "retail_product_can_add_from_qr(",
            "retail_product_index(",
        ]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, code)

    def test_voice_panel_labels_customer_and_ai_and_sanitizes_mojibake(self):
        code = SRC.read_text(encoding="utf-8", errors="ignore")
        assets = (ROOT / "src" / "retail_ui_assets.h").read_text(encoding="utf-8", errors="ignore")

        self.assertIn("static void sanitize_voice_text(", code)
        self.assertIn("strip_voice_role_prefix(", code)
        self.assertIn("replace_all_inplace(", code)
        self.assertIn("#define VOICE_HISTORY_TEXT_MAX 192", code)
        self.assertIn("g_voice_history[VOICE_HISTORY_LINES][VOICE_HISTORY_TEXT_MAX]", code)
        self.assertIn("utf8_safe_copy(", code)
        self.assertIn("宸蹭负", code)
        self.assertIn("璇锋壂鐮", code)

        apply_pos = code.index("static int retail_apply_voice_state(void)")
        apply_block = code[apply_pos: apply_pos + 1600]
        self.assertIn("sanitize_voice_text(question", apply_block)
        self.assertIn("sanitize_voice_text(answer", apply_block)
        self.assertIn("char line_q[VOICE_HISTORY_TEXT_MAX]", apply_block)
        self.assertIn("char line_a[VOICE_HISTORY_TEXT_MAX]", apply_block)
        self.assertIn('format_prefixed_text(line_q, sizeof(line_q), "客户：", question)', apply_block)
        self.assertIn('format_prefixed_text(line_a, sizeof(line_a), "AI：", answer)', apply_block)
        self.assertNotIn('"问：%s"', apply_block)
        self.assertNotIn('"答：%s"', apply_block)

        payment_pos = code.index('if (equals_ignore_case(cmd, "pay:wechat"))')
        payment_block = code[payment_pos: payment_pos + 900]
        self.assertIn('retail_push_voice_history("客户：微信支付")', payment_block)
        self.assertIn('retail_push_voice_history("AI：已为您打开微信收款码，请扫码支付。")', payment_block)
        self.assertIn('retail_push_voice_history("客户：支付宝支付")', payment_block)
        self.assertIn('retail_push_voice_history("AI：已为您打开支付宝收款码，请扫码支付。")', payment_block)

        draw_pos = code.index("static void draw_voice_history_panel(")
        draw_block = code[draw_pos: draw_pos + 900]
        self.assertIn("voice_line_content(", draw_block)
        self.assertIn("draw_voice_label_rgb(", code)
        self.assertIn('draw_text_utf8_rgb(fb, fw, fh, x, y, "ＡＩ：", 1, color)', code)
        self.assertIn("return 58;", code)
        self.assertIn("draw_voice_text_wrapped_rgb(", draw_block)
        self.assertIn("draw_voice_codepoint_rgb(", code)
        self.assertIn("voice_fullwidth_codepoint(", code)
        self.assertIn("sanitize_voice_history_line(", code)
        self.assertIn("if (c == '?') continue;", code)
        self.assertNotIn('strstr(g_voice_history[i], "答：")', draw_block)

        punctuation_codepoints = [
            0x3001, 0x3002, 0xFF0C, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B,
            0xFF08, 0xFF09, 0x3010, 0x3011, 0x300A, 0x300B,
            0x201C, 0x201D, 0x2018, 0x2019, 0x300C, 0x300D,
            0x300E, 0x300F, 0x2014, 0x2026, 0xFFE5, 0x00B7, 0xFF5E,
        ]
        for cp in punctuation_codepoints:
            with self.subTest(punctuation_glyph=hex(cp)):
                self.assertIn(f"{{0x{cp:04X},", assets)


if __name__ == "__main__":
    unittest.main()
