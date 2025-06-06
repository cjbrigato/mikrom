#!/usr/bin/env python
# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

FUZZERS_WITH_CORPORA = [
    'aec3_config_json_fuzzer',
    'aec3_fuzzer',
    'agc_fuzzer',
    'angle_translator_fuzzer',
    'apdu_fuzzer',
    'audio_decoder_g722_fuzzer',
    'audio_decoder_ilbc_fuzzer',
    'audio_decoder_multiopus_fuzzer',
    'audio_decoder_opus_fuzzer',
    'audio_decoder_opus_redundant_fuzzer',
    'audio_decoder_pcm16b_fuzzer',
    'audio_decoder_pcm_fuzzer',
    'audio_encoder_opus_fuzzer',
    'audio_processing_fuzzer',
    'audio_processing_sample_rate_fuzzer',
    'autocomplete_input_fuzzer',
    'autofill_autocomplete_parsing_util_fuzzer',
    'autofill_legal_message_line_fuzzer',
    'autofill_phone_number_i18n_fuzzer',
    'ax_node_position_fuzzer',
    'ax_table_fuzzer',
    'ax_tree_fuzzer',
    'base32_fuzzer',
    'base64_decode_fuzzer',
    'base64_encode_fuzzer',
    'base64url_fuzzer',
    'base_json_reader_fuzzer',
    'base_json_string_escape_fuzzer',
    'bdict_fuzzer',
    'blink_harfbuzz_shaper_fuzzer',
    'blink_html_tokenizer_fuzzer',
    'blink_png_decoder_fuzzer',
    'blink_security_origin_fuzzer',
    'blink_text_codec_UTF_8_fuzzer',
    'blink_text_codec_WINDOWS_1252_fuzzer',
    'boringssl_arm_cpuinfo_fuzzer',
    'brotli_fuzzer',
    'cast_framer_serialize_fuzzer',
    'cast_message_util_fuzzer',
    'cbcs_decryptor_fuzzer',
    'charntorune_fuzzer',
    'cipher_encrypt_fuzzer',
    'clear_site_data_handler_fuzzer',
    'client_hints_fuzzer',
    'client_side_phishing_fuzzer',
    'code_cache_host_mojolpm_fuzzer',
    'color_analysis_fuzzer',
    'color_parser_fuzzer',
    'color_transform_fuzzer',
    'comfort_noise_decoder_fuzzer',
    'command_line_fuzzer',
    'compact_enc_det_fuzzer',
    'content_security_policy_conversion_util_fuzzer',
    'content_security_policy_util_fuzzer',
    'content_sms_parser_fuzzer',
    'convert_woff2ttf_fuzzer',
    'create_trial_from_study_fuzzer',
    'create_trials_from_seed_fuzzer_v2',
    'css_parser_fast_paths_fuzzer',
    'ctap_response_fuzzer',
    'cypher_decrypt_fuzzer',
    'cypher_encrypt_with_key_fuzzer',
    'cypher_reencrypt_fuzzer',
    'd2d_connection_context_client_fuzzer',
    'd2d_connection_context_server_fuzzer',
    'dawn_wire_server_and_frontend_fuzzer',
    'devtools_protocol_encoding_cbor_fuzzer',
    'devtools_protocol_encoding_json_fuzzer',
    'dial_internal_message_fuzzer',
    'disk_cache_lpm_fuzzer',
    'document_policy_fuzzer',
    'dpf_fuzzer',
    'expat_xml_parse_fuzzer',
    'extension_csp_validator_fuzzer',
    'extension_declarative_net_request_indexed_rule_fuzzer',
    'extension_file_highlighter_fuzzer',
    'extension_fuzzer',
    'extension_management_internal_fuzzer',
    'extension_manifest_fuzzer',
    'extension_url_pattern_fuzzer',
    'extension_web_request_form_data_parser_fuzzer',
    'favicon_url_parser_fuzzer',
    'feature_policy_fuzzer',
    'fido_ble_frames_fuzzer',
    'fido_cable_handshake_handler_fuzzer',
    'fido_hid_message_fuzzer',
    'field_trial_fuzzer',
    'file_system_manager_mojolpm_fuzzer',
    'fingerprint_fuzzer',
    'first_party_set_parser_fuzzer',
    'first_party_set_parser_json_fuzzer',
    'first_party_sets_overrides_policy_handler_fuzzer',
    'flac_audio_handler_fuzzer',
    'flatbuffers_verifier_fuzzer',
    'flexfec_receiver_fuzzer',
    'flexfec_sender_fuzzer',
    'form_structure_process_query_response_fuzzer',
    'frame_buffer_fuzzer',
    'freetype_bdf_fuzzer',
    'freetype_cff_ftengine_fuzzer',
    'freetype_cff_render_fuzzer',
    'freetype_cidtype1_fuzzer',
    'freetype_cidtype1_render_fuzzer',
    'freetype_glyphs_outlines_fuzzer',
    'freetype_truetype_fuzzer',
    'freetype_truetype_render_i35_fuzzer',
    'freetype_truetype_render_i38_fuzzer',
    'freetype_type1_ftengine_fuzzer',
    'freetype_type1_fuzzer',
    'freetype_type1_render_ftengine_fuzzer',
    'fuzzy_finder_fuzzer',
    'gfx_png_image_fuzzer',
    'gl_lpm_fuzzer',
    'gpu_fuzzer',
    'gpu_swangle_passthrough_fuzzer',
    'h264_annex_b_converter_fuzzer',
    'h264_bitstream_parser_fuzzer',
    'hb_shape_fuzzer',
    'hb_subset_fuzzer',
    'hid_report_descriptor_fuzzer',
    'hit_test_query_fuzzer',
    'hls_attribute_list_fuzzer',
    'hls_items_fuzzer',
    'hls_media_playlist_fuzzer',
    'hls_multivariant_playlist_fuzzer',
    'hunspell_suggest_fuzzer',
    'icu_appendable_fuzzer',
    'icu_break_iterator_fuzzer',
    'icu_break_iterator_utf32_fuzzer',
    'icu_converter_fuzzer',
    'icu_number_format_fuzzer',
    'icu_to_case_fuzzer',
    'icu_ucasemap_fuzzer',
    'icu_uregex_open_fuzzer',
    'identifiable_token_builder_atomic_fuzzer',
    'identifiable_token_builder_fuzzer',
    'indexed_db_leveldb_coding_decodeidbkey_fuzzer',
    'indexed_db_leveldb_coding_decodeidbkeypath_fuzzer',
    'indexed_db_leveldb_coding_encodeidbkey_fuzzer',
    'indexed_db_leveldb_coding_encodeidbkeypath_fuzzer',
    'indexed_ruleset_fuzzer',
    'inspector_fuzzer',
    'json_web_key_fuzzer',
    'jsoncpp_fuzzer',
    'layout_locale_fuzzer',
    'leveldb_put_get_delete_fuzzer',
    'libaddressinput_address_formatter_fuzzer',
    'libaddressinput_parse_address_fields_fuzzer',
    'libaddressinput_parse_format_rule_fuzzer',
    'libaom_av1_dec_fuzzer',
    'libpng_progressive_read_fuzzer',
    'libpng_read_fuzzer',
    'liburlpattern_fuzzer',
    'libwebp_advanced_api_fuzzer',
    'libwebp_animation_api_fuzzer',
    'libwebp_animencoder_fuzzer',
    'libwebp_enc_dec_api_fuzzer',
    'libwebp_mux_demux_api_fuzzer',
    'libwebp_simple_api_fuzzer',
    'libxml_xml_read_memory_fuzzer',
    'libyuv_scale_fuzzer',
    'lookup_affiliation_response_parser_fuzzer',
    'math_transform_fuzzer',
    'mathml_operator_dictionary_fuzzer',
    'media_av1_decoder_fuzzer',
    'media_bit_reader_fuzzer',
    'media_capabilities_fuzzer',
    'media_cenc_utils_fuzzer',
    'media_es_parser_h264_fuzzer',
    'media_es_parser_mpeg1audio_fuzzer',
    'media_h264_parser_fuzzer',
    'media_h265_decoder_fuzzer',
    'media_h265_parser_fuzzer',
    'media_jpeg_parser_picture_fuzzer',
    'media_mp4_box_reader_fuzzer',
    'media_vp9_parser_encrypted_fuzzer',
    'media_vp9_parser_fuzzer',
    'media_vpx_quantizer_parser_fuzzer',
    'media_vpx_video_decoder_fuzzer',
    'media_webm_muxer_fuzzer',
    'mediasource_ADTS_pipeline_integration_fuzzer',
    'mediasource_MP2T_AACLC_AVC_pipeline_integration_fuzzer',
    'mediasource_MP2T_AACSBR_pipeline_integration_fuzzer',
    'mediasource_MP2T_MP3_pipeline_integration_fuzzer',
    'mediasource_MP3_pipeline_integration_fuzzer',
    'mediasource_MP4_AACLC_pipeline_integration_fuzzer',
    'mediasource_MP4_AACSBR_pipeline_integration_fuzzer',
    'mediasource_MP4_AV1_pipeline_integration_fuzzer',
    'mediasource_MP4_AVC1_pipeline_integration_fuzzer',
    'mediasource_MP4_FLAC_pipeline_integration_fuzzer',
    'mediasource_WEBM_OPUS_pipeline_integration_fuzzer',
    'mediasource_WEBM_VP8_pipeline_integration_fuzzer',
    'mediasource_WEBM_VP9_pipeline_integration_fuzzer',
    'merkle_integrity_source_stream_fuzzer',
    'mhtml_parser_fuzzer',
    'midi_message_queue_fuzzer',
    'midi_webmidi_data_validator_fuzzer',
    'mojo_core_node_channel_fuzzer',
    'mojo_core_port_event_fuzzer',
    'mojo_core_user_message_fuzzer',
    'net_auth_challenge_tokenizer_fuzzer',
    'net_backoff_entry_serializer_fuzzer',
    'net_base_address_tracker_linux_fuzzer',
    'net_base_schemeful_site_fuzzer',
    'net_canonical_cookie_fuzzer',
    'net_cert_crl_parse_crl_certificatelist_fuzzer',
    'net_cert_crl_parse_issuing_distribution_point_fuzzer',
    'net_cert_ct_decode_signed_certificate_timestamp_fuzzer',
    'net_cert_normalize_name_fuzzer',
    'net_cert_ocsp_parse_ocsp_cert_id_fuzzer',
    'net_cert_ocsp_parse_ocsp_response_data_fuzzer',
    'net_cert_ocsp_parse_ocsp_response_fuzzer',
    'net_cert_parse_authority_key_identifier_fuzzer',
    'net_cert_verify_name_match_fuzzer',
    'net_cookie_partition_key_fuzzer',
    'net_cookie_util_parsing_fuzzer',
    'net_crl_set_fuzzer',
    'net_data_url_fuzzer',
    'net_der_parser_fuzzer',
    'net_dns_host_cache_fuzzer',
    'net_dns_hosts_parse_fuzzer',
    'net_dns_https_record_rdata_fuzzer',
    'net_dns_nsswitch_reader_fuzzer',
    'net_dns_query_parse_fuzzer',
    'net_dns_response_fuzzer',
    'net_gzip_source_stream_fuzzer',
    'net_host_resolver_manager_fuzzer',
    'net_http2_frame_decoder_fuzzer',
    'net_http_auth_handler_basic_fuzzer',
    'net_http_auth_handler_digest_fuzzer',
    'net_http_auth_handler_fuzzer',
    'net_http_chunked_decoder_fuzzer',
    'net_http_content_disposition_fuzzer',
    'net_http_security_headers_hsts_fuzzer',
    'net_http_server_fuzzer',
    'net_http_stream_parser_fuzzer',
    'net_lookup_string_in_fixed_set_fuzzer',
    'net_mime_sniffer_fuzzer',
    'net_ntlm_ntlm_client_fuzzer',
    'net_parse_cookie_line_fuzzer',
    'net_parse_proxy_bypass_rules_fuzzer',
    'net_parse_proxy_list_pac_fuzzer',
    'net_parse_proxy_rules_fuzzer',
    'net_parse_url_hostname_to_address_fuzzer',
    'net_qpack_decoder_fuzzer',
    'net_qpack_encoder_stream_receiver_fuzzer',
    'net_qpack_encoder_stream_sender_fuzzer',
    'net_qpack_round_trip_fuzzer',
    'net_quic_crypto_framer_parse_message_fuzzer',
    'net_quic_framer_fuzzer',
    'net_quic_framer_process_data_packet_fuzzer',
    'net_quic_session_pool_fuzzer',
    'net_socks5_client_socket_fuzzer',
    'net_socks_client_socket_fuzzer',
    'net_structured_headers_fuzzer',
    'net_unescape_url_component_fuzzer',
    'net_web_socket_encoder_fuzzer',
    'net_websocket_deflate_stream_fuzzer',
    'net_websocket_extension_parser_fuzzer',
    'net_websocket_frame_parser_fuzzer',
    'neteq_rtp_fuzzer',
    'neteq_signal_fuzzer',
    'network_content_security_policy_fuzzer',
    'omnibox_view_fuzzer',
    'open_type_math_support_fuzzer',
    'openscreen_cast_auth_util_fuzzer',
    'openscreen_message_framer_deserialize_fuzzer',
    'openscreen_message_framer_serialize_fuzzer',
    'optimization_guide_page_topics_fuzzer',
    'optimization_guide_page_visibility_model_fuzzer',
    'origin_trial_token_fuzzer',
    'ots_fuzzer',
    'paint_op_buffer_fuzzer',
    'parse_os_header_fuzzer',
    'password_manager_form_data_parser_fuzzer',
    'password_manager_form_data_parser_proto_fuzzer',
    'payment_method_manifest_fuzzer',
    'payment_web_app_manifest_fuzzer',
    'pdf_bidi_fuzzer',
    'pdf_cfgas_stringformatter_fuzzer',
    'pdf_cfx_barcode_fuzzer',
    'pdf_cjs_util_fuzzer',
    'pdf_cmap_fuzzer',
    'pdf_codec_a85_fuzzer',
    'pdf_codec_bmp_fuzzer',
    'pdf_codec_fax_fuzzer',
    'pdf_codec_gif_fuzzer',
    'pdf_codec_icc_fuzzer',
    'pdf_codec_jbig2_fuzzer',
    'pdf_codec_jpeg_fuzzer',
    'pdf_codec_png_fuzzer',
    'pdf_codec_rle_fuzzer',
    'pdf_codec_tiff_fuzzer',
    'pdf_cpdf_tounicodemap_fuzzer',
    'pdf_css_fuzzer',
    'pdf_dates_fuzzer',
    'pdf_font_fuzzer',
    'pdf_formcalc_context_fuzzer',
    'pdf_formcalc_fuzzer',
    'pdf_formcalc_translate_fuzzer',
    'pdf_fx_date_helpers_fuzzer',
    'pdf_hint_table_fuzzer',
    'pdf_jpx_fuzzer',
    'pdf_lzw_fuzzer',
    'pdf_nametree_fuzzer',
    'pdf_psengine_fuzzer',
    'pdf_scanlinecompositor_fuzzer',
    'pdf_streamparser_fuzzer',
    'pdf_xfa_fdp_fuzzer',
    'pdf_xfa_raw_fuzzer',
    'pdf_xfa_xdp_fdp_fuzzer',
    'pdf_xml_fuzzer',
    'pdfium_fuzzer',
    'pdfium_xfa_fuzzer',
    'permissions_policy_attr_fuzzer',
    'permissions_policy_fuzzer',
    'pffft_complex_fuzzer',
    'pffft_real_fuzzer',
    'pickle_fuzzer',
    'pix_code_validator_fuzzer',
    'policy_schema_fuzzer',
    'preg_parser_fuzzer',
    'prtime_fuzzer',
    'pseudotcp_parser_fuzzer',
    'qr_code_generator_fuzzer',
    'reader_fuzzer',
    'redaction_tool_fuzzer',
    'remoting_protobuf_http_stream_parser_fuzzer',
    'render_text_api_fuzzer',
    'render_text_fuzzer',
    'renderer_proto_tree_fuzzer',
    'residual_echo_detector_fuzzer',
    'rtcp_receiver_fuzzer',
    'rtp_depacketizer_av1_assemble_frame_fuzzer',
    'rtp_dependency_descriptor_fuzzer',
    'rtp_frame_reference_finder_fuzzer',
    'rtp_packetizer_av1_fuzzer',
    'rtp_video_frame_assembler_fuzzer',
    'rtp_video_layers_allocation_fuzzer',
    'sctp_utils_fuzzer',
    'sdp_parser_fuzzer',
    'search_suggestion_parser_fuzzer',
    'seven_zip_reader_fuzzer',
    'sha1_fuzzer',
    'skia_image_filter_proto_fuzzer',
    'skia_path_fuzzer',
    'skia_pathop_fuzzer',
    'snappy_compress_fuzzer',
    'snappy_uncompress_fuzzer',
    'source_registration_fuzzer',
    'speech_audio_encoder_fuzzer',
    'sqlite3_ossfuzz_fuzzer',
    'sqlite3_select_strftime_lpm_fuzzer',
    'sqlite3_shadow_table_fuzzer',
    'ssl_certificate_fuzzer',
    'storage_key_proto_fuzzer',
    'storage_key_string_fuzzer',
    'stretchy_operator_shaper_fuzzer',
    'string_number_conversions_fuzzer',
    'string_pattern_fuzzer',
    'string_to_number_fuzzer',
    'string_tokenizer_fuzzer',
    'stun_parser_fuzzer',
    'stun_validator_fuzzer',
    'stylesheet_contents_fuzzer',
    'subresource_filter_rule_parser_fuzzer',
    'substring_set_matcher_fuzzer',
    'sys_string_conversions_fuzzer',
    'template_url_parser_fuzzer',
    'text_resource_decoder_fuzzer',
    'third_party_re2_fuzzer',
    'time_delta_from_string_fuzzer',
    'time_fuzzer',
    'tint_all_transforms_fuzzer',
    'tint_ast_clone_fuzzer',
    'tint_ast_hlsl_writer_fuzzer',
    'tint_ast_msl_writer_fuzzer',
    'tint_ast_spv_writer_fuzzer',
    'tint_ast_wgsl_writer_fuzzer',
    'tint_binding_remapper_fuzzer',
    'tint_first_index_offset_fuzzer',
    'tint_regex_hlsl_writer_fuzzer',
    'tint_regex_msl_writer_fuzzer',
    'tint_regex_spv_writer_fuzzer',
    'tint_regex_wgsl_writer_fuzzer',
    'tint_renamer_fuzzer',
    'tint_robustness_fuzzer',
    'tint_wgsl_reader_hlsl_writer_fuzzer',
    'tint_wgsl_reader_msl_writer_fuzzer',
    'tint_wgsl_reader_wgsl_writer_fuzzer',
    'transfer_cache_fuzzer',
    'trigger_registration_fuzzer',
    'trust_token_key_commitment_parser_fuzzer',
    'turn_unwrap_fuzzer',
    'ukey2_handshake_client_init_fuzzer',
    'ulpfec_generator_fuzzer',
    'ulpfec_header_reader_fuzzer',
    'ulpfec_receiver_fuzzer',
    'update_client_protocol_parser_fuzzer',
    'update_client_protocol_serializer_fuzzer',
    'url_file_parser_fuzzer',
    'url_formatter_fixer_fuzzer',
    'url_parse_proto_fuzzer',
    'url_pattern_fuzzer',
    'usb_descriptors_fuzzer',
    'usb_midi_descriptor_parser_fuzzer',
    'usb_string_read_fuzzer',
    'utf_string_conversions_fuzzer',
    'v2_handshake_fuzzer',
    'v4_get_hash_protocol_manager_fuzzer',
    'v4_store_fuzzer',
    'v8_json_parser_fuzzer',
    'v8_multi_return_fuzzer',
    'v8_regexp_builtins_fuzzer',
    'v8_regexp_parser_fuzzer',
    'v8_script_parser_fuzzer',
    'v8_wasm_async_fuzzer',
    'v8_wasm_code_fuzzer',
    'v8_wasm_compile_all_fuzzer',
    'v8_wasm_compile_fuzzer',
    'v8_wasm_compile_simd_fuzzer',
    'v8_wasm_compile_wasmgc_fuzzer',
    'v8_wasm_compile_revec_fuzzer',
    'v8_wasm_deopt_fuzzer',
    'v8_wasm_init_expr_fuzzer',
    'v8_wasm_module_fuzzer',
    'v8_wasm_streaming_fuzzer',
    'video_capture_host_mojolpm_fuzzer',
    'vp8_depacketizer_fuzzer',
    'vp9_depacketizer_fuzzer',
    'vp9_encoder_references_fuzzer',
    'vp9_qp_parser_fuzzer',
    'vp9_replay_fuzzer',
    'vr_omnibox_formatting_fuzzer',
    'wav_audio_handler_fuzzer',
    'wayland_buffer_fuzzer',
    'web_app_manifest_fuzzer',
    'web_bundle_parser_fuzzer',
    'web_icon_sizes_fuzzer',
    'webcodecs_audio_data_copy_to_fuzzer',
    'webcodecs_audio_decoder_fuzzer',
    'webcodecs_audio_encoder_fuzzer',
    'webcodecs_video_decoder_fuzzer',
    'webcodecs_video_encoder_fuzzer',
    'webcodecs_video_frame_copy_to_fuzzer',
    'webcrypto_ec_import_key_pkcs8_fuzzer',
    'webcrypto_ec_import_key_raw_fuzzer',
    'webcrypto_ec_import_key_spki_fuzzer',
    'webcrypto_rsa_import_key_pkcs8_fuzzer',
    'webcrypto_rsa_import_key_spki_fuzzer',
    'webrtc_video_perf_mojolpm_fuzzer',
    'webusb_descriptors_fuzzer',
    'xfo_fuzzer',
    'xml_parser_fuzzer',
    'xxhash_fuzzer',
    'zlib_deflate_fuzzer',
    'zlib_deflate_set_dictionary_fuzzer',
    'zlib_inflate_fuzzer',
    'zlib_inflate_with_header_fuzzer',
    'zlib_streaming_inflate_fuzzer',
    'zlib_uncompress_fuzzer',
    'zucchini_apply_fuzzer',
    'zucchini_disassembler_dex_fuzzer',
    'zucchini_disassembler_elf_fuzzer',
    'zucchini_disassembler_win32_fuzzer',
    'zucchini_imposed_ensemble_matcher_fuzzer',
    'zucchini_patch_fuzzer',
    'zucchini_raw_gen_fuzzer',
    'zucchini_ztf_gen_fuzzer',
    'zxcvbn_matching_fuzzer',
    'zxcvbn_scoring_fuzzer',
]
