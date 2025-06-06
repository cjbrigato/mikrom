// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/ffmpeg_h264_to_annex_b_bitstream_converter.h"

#include <stdint.h>

#include "base/compiler_specific.h"
#include "media/ffmpeg/ffmpeg_common.h"
#include "media/ffmpeg/scoped_av_packet.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

// Test data arrays.
static const uint8_t kHeaderDataOkWithFieldLen4[] = {
    0x01, 0x42, 0x00, 0x28, 0xFF, 0xE1, 0x00, 0x08, 0x67, 0x42, 0x00, 0x28,
    0xE9, 0x05, 0x89, 0xC8, 0x01, 0x00, 0x04, 0x68, 0xCE, 0x06, 0xF2};

// SPS and PPS have trailing null bytes.
static const uint8_t kHeaderDataTrailingNullsWithFieldLen4[] = {
    0x01, 0x42, 0x00, 0x28, 0xFF, 0xE1, 0x00, 0x09, 0x67,
    0x42, 0x00, 0x28, 0xE9, 0x05, 0x89, 0xC8, 0x00, 0x01,
    0x00, 0x05, 0x68, 0xCE, 0x06, 0xF2, 0x00};

static const uint8_t kPacketDataOkWithFieldLen4[] = {
    0x00, 0x00, 0x0B, 0xF7, 0x65, 0xB8, 0x40, 0x57, 0x0B, 0xF0, 0xDF, 0xF8,
    0x00, 0x1F, 0x78, 0x98, 0x54, 0xAC, 0xF2, 0x00, 0x04, 0x9D, 0x26, 0xE0,
    0x3B, 0x5C, 0x00, 0x0A, 0x00, 0x8F, 0x9E, 0x86, 0x63, 0x1B, 0x46, 0xE7,
    0xD6, 0x45, 0x88, 0x88, 0xEA, 0x10, 0x89, 0x79, 0x01, 0x34, 0x30, 0x01,
    0x8E, 0x7D, 0x1A, 0x39, 0x45, 0x4E, 0x69, 0x86, 0x12, 0xF2, 0xE7, 0xCF,
    0x50, 0xF8, 0x26, 0x54, 0x17, 0xBE, 0x3F, 0xC4, 0x80, 0x32, 0xD8, 0x02,
    0x32, 0xE4, 0xAE, 0xDD, 0x39, 0x11, 0x8E, 0x54, 0x42, 0xAE, 0xBD, 0x12,
    0xA4, 0xCE, 0xE2, 0x98, 0x91, 0x05, 0xC4, 0xA8, 0x20, 0xC7, 0xB3, 0xD9,
    0x47, 0x73, 0x09, 0xD5, 0xCF, 0x62, 0x57, 0x3F, 0xFF, 0xFD, 0xB9, 0x94,
    0x2B, 0x3D, 0x12, 0x1A, 0x84, 0x0B, 0x28, 0xAD, 0x5C, 0x9E, 0x5C, 0xC3,
    0xBB, 0xBD, 0x7F, 0xFE, 0x09, 0x87, 0x74, 0x39, 0x1C, 0xA5, 0x0E, 0x44,
    0xD8, 0x5D, 0x41, 0xDB, 0xAA, 0xBC, 0x05, 0x16, 0xA3, 0x98, 0xEE, 0xEE,
    0x9C, 0xA0, 0xF1, 0x23, 0x90, 0xF0, 0x5E, 0x9F, 0xF4, 0xFA, 0x7F, 0x4B,
    0x69, 0x66, 0x49, 0x52, 0xDD, 0xD6, 0xC0, 0x0F, 0x8C, 0x6E, 0x80, 0xDD,
    0x7A, 0xDF, 0x10, 0xCD, 0x4B, 0x54, 0x6F, 0xFC, 0x7D, 0x34, 0xBA, 0x8B,
    0xD4, 0xD9, 0x30, 0x18, 0x9F, 0x39, 0x04, 0x9F, 0xCB, 0xDB, 0x1B, 0xA7,
    0x70, 0x96, 0xAF, 0xFF, 0x6F, 0xB5, 0xBF, 0x58, 0x01, 0x98, 0xCD, 0xF2,
    0x66, 0x28, 0x1A, 0xC4, 0x9E, 0x58, 0x40, 0x39, 0xAE, 0x07, 0x11, 0x3F,
    0xF2, 0x9B, 0x06, 0x9C, 0xB8, 0xC9, 0x16, 0x12, 0x09, 0x8E, 0xD2, 0xD4,
    0xF5, 0xC6, 0x77, 0x40, 0x0F, 0xFD, 0x12, 0x19, 0x55, 0x1A, 0x8E, 0x9C,
    0x18, 0x8B, 0x0D, 0x18, 0xFA, 0xBA, 0x7F, 0xBB, 0x83, 0xBB, 0x85, 0xA0,
    0xCC, 0xAF, 0xF6, 0xEA, 0x81, 0x10, 0x18, 0x8E, 0x10, 0x00, 0xCB, 0x7F,
    0x27, 0x08, 0x06, 0xDE, 0x3C, 0x20, 0xE5, 0xFE, 0xCC, 0x4F, 0xB3, 0x41,
    0xE0, 0xCC, 0x4C, 0x26, 0xC1, 0xC0, 0x2C, 0x16, 0x12, 0xAA, 0x04, 0x83,
    0x51, 0x4E, 0xCA, 0x00, 0xCF, 0x42, 0x9C, 0x06, 0x2D, 0x06, 0xDD, 0x1D,
    0x08, 0x75, 0xE0, 0x89, 0xC7, 0x62, 0x68, 0x2E, 0xBF, 0x4D, 0x2D, 0x0A,
    0xC4, 0x86, 0xF6, 0x2F, 0xA1, 0x49, 0xA7, 0x0F, 0xDB, 0x1F, 0x82, 0xEC,
    0xC1, 0x62, 0xFB, 0x7F, 0xF1, 0xAE, 0xA6, 0x1A, 0xD5, 0x6B, 0x06, 0x5E,
    0xB6, 0x02, 0x50, 0xAE, 0x2D, 0xF9, 0xD9, 0x95, 0xAD, 0x01, 0x8C, 0x53,
    0x01, 0xAF, 0xCE, 0xE5, 0xA5, 0xBB, 0x95, 0x8A, 0x85, 0x70, 0x77, 0xE3,
    0x9A, 0x68, 0x1B, 0xDF, 0x47, 0xF9, 0xF4, 0xBD, 0x80, 0x7D, 0x76, 0x9A,
    0x69, 0xFC, 0xBE, 0x14, 0x0D, 0x87, 0x09, 0x12, 0x98, 0x20, 0x05, 0x46,
    0xB7, 0xAE, 0x10, 0xB7, 0x01, 0xB7, 0xDE, 0x3B, 0xDD, 0x7A, 0x8A, 0x55,
    0x73, 0xAD, 0xDF, 0x69, 0xDE, 0xD0, 0x51, 0x97, 0xA0, 0xE6, 0x5E, 0xBA,
    0xBA, 0x80, 0x0F, 0x4E, 0x9A, 0x68, 0x36, 0xE6, 0x9F, 0x5B, 0x39, 0xC0,
    0x90, 0xA1, 0xC0, 0xC3, 0x82, 0xE4, 0x50, 0xEA, 0x60, 0x7A, 0xDD, 0x5F,
    0x8B, 0x5F, 0xAF, 0xFC, 0x74, 0xAF, 0xDC, 0x56, 0xF7, 0x2E, 0x3E, 0x97,
    0x6E, 0x2B, 0xF3, 0xAF, 0xFE, 0x7D, 0x32, 0xDC, 0x56, 0xF8, 0xAF, 0xB5,
    0xA3, 0xBB, 0x00, 0x5B, 0x84, 0x3D, 0x9F, 0x0B, 0x40, 0x88, 0x61, 0x5F,
    0x4F, 0x4F, 0xB0, 0xB3, 0x07, 0x81, 0x3E, 0xF2, 0xFB, 0x50, 0xCA, 0x77,
    0x40, 0x12, 0xA8, 0xE6, 0x11, 0x8E, 0xD6, 0x8A, 0xC6, 0xD6, 0x8C, 0x1D,
    0x63, 0x55, 0x3D, 0x34, 0xEA, 0xC3, 0xC6, 0x6A, 0xD2, 0x8C, 0xB0, 0x1D,
    0x5E, 0x4A, 0x7A, 0x8B, 0xD5, 0x99, 0x80, 0x84, 0x32, 0xFB, 0xB7, 0x02,
    0x6E, 0x61, 0xFE, 0xAC, 0x1B, 0x5D, 0x10, 0x23, 0x24, 0xC3, 0x8C, 0x7B,
    0x58, 0x2C, 0x4D, 0x04, 0x74, 0x84, 0x25, 0x10, 0x4E, 0x94, 0x29, 0x4D,
    0x88, 0xAE, 0x65, 0x53, 0xB9, 0x95, 0x4E, 0xE7, 0xDD, 0xEE, 0xF2, 0x70,
    0x1F, 0x26, 0x4F, 0xA8, 0xBC, 0x3D, 0x35, 0x02, 0x3B, 0xC0, 0x98, 0x70,
    0x38, 0x18, 0xE5, 0x1E, 0x05, 0xAC, 0x28, 0xAA, 0x46, 0x1A, 0xB0, 0x19,
    0x99, 0x18, 0x35, 0x78, 0x1E, 0x41, 0x60, 0x0D, 0x4F, 0x7E, 0xEC, 0x37,
    0xC3, 0x30, 0x73, 0x2A, 0x69, 0xFE, 0xEF, 0x27, 0xEE, 0x13, 0xCC, 0xD0,
    0xDB, 0xE6, 0x45, 0xEC, 0x5C, 0xB5, 0x71, 0x54, 0x2E, 0xB1, 0xE9, 0x88,
    0xB4, 0x3F, 0x6F, 0xFD, 0xF7, 0xFF, 0x9D, 0x2D, 0x52, 0x2E, 0xAE, 0xC9,
    0x95, 0xDE, 0xBF, 0xDF, 0xFF, 0xBF, 0x21, 0xB3, 0x2B, 0xF5, 0xF7, 0xF7,
    0xD1, 0xA0, 0xF0, 0x76, 0x68, 0x37, 0xDB, 0x8F, 0x85, 0x4D, 0xA8, 0x1A,
    0xF9, 0x7F, 0x75, 0xA7, 0x93, 0xF5, 0x03, 0xC1, 0xF2, 0x60, 0x8A, 0x92,
    0x53, 0xF5, 0xD1, 0xC1, 0x56, 0x4B, 0x68, 0x05, 0x16, 0x88, 0x61, 0xE7,
    0x14, 0xC8, 0x0D, 0xF0, 0xDF, 0xEF, 0x46, 0x4A, 0xED, 0x0B, 0xD1, 0xD1,
    0xD1, 0xA4, 0x85, 0xA3, 0x2C, 0x1D, 0xDE, 0x45, 0x14, 0xA1, 0x8E, 0xA8,
    0xD9, 0x8C, 0xAB, 0x47, 0x31, 0xF1, 0x00, 0x15, 0xAD, 0x80, 0x20, 0xAA,
    0xE4, 0x57, 0xF8, 0x05, 0x14, 0x58, 0x0B, 0xD3, 0x63, 0x00, 0x8F, 0x44,
    0x15, 0x7F, 0x19, 0xC7, 0x0A, 0xE0, 0x49, 0x32, 0xFE, 0x36, 0x0E, 0xF3,
    0x66, 0x10, 0x2B, 0x11, 0x73, 0x3D, 0x19, 0x92, 0x22, 0x20, 0x75, 0x1F,
    0xF1, 0xDB, 0x96, 0x73, 0xCF, 0x1B, 0x53, 0xFF, 0xD2, 0x23, 0xF2, 0xB6,
    0xAA, 0xB6, 0x44, 0xA3, 0x73, 0x7E, 0x00, 0x2D, 0x4D, 0x4D, 0x87, 0xE0,
    0x84, 0x55, 0xD6, 0x03, 0xB8, 0xD8, 0x90, 0xEF, 0xC0, 0x76, 0x5D, 0x69,
    0x02, 0x00, 0x0E, 0x17, 0xD0, 0x02, 0x96, 0x50, 0xEA, 0xAB, 0xBF, 0x0D,
    0xAF, 0xCB, 0xD3, 0xFF, 0xAA, 0x9D, 0x7F, 0xD6, 0xBD, 0x2C, 0x14, 0xB4,
    0xCD, 0x20, 0x73, 0xB4, 0xF4, 0x38, 0x96, 0xDE, 0xB0, 0x6B, 0xE5, 0x1B,
    0xFD, 0x0E, 0x0B, 0xA4, 0x81, 0xBF, 0xC8, 0xA0, 0x21, 0x76, 0x7B, 0x25,
    0x3F, 0xE6, 0x84, 0x40, 0x1A, 0xDA, 0x25, 0x5A, 0xFF, 0x73, 0x6B, 0x14,
    0x1B, 0xF7, 0x08, 0xFA, 0x26, 0x73, 0x7A, 0x58, 0x02, 0x1A, 0xE6, 0x63,
    0xB6, 0x45, 0x7B, 0xE3, 0xE0, 0x80, 0x14, 0x42, 0xA8, 0x7D, 0xF3, 0x80,
    0x9B, 0x01, 0x43, 0x82, 0x82, 0x8C, 0xBE, 0x0D, 0xFD, 0xAE, 0x88, 0xA8,
    0xB9, 0xC3, 0xEE, 0xFF, 0x46, 0x00, 0x84, 0xE6, 0xB4, 0x0C, 0xA9, 0x66,
    0xC6, 0x74, 0x72, 0xAA, 0xA4, 0x3A, 0xB0, 0x1B, 0x06, 0xB4, 0xDB, 0xE8,
    0xC2, 0x17, 0xA2, 0xBC, 0xBE, 0x5C, 0x0F, 0x2A, 0x76, 0xD5, 0xEE, 0x39,
    0x36, 0x7C, 0x25, 0x94, 0x15, 0x3C, 0xC9, 0xB9, 0x93, 0x07, 0x19, 0xAF,
    0xE6, 0x70, 0xC3, 0xF5, 0xD4, 0x17, 0x87, 0x57, 0x77, 0x7D, 0xCF, 0x0D,
    0xDD, 0xDE, 0xB7, 0xFF, 0xB4, 0xDA, 0x20, 0x45, 0x1A, 0x45, 0xF4, 0x58,
    0x01, 0xBC, 0xEB, 0x3F, 0x16, 0x7F, 0x4C, 0x15, 0x84, 0x8C, 0xE5, 0xF6,
    0x96, 0xA6, 0xA1, 0xB9, 0xB2, 0x7F, 0x6B, 0xFF, 0x31, 0xF2, 0xF5, 0xC9,
    0xFF, 0x61, 0xEE, 0xB5, 0x84, 0xAE, 0x68, 0x41, 0xEA, 0xD0, 0xF0, 0xA5,
    0xCE, 0x0C, 0xE6, 0x4C, 0x6D, 0x6D, 0x94, 0x08, 0xC9, 0xA9, 0x4A, 0x60,
    0x6D, 0x01, 0x3B, 0xEF, 0x4D, 0x99, 0x8D, 0x42, 0x2A, 0x6B, 0x8A, 0xC7,
    0xFA, 0xA9, 0x90, 0x40, 0x00, 0x90, 0xF3, 0xA0, 0x75, 0x8E, 0xD5, 0xFE,
    0xE7, 0xBD, 0x02, 0x87, 0x0C, 0x7D, 0xF0, 0xAF, 0x1E, 0x5F, 0x8D, 0xC8,
    0xE1, 0xD4, 0x56, 0x08, 0xBF, 0x76, 0x80, 0xD4, 0x18, 0x89, 0x2D, 0x57,
    0xDF, 0x66, 0xD0, 0x46, 0x68, 0x77, 0x55, 0x47, 0xF5, 0x7C, 0xF7, 0xA6,
    0x66, 0xD6, 0x5A, 0x64, 0x55, 0xD4, 0x80, 0xC4, 0x55, 0xE9, 0x36, 0x3F,
    0x5E, 0xE2, 0x5C, 0x7F, 0x5F, 0xCE, 0x7F, 0xE1, 0x0C, 0x82, 0x3D, 0x6B,
    0x6E, 0xA2, 0xEA, 0x3B, 0x1F, 0xE8, 0x9E, 0xC7, 0x4E, 0x24, 0x3D, 0xDD,
    0xFA, 0xEB, 0x71, 0xDF, 0xFE, 0x15, 0xFE, 0x41, 0x9B, 0xB4, 0x4E, 0xAB,
    0x51, 0xE5, 0x1F, 0x7D, 0x2D, 0xAC, 0xD0, 0x66, 0xD9, 0xA1, 0x59, 0x78,
    0xC6, 0xEF, 0xC4, 0x43, 0x08, 0x65, 0x18, 0x73, 0xDE, 0x2A, 0xAD, 0x72,
    0xE7, 0x5A, 0x7E, 0x33, 0x04, 0x72, 0x38, 0x57, 0x47, 0x73, 0x10, 0x1D,
    0x88, 0x57, 0x4C, 0xDF, 0xA7, 0x78, 0x16, 0xFB, 0x01, 0x21, 0x28, 0x2D,
    0xB6, 0x7E, 0x05, 0x18, 0x32, 0x52, 0xC3, 0x49, 0x0B, 0x32, 0x18, 0x12,
    0x93, 0x54, 0x15, 0x3B, 0xC8, 0x6D, 0x4A, 0x77, 0xEF, 0x0A, 0x46, 0x83,
    0x89, 0x5C, 0x8B, 0xCB, 0x18, 0xA6, 0xDC, 0x97, 0x6F, 0xEE, 0xEE, 0x00,
    0x6A, 0xF1, 0x10, 0xFE, 0x07, 0x0C, 0xE0, 0x53, 0xD2, 0xB8, 0x45, 0xF4,
    0x6E, 0x16, 0x4B, 0xC9, 0x9C, 0xC7, 0x93, 0x83, 0x23, 0x1D, 0x4D, 0x00,
    0xB9, 0x4F, 0x86, 0x51, 0xF0, 0x29, 0x69, 0x41, 0x21, 0xC5, 0x4A, 0xC6,
    0x6D, 0xD1, 0x81, 0x38, 0xDB, 0x7C, 0x06, 0xA8, 0x26, 0x8E, 0x71, 0x00,
    0x4C, 0x44, 0x14, 0x05, 0xF2, 0x1C, 0x00, 0x49, 0xFC, 0x29, 0x6A, 0xF9,
    0x9E, 0xD1, 0x35, 0x4B, 0xB7, 0xE5, 0xDB, 0xFC, 0x01, 0x04, 0x3F, 0x70,
    0x33, 0x56, 0x87, 0x69, 0x01, 0xB4, 0xCE, 0x1C, 0x4D, 0x2E, 0x83, 0x51,
    0x51, 0xD0, 0x37, 0x3B, 0xB4, 0xBA, 0x47, 0xF5, 0xFF, 0xBF, 0xFA, 0xD5,
    0x03, 0x65, 0xD3, 0x28, 0x9F, 0x38, 0x57, 0xFE, 0x71, 0xD8, 0x9C, 0x16,
    0xEE, 0x72, 0x19, 0x03, 0x17, 0x6E, 0xC0, 0xEC, 0x49, 0x3D, 0x96, 0xE2,
    0x30, 0x97, 0x97, 0x84, 0x38, 0x6B, 0xE8, 0x2E, 0xAB, 0x0E, 0x2E, 0x03,
    0x52, 0xBA, 0x68, 0x55, 0xBA, 0x1D, 0x2C, 0x47, 0xAA, 0x72, 0xAE, 0x02,
    0x31, 0x6E, 0xA1, 0xDC, 0xAD, 0x0F, 0x4A, 0x46, 0xC9, 0xF2, 0xA9, 0xAB,
    0xFD, 0x87, 0x89, 0x5C, 0xB3, 0x75, 0x7E, 0xE3, 0xDE, 0x9F, 0xC4, 0x02,
    0x1E, 0xA2, 0xF8, 0x8B, 0xD3, 0x00, 0x83, 0x96, 0xC4, 0xD0, 0xB9, 0x62,
    0xB9, 0x69, 0xEC, 0x56, 0xDF, 0x7D, 0x91, 0x4B, 0x68, 0x27, 0xA8, 0x61,
    0x78, 0xA7, 0x95, 0x66, 0x51, 0x41, 0xF6, 0xCE, 0x78, 0xD3, 0x9A, 0x91,
    0xA0, 0x31, 0x09, 0x47, 0xB8, 0x47, 0xB8, 0x44, 0xE1, 0x13, 0x86, 0x7E,
    0x92, 0x80, 0xC6, 0x1A, 0xF7, 0x79, 0x7E, 0xF1, 0x5D, 0x9F, 0x17, 0x2D,
    0x80, 0x00, 0x79, 0x34, 0x7D, 0xE3, 0xAD, 0x60, 0x00, 0x20, 0x07, 0x80,
    0x00, 0x40, 0x01, 0xF8, 0xA1, 0x86, 0xB1, 0xEE, 0x21, 0x63, 0x85, 0x60,
    0x51, 0x84, 0x90, 0x7E, 0x92, 0x09, 0x39, 0x1C, 0x16, 0x87, 0x5C, 0xA6,
    0x09, 0x90, 0x06, 0x34, 0x6E, 0xB8, 0x8D, 0x5D, 0xAC, 0x77, 0x97, 0xB5,
    0x4D, 0x30, 0xFD, 0x39, 0xD0, 0x50, 0x00, 0xC9, 0x98, 0x04, 0x86, 0x00,
    0x0D, 0xD8, 0x3E, 0x34, 0xC2, 0xA6, 0x25, 0xF8, 0x20, 0xCC, 0x6D, 0x9E,
    0x63, 0x05, 0x30, 0xC4, 0xC6, 0xCC, 0x54, 0x31, 0x9F, 0x3C, 0xF5, 0x86,
    0xB9, 0x08, 0x18, 0xC3, 0x1E, 0xB9, 0xA0, 0x0C, 0x45, 0x2C, 0x54, 0x32,
    0x8B, 0x85, 0x86, 0x59, 0xC3, 0xB3, 0x50, 0x5A, 0xFE, 0xBA, 0xF7, 0x4D,
    0xC9, 0x9C, 0x9E, 0x01, 0xDF, 0xD7, 0x6E, 0xB5, 0x15, 0x53, 0x08, 0x57,
    0xA4, 0x71, 0x36, 0x80, 0x46, 0x05, 0x21, 0x48, 0x7B, 0x91, 0xC8, 0xAA,
    0xFF, 0x07, 0x9F, 0x78, 0x68, 0xCF, 0x3C, 0xEF, 0xFF, 0xBC, 0xB6, 0xA2,
    0x36, 0xB7, 0x9F, 0x54, 0xF6, 0x6F, 0x5D, 0xDD, 0x75, 0xD4, 0x3C, 0x75,
    0xE8, 0xCF, 0x15, 0x02, 0x5B, 0x94, 0xC3, 0xA2, 0x41, 0x63, 0xA1, 0x14,
    0xF6, 0xC0, 0x57, 0x15, 0x9F, 0x0C, 0x3F, 0x80, 0xF2, 0x98, 0xEE, 0x41,
    0x85, 0xEE, 0xBC, 0xAA, 0xE9, 0x59, 0xAA, 0xA0, 0x92, 0xCA, 0x00, 0xF3,
    0x50, 0xCC, 0xFF, 0xAD, 0x97, 0x69, 0xA7, 0xF2, 0x0B, 0x8F, 0xD7, 0xD7,
    0x82, 0x3A, 0xBB, 0x98, 0x1D, 0xCB, 0x89, 0x0B, 0x9B, 0x05, 0xF7, 0xD0,
    0x1A, 0x60, 0xF3, 0x29, 0x16, 0x12, 0xF8, 0xF4, 0xF1, 0x4A, 0x05, 0x9B,
    0x57, 0x12, 0x7E, 0x3A, 0x4A, 0x8D, 0xA6, 0xDF, 0xB6, 0xDD, 0xDF, 0xC3,
    0xF0, 0xD2, 0xD4, 0xD7, 0x41, 0xA6, 0x00, 0x76, 0x8C, 0x75, 0x08, 0xF0,
    0x19, 0xD8, 0x14, 0x63, 0x55, 0x52, 0x18, 0x30, 0x98, 0xD0, 0x3F, 0x65,
    0x52, 0xB3, 0x88, 0x6D, 0x17, 0x39, 0x93, 0xCA, 0x3B, 0xB4, 0x1D, 0x8D,
    0xDF, 0xDF, 0xAD, 0x72, 0xDA, 0x74, 0xAF, 0xBD, 0x31, 0xF9, 0x12, 0x61,
    0x45, 0x29, 0x4C, 0x2B, 0x61, 0xA1, 0x12, 0x90, 0x53, 0xE7, 0x5A, 0x9D,
    0x44, 0xC8, 0x3A, 0x83, 0xDC, 0x34, 0x4C, 0x07, 0xAF, 0xDB, 0x90, 0xCD,
    0x03, 0xA4, 0x64, 0x78, 0xBD, 0x55, 0xB2, 0x56, 0x59, 0x32, 0xAB, 0x13,
    0x2C, 0xC9, 0x77, 0xF8, 0x3B, 0xDF, 0xFF, 0xAC, 0x07, 0xB9, 0x08, 0x7B,
    0xE9, 0x82, 0xB9, 0x59, 0xC7, 0xFF, 0x86, 0x2C, 0x12, 0x7C, 0xC6, 0x65,
    0x3C, 0x71, 0xB8, 0x98, 0x9F, 0xA2, 0x45, 0x03, 0xA5, 0xD9, 0xC3, 0xCF,
    0xFA, 0xEB, 0x89, 0xAD, 0x03, 0xEE, 0xDD, 0x76, 0xD3, 0x4F, 0x10, 0x6F,
    0xF0, 0xC1, 0x60, 0x0C, 0x00, 0xD4, 0x76, 0x12, 0x0A, 0x8D, 0xDC, 0xFD,
    0x5E, 0x0B, 0x26, 0x2F, 0x01, 0x1D, 0xB9, 0xE7, 0x73, 0xD4, 0xF2, 0xCB,
    0xD8, 0x78, 0x21, 0x52, 0x4B, 0x83, 0x3C, 0x44, 0x72, 0x0E, 0xB1, 0x4E,
    0x37, 0xBC, 0xC7, 0x50, 0xFA, 0x07, 0x80, 0x71, 0x10, 0x0B, 0x24, 0xD1,
    0x7E, 0xDA, 0x7F, 0xA7, 0x2F, 0x40, 0xAA, 0xD3, 0xF5, 0x44, 0x10, 0x56,
    0x4E, 0x3B, 0xF1, 0x6E, 0x9A, 0xA0, 0xEA, 0x85, 0x66, 0x16, 0xFB, 0x5C,
    0x0B, 0x2B, 0x74, 0x18, 0xAF, 0x3D, 0x04, 0x3E, 0xCE, 0x88, 0x9B, 0x3E,
    0xF4, 0xB9, 0x00, 0x60, 0x0E, 0xE1, 0xE2, 0xCB, 0x12, 0xB9, 0x6D, 0x5A,
    0xC7, 0x55, 0x1D, 0xB9, 0x79, 0xAC, 0x43, 0x43, 0xE6, 0x3B, 0xDD, 0x7E,
    0x9F, 0x78, 0xD3, 0xEA, 0xA3, 0x11, 0xFF, 0xDB, 0xBB, 0xB8, 0x97, 0x37,
    0x15, 0xDB, 0xF1, 0x97, 0x96, 0xC7, 0xFC, 0xE5, 0xBF, 0xF2, 0x86, 0xC0,
    0xFA, 0x9B, 0x4C, 0x00, 0x04, 0x03, 0xA5, 0xB6, 0xB7, 0x9C, 0xD9, 0xAB,
    0x09, 0x77, 0x51, 0x18, 0x3B, 0xAD, 0x61, 0x6C, 0xFC, 0x51, 0x96, 0xFB,
    0x19, 0xC8, 0x52, 0x35, 0x07, 0x00, 0x63, 0x87, 0x14, 0x04, 0xFA, 0x7A,
    0x48, 0x3E, 0x00, 0x47, 0x29, 0x07, 0x74, 0x97, 0x74, 0x84, 0xEB, 0xB2,
    0x16, 0xB2, 0x31, 0x81, 0xCE, 0x2A, 0x31, 0xA7, 0xB1, 0xEB, 0x83, 0x34,
    0x7A, 0x73, 0xD7, 0x2F, 0xFF, 0xBC, 0xFF, 0xE5, 0xAA, 0xF2, 0xB5, 0x6E,
    0x9E, 0xA5, 0x70, 0x8A, 0x8C, 0xDF, 0x6A, 0x06, 0x16, 0xC1, 0xAB, 0x59,
    0x70, 0xD9, 0x3D, 0x47, 0x7C, 0xDD, 0xEF, 0xDF, 0x2F, 0xFF, 0x42, 0x6B,
    0xBA, 0x4B, 0xBF, 0xF8, 0x7F, 0xF2, 0x03, 0x0D, 0x79, 0xBC, 0x03, 0x76,
    0x64, 0x1C, 0x0D, 0x7B, 0xD7, 0xBD, 0xB0, 0x6C, 0xD8, 0x61, 0x17, 0x17,
    0x8C, 0xED, 0x4E, 0x20, 0xEB, 0x55, 0x33, 0x39, 0xE9, 0x7E, 0xBE, 0x8E,
    0x05, 0x4B, 0x78, 0x96, 0x85, 0xCC, 0x68, 0xC9, 0x78, 0xAF, 0xAE, 0x44,
    0x36, 0x61, 0xD3, 0x53, 0xEB, 0xB3, 0x3E, 0x4F, 0x97, 0xE2, 0x8D, 0xAE,
    0x90, 0xED, 0xB5, 0x4F, 0x8E, 0xE4, 0x7A, 0x44, 0xCF, 0x9D, 0xC5, 0x77,
    0x4D, 0xAB, 0x4F, 0xE5, 0xC5, 0x73, 0xA0, 0xC8, 0xA5, 0xBB, 0x4B, 0x7D,
    0xD5, 0xFB, 0xFB, 0xFF, 0x61, 0xFD, 0xAA, 0x1A, 0x62, 0x7E, 0x3C, 0x66,
    0x34, 0x15, 0x64, 0x25, 0xEC, 0x7C, 0x9D, 0x6A, 0x64, 0x4D, 0x80, 0xD5,
    0x4F, 0xFE, 0x8E, 0xEE, 0x18, 0x53, 0xC1, 0x09, 0x51, 0xF7, 0xC0, 0xA6,
    0xB2, 0x9B, 0x19, 0x2B, 0x14, 0x66, 0x66, 0x4B, 0x39, 0x62, 0x1D, 0x84,
    0xB9, 0x02, 0x84, 0xAC, 0xC1, 0xDA, 0x6C, 0x80, 0xCD, 0x40, 0x20, 0x20,
    0x19, 0x51, 0xDC, 0x2B, 0x7D, 0x5D, 0x7F, 0xE3, 0x86, 0x8E, 0xC3, 0x35,
    0xFE, 0x5C, 0xF6, 0x1C, 0xFF, 0x05, 0x9E, 0xB5, 0xB6, 0xBB, 0xBE, 0xF7,
    0x2F, 0xB7, 0xE1, 0xF5, 0x33, 0x86, 0xA0, 0x47, 0xDE, 0xF7, 0xE9, 0x3B,
    0xBE, 0x7E, 0x9B, 0x17, 0xFC, 0xFD, 0x2E, 0x40, 0x86, 0x41, 0x75, 0xF1,
    0xB2, 0x18, 0xA9, 0xDE, 0x2D, 0xD6, 0x04, 0x20, 0xA4, 0xBA, 0x81, 0xBC,
    0x1D, 0x5A, 0xD6, 0xF7, 0xF6, 0xB8, 0x42, 0xF7, 0xF5, 0x3D, 0x97, 0xAC,
    0xCD, 0x6F, 0xAD, 0xDB, 0x4F, 0x5A, 0x2E, 0x64, 0xB9, 0x5D, 0xDD, 0x8B,
    0x4A, 0x35, 0x44, 0xFE, 0x3D, 0xC6, 0x77, 0x7A, 0xBF, 0xDA, 0xAC, 0x9E,
    0xB0, 0xA2, 0xB9, 0x6C, 0xAF, 0x02, 0xDD, 0xF2, 0x71, 0x2B, 0xEF, 0xD3,
    0x51, 0x0E, 0x07, 0x11, 0xBD, 0xED, 0x39, 0x7F, 0xD9, 0xB8, 0xBD, 0xEE,
    0x35, 0xE9, 0x5C, 0x67, 0x42, 0xDA, 0x05, 0x6E, 0x39, 0xCE, 0x55, 0xFB,
    0x26, 0xB7, 0x90, 0x4B, 0xDA, 0x91, 0x48, 0xFD, 0xDE, 0xB2, 0xEC, 0x88,
    0x9A, 0x46, 0x1A, 0x4C, 0xD4, 0x05, 0x12, 0x85, 0x57, 0x37, 0x22, 0xD3,
    0x0E, 0x4F, 0x79, 0xE3, 0x81, 0xA9, 0x2B, 0x5F, 0xD7, 0x6D, 0xBD, 0x21,
    0x98, 0x6F, 0x7D, 0xF5, 0x32, 0x7A, 0x6E, 0xF8, 0x20, 0x01, 0x50, 0x90,
    0x7A, 0x88, 0x3E, 0x0D, 0x57, 0xB1, 0x58, 0x65, 0xE6, 0x82, 0xCE, 0x08,
    0x69, 0x8B, 0x87, 0x62, 0x36, 0xB1, 0x7B, 0xDE, 0x74, 0xBD, 0xFE, 0x10,
    0xBE, 0x26, 0xAB, 0x7E, 0xB7, 0x8D, 0xF7, 0x83, 0x2E, 0x0F, 0xAF, 0x7E,
    0xBC, 0x17, 0x31, 0xFF, 0xB0, 0x4F, 0x7F, 0x4B, 0x13, 0x83, 0xDF, 0xEE,
    0x23, 0xD3, 0xE7, 0xC8, 0xAF, 0x75, 0xAB, 0xEA, 0xBD, 0x7D, 0xD2, 0x9D,
    0xE9, 0xC1, 0x18, 0x8B, 0x7C, 0x9F, 0x51, 0xDC, 0x37, 0xA3, 0xDB, 0xFC,
    0xD4, 0x6A, 0x91, 0x44, 0x7F, 0x72, 0xC5, 0xD9, 0xC8, 0x37, 0x38, 0x63,
    0x0D, 0x59, 0x8B, 0x7F, 0x7D, 0x96, 0xC1, 0x5F, 0x4C, 0x7C, 0x88, 0xCB,
    0x65, 0x07, 0x2B, 0x0E, 0x1D, 0x24, 0xAA, 0x20, 0x2E, 0x6C, 0x33, 0xAB,
    0xEF, 0x23, 0xE5, 0xE3, 0x6C, 0xA3, 0xA5, 0x2D, 0x01, 0xDF, 0x26, 0x92,
    0x52, 0xF5, 0xE6, 0x3E, 0xE3, 0xDD, 0xC6, 0xED, 0x42, 0x0F, 0x71, 0x7B,
    0xD1, 0xF4, 0x06, 0xF6, 0x82, 0xD5, 0x13, 0xB3, 0x60, 0x31, 0x09, 0x89,
    0x63, 0x15, 0xD2, 0xCB, 0xAA, 0x77, 0xFD, 0xF4, 0xEB, 0xF4, 0xED, 0x2E,
    0xE2, 0xBA, 0x27, 0x2E, 0x74, 0xD2, 0x91, 0x7F, 0x0F, 0xDE, 0x25, 0xFE,
    0x78, 0x20, 0x05, 0x0A, 0x6A, 0xFE, 0x89, 0x14, 0x23, 0xF3, 0xF5, 0x3A,
    0x1E, 0xF3, 0x22, 0xCE, 0x12, 0x82, 0x24, 0x11, 0x05, 0x04, 0x71, 0x99,
    0xE5, 0xF0, 0xA6, 0xDB, 0x7B, 0xF5, 0x8F, 0xF9, 0x3C, 0x02, 0x0C, 0x46,
    0xFD, 0xB6, 0xEA, 0x06, 0x11, 0xF4, 0x1E, 0x7A, 0x20, 0x6A, 0x54, 0xBB,
    0x4A, 0x60, 0xB0, 0x30, 0x28, 0x9A, 0xF3, 0x3B, 0xE9, 0xBD, 0xD6, 0x04,
    0xCA, 0x3A, 0x33, 0x37, 0x5F, 0xB7, 0xAD, 0xE7, 0x9C, 0xE2, 0x95, 0x21,
    0xF7, 0xB5, 0xC4, 0xF0, 0xD1, 0x51, 0x09, 0x44, 0x3F, 0x07, 0xFC, 0x5F,
    0x37, 0xFD, 0x7D, 0x7E, 0xD5, 0xF7, 0xEB, 0x69, 0xB9, 0x54, 0x98, 0x5A,
    0x2A, 0x56, 0xE3, 0xC0, 0x21, 0x57, 0xD1, 0xEB, 0x75, 0x15, 0xED, 0xAC,
    0xAF, 0x5D, 0xFF, 0xC2, 0xFE, 0x4E, 0xFB, 0xBA, 0x13, 0xB8, 0x87, 0xFA,
    0x4E, 0x5E, 0x5C, 0x24, 0x15, 0x5B, 0x2B, 0x2C, 0x32, 0x68, 0x1F, 0x30,
    0x5F, 0xC1, 0xF7, 0xE7, 0xE1, 0x9C, 0x00, 0xC1, 0x9C, 0xB1, 0xAB, 0xFA,
    0xFF, 0xC1, 0x1E, 0x72, 0xA1, 0x46, 0x9E, 0x2E, 0xCD, 0x76, 0x96, 0x4F,
    0x14, 0xDC, 0x68, 0xC1, 0x10, 0x9F, 0xDF, 0xEB, 0x5A, 0xBA, 0x8D, 0x91,
    0x4E, 0x76, 0xE9, 0x3A, 0x43, 0x2D, 0x88, 0xD2, 0x81, 0x0C, 0xEC, 0x6F,
    0xB7, 0xA4, 0x8B, 0x97, 0x4F, 0xC4, 0x1E, 0xF3, 0x0F, 0xF5, 0x66, 0x66,
    0xBF, 0x6C, 0x3F, 0xFB, 0x6E, 0x2B, 0x48, 0x6C, 0x7B, 0xD1, 0x2E, 0x64,
    0xD1, 0x0B, 0x6E, 0x5B, 0x05, 0x16, 0xDD, 0xCB, 0x1B, 0xDE, 0xA2, 0xB9,
    0xA8, 0x94, 0xD6, 0x5A, 0x5B, 0xE2, 0xC9, 0xBC, 0xD5, 0xAB, 0x64, 0x5B,
    0x0F, 0x9A, 0xFD, 0xC7, 0x2E, 0xB7, 0xEF, 0xAE, 0xE9, 0x1F, 0x32, 0xD2,
    0xCA, 0xA0, 0x37, 0x63, 0x86, 0x72, 0x41, 0x07, 0xBC, 0xAB, 0x6F, 0xFF,
    0xB7, 0x16, 0xAA, 0xA9, 0x58, 0x9E, 0x43, 0x9C, 0x22, 0x8D, 0x48, 0xCE,
    0xE5, 0xEF, 0xE0, 0x7D, 0x47, 0x87, 0x5A, 0xA8, 0x5B, 0x06, 0xA9, 0x47,
    0xF0, 0x26, 0xB4, 0x99, 0xD8, 0xA3, 0x64, 0xED, 0x73, 0xB3, 0x96, 0xB4,
    0x21, 0x19, 0xA5, 0xC1, 0xDC, 0x88, 0x2E, 0xEE, 0xF2, 0x77, 0x91, 0xEC,
    0xFB, 0xD5, 0xF9, 0xF8, 0x90, 0x47, 0xAD, 0xF5, 0xEB, 0x96, 0x6D, 0xF1,
    0x1C, 0xE0, 0xDC, 0x74, 0x1C, 0xE6, 0x2E, 0xE1, 0x76, 0x9D, 0xEE, 0xF4,
    0xEF, 0xA5, 0x31, 0x03, 0x87, 0x0E, 0x2C, 0x84, 0xA5, 0xF1, 0x22, 0xBE,
    0x48, 0xA9, 0xCD, 0x09, 0x07, 0xC1, 0xF0, 0xD4, 0xE7, 0x03, 0x82, 0x39,
    0xE2, 0xA0, 0x0B, 0xDE, 0xAC, 0x37, 0xAC, 0x62, 0x97, 0x8E, 0x79, 0xCE,
    0x52, 0x24, 0x78, 0xF9, 0x17, 0xD2, 0xF1, 0x5D, 0x2D, 0xA1, 0xDF, 0x12,
    0x2C, 0x83, 0xE5, 0x1A, 0x28, 0x9A, 0x2D, 0xED, 0x8A, 0xBF, 0xFC, 0x41,
    0xC3, 0xEB, 0x0E, 0x91, 0xDB, 0xF2, 0xA1, 0xC8, 0xA8, 0x01, 0x8B, 0xF2,
    0xF3, 0x59, 0xB7, 0xA7, 0x6F, 0x80, 0xFF, 0x0B, 0x46, 0xE1, 0x63, 0xA7,
    0x5F, 0x6B, 0xBE, 0x33, 0x71, 0xBE, 0x3A, 0xAF, 0xA9, 0x53, 0x5D, 0x3B,
    0xB2, 0xF6, 0xEB, 0x42, 0x1C, 0x3E, 0x3F, 0x1D, 0x6A, 0x34, 0xAE, 0xB1,
    0x05, 0xA1, 0x32, 0x6C, 0xB5, 0xE4, 0xD3, 0xBB, 0xE8, 0x10, 0x14, 0x9E,
    0x68, 0x6A, 0x24, 0x51, 0xA5, 0x66, 0x64, 0xCC, 0xC4, 0x2D, 0x96, 0xA2,
    0xC7, 0x2D, 0x1F, 0x0A, 0x0F, 0x6B, 0xD9, 0xAD, 0xA3, 0x11, 0x8F, 0x00,
    0xAA, 0x06, 0xC2, 0x1E, 0xF3, 0xE8, 0x5A, 0x37, 0x4C, 0xD6, 0x4B, 0x6B,
    0x01, 0xC9, 0xB0, 0xB6, 0xB9, 0x92, 0xED, 0x1D, 0x08, 0xB0, 0x80, 0x06,
    0x20, 0xEA, 0xEE, 0xF9, 0x1D, 0xA4, 0x57, 0x73, 0x2E, 0x1B, 0xA5, 0xAF,
    0xF6, 0xAF, 0xAE, 0x04, 0x7C, 0x4C, 0x7E, 0xC8, 0xDB, 0xC0, 0xFB, 0x37,
    0xC8, 0x7E, 0xFE, 0x47, 0x0A, 0x3C, 0xFA, 0x61, 0xE7, 0xEB, 0x1B, 0xF3,
    0x7C, 0x32, 0xE3, 0x7C, 0x37, 0x66, 0x7C, 0x53, 0x07, 0xC2, 0x37, 0xA3,
    0xBD, 0xF7, 0xFA, 0xE3, 0x8A, 0x76, 0xCB, 0x6C, 0xC8, 0x13, 0xC4, 0x53,
    0x53, 0xDB, 0xAD, 0x37, 0x1A, 0xEB, 0xE0};

// Class for testing the FFmpegH264ToAnnexBBitstreamConverter.
class FFmpegH264ToAnnexBBitstreamConverterTest : public testing::Test {
 protected:
  FFmpegH264ToAnnexBBitstreamConverterTest() {
    // Set up AVCConfigurationRecord correctly for tests.
    // It's ok to do const cast here as data in kHeaderDataOkWithFieldLen4 is
    // never written to.
    UNSAFE_TODO(memset(&test_parameters_, 0, sizeof(AVCodecParameters)));
    test_parameters_.extradata =
        const_cast<uint8_t*>(kHeaderDataOkWithFieldLen4);
    test_parameters_.extradata_size = sizeof(kHeaderDataOkWithFieldLen4);
  }

  FFmpegH264ToAnnexBBitstreamConverterTest(
      const FFmpegH264ToAnnexBBitstreamConverterTest&) = delete;
  FFmpegH264ToAnnexBBitstreamConverterTest& operator=(
      const FFmpegH264ToAnnexBBitstreamConverterTest&) = delete;

  void CreatePacket(AVPacket* packet, const uint8_t* data, uint32_t data_size) {
    // Create new packet sized of |data_size| from |data|.
    EXPECT_EQ(av_new_packet(packet, data_size), 0);
    UNSAFE_TODO(memcpy(packet->data, data, data_size));
  }

  // Variable to hold valid dummy parameters for testing.
  AVCodecParameters test_parameters_;
};

TEST_F(FFmpegH264ToAnnexBBitstreamConverterTest, Conversion_Success) {
  FFmpegH264ToAnnexBBitstreamConverter converter(&test_parameters_);

  auto test_packet = ScopedAVPacket::Allocate();
  CreatePacket(test_packet.get(), kPacketDataOkWithFieldLen4,
               sizeof(kPacketDataOkWithFieldLen4));

  // Try out the actual conversion (should be successful and allocate new
  // packet and destroy the old one).
  EXPECT_TRUE(converter.ConvertPacket(test_packet.get()));

  // Converter will be automatically cleaned up.
}

TEST_F(FFmpegH264ToAnnexBBitstreamConverterTest, Conversion_SuccessBigPacket) {
  FFmpegH264ToAnnexBBitstreamConverter converter(&test_parameters_);

  // Create new packet with 1000 excess bytes.
  auto test_packet = ScopedAVPacket::Allocate();
  static uint8_t excess_data[sizeof(kPacketDataOkWithFieldLen4) + 1000] = {};
  UNSAFE_TODO(memcpy(excess_data, kPacketDataOkWithFieldLen4,
                     sizeof(kPacketDataOkWithFieldLen4)));
  CreatePacket(test_packet.get(), excess_data, sizeof(excess_data));

  // Try out the actual conversion (should be successful and allocate new
  // packet and destroy the old one as we do NOT support in place transform).
  EXPECT_TRUE(converter.ConvertPacket(test_packet.get()));

  // Converter will be automatically cleaned up.
}

TEST_F(FFmpegH264ToAnnexBBitstreamConverterTest, Conversion_SuccessNullParams) {
  // Set up AVCConfigurationRecord to represent NULL data.
  AVCodecParameters dummy_parameters;
  dummy_parameters.extradata = nullptr;
  dummy_parameters.extradata_size = 0;
  FFmpegH264ToAnnexBBitstreamConverter converter(&dummy_parameters);

  // Try out the actual conversion with NULL parameter.
  EXPECT_FALSE(converter.ConvertPacket(nullptr));

  // Create new packet to test actual conversion.
  auto test_packet = ScopedAVPacket::Allocate();
  CreatePacket(test_packet.get(), kPacketDataOkWithFieldLen4,
               sizeof(kPacketDataOkWithFieldLen4));

  // Try out the actual conversion. This should fail due to missing extradata.
  auto* packet_data = test_packet->data;
  EXPECT_TRUE(converter.ConvertPacket(test_packet.get()));
  EXPECT_EQ(static_cast<size_t>(test_packet->size),
            sizeof(kPacketDataOkWithFieldLen4));
  EXPECT_EQ(test_packet->data, packet_data);

  // Converter will be automatically cleaned up.
}

TEST_F(FFmpegH264ToAnnexBBitstreamConverterTest,
       Conversion_SuccessTrailingNulls) {
  // Convert using the standard configuration to find the expected size.
  FFmpegH264ToAnnexBBitstreamConverter converter(&test_parameters_);
  auto expected_packet = ScopedAVPacket::Allocate();
  CreatePacket(expected_packet.get(), kPacketDataOkWithFieldLen4,
               sizeof(kPacketDataOkWithFieldLen4));
  EXPECT_TRUE(converter.ConvertPacket(expected_packet.get()));

  // Convert using the trailing nulls configuration.
  AVCodecParameters parameters;
  parameters.extradata =
      const_cast<uint8_t*>(kHeaderDataTrailingNullsWithFieldLen4);
  parameters.extradata_size = sizeof(kHeaderDataTrailingNullsWithFieldLen4);
  FFmpegH264ToAnnexBBitstreamConverter test_converter(&parameters);

  auto test_packet = ScopedAVPacket::Allocate();
  CreatePacket(test_packet.get(), kPacketDataOkWithFieldLen4,
               sizeof(kPacketDataOkWithFieldLen4));
  EXPECT_TRUE(test_converter.ConvertPacket(test_packet.get()));

  // The converted packets should be the same.
  EXPECT_EQ(expected_packet->size, test_packet->size);
}

}  // namespace media
