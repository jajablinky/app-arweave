/*******************************************************************************
*  Modifications (c) 2026 Forward Research
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
********************************************************************************/

#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include "parser_dataitem.h"

static const uint8_t TAGS[] = {
    0x0c, 0x1a, 0x44, 0x61, 0x74, 0x61, 0x2d, 0x50, 0x72, 0x6f, 0x74, 0x6f,
    0x63, 0x6f, 0x6c, 0x04, 0x61, 0x6f, 0x0e, 0x56, 0x61, 0x72, 0x69, 0x61,
    0x6e, 0x74, 0x0e, 0x61, 0x6f, 0x2e, 0x54, 0x4e, 0x2e, 0x31, 0x08, 0x54,
    0x79, 0x70, 0x65, 0x0e, 0x4d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0x0c,
    0x41, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x10, 0x54, 0x72, 0x61, 0x6e, 0x73,
    0x66, 0x65, 0x72, 0x12, 0x52, 0x65, 0x63, 0x69, 0x70, 0x69, 0x65, 0x6e,
    0x74, 0x22, 0x72, 0x65, 0x63, 0x69, 0x70, 0x69, 0x65, 0x6e, 0x74, 0x2d,
    0x61, 0x64, 0x64, 0x72, 0x65, 0x73, 0x73, 0x10, 0x51, 0x75, 0x61, 0x6e,
    0x74, 0x69, 0x74, 0x79, 0x08, 0x31, 0x30, 0x30, 0x30, 0x00,
};

static void append_u64(std::vector<uint8_t> &out, uint64_t value) {
    for (uint8_t i = 0; i < 8; i++) out.push_back((value >> (8 * i)) & 0xff);
}

static std::vector<uint8_t> valid_dataitem() {
    std::vector<uint8_t> out = {1, 0};
    out.insert(out.end(), 512, 0);
    out.insert(out.end(), 512, 0x11);
    out.push_back(1);
    out.insert(out.end(), 32, 0x22);
    out.push_back(0);
    append_u64(out, 6);
    append_u64(out, sizeof(TAGS));
    out.insert(out.end(), TAGS, TAGS + sizeof(TAGS));
    const char data[] = "hello ao";
    out.insert(out.end(), data, data + sizeof(data) - 1);
    return out;
}

static std::vector<uint8_t> dataitem_with_tags(uint64_t count,
                                                const std::vector<uint8_t> &tags) {
    std::vector<uint8_t> out = {1, 0};
    out.insert(out.end(), 512, 0);
    out.insert(out.end(), 512, 0x11);
    out.push_back(0);
    out.push_back(0);
    append_u64(out, count);
    append_u64(out, tags.size());
    out.insert(out.end(), tags.begin(), tags.end());
    return out;
}

TEST(DataItem, ParsesCanonicalANS104AndMatchesArbundlesDigest) {
    const auto blob = valid_dataitem();
    parser_context_t ctx;
    ASSERT_EQ(dataitem_parse(&ctx, blob.data(), blob.size()), parser_ok);
    ASSERT_EQ(dataitem_validate(&ctx), parser_ok);

    uint8_t digest[48] = {0};
    ASSERT_EQ(dataitem_getDigest(digest, sizeof(digest)), parser_ok);
    char digest_hex[97] = {0};
    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(digest_hex + i * 2, sizeof(digest_hex) - i * 2, "%02x", digest[i]);
    }
    EXPECT_STREQ(digest_hex,
                 "27b0e89d5e4be7c252f835ad57fc0999d4cf9c7d859e6853640a4dac4c15c871"
                 "b5415abbb66dbb37a0f7daab2d197300");

    uint8_t items = 0;
    ASSERT_EQ(dataitem_getNumItems(&ctx, &items), parser_ok);
    EXPECT_EQ(items, 10);
}

TEST(DataItem, RejectsWrongSignatureType) {
    auto blob = valid_dataitem();
    blob[0] = 2;
    parser_context_t ctx;
    EXPECT_EQ(dataitem_parse(&ctx, blob.data(), blob.size()), parser_unexpected_type);
}

TEST(DataItem, RejectsTruncatedAvroTags) {
    auto blob = valid_dataitem();
    blob.resize(blob.size() - 12);
    parser_context_t ctx;
    EXPECT_NE(dataitem_parse(&ctx, blob.data(), blob.size()), parser_ok);
}

TEST(DataItem, AcceptsAnEmptyTagBlock) {
    std::vector<uint8_t> blob = {1, 0};
    blob.insert(blob.end(), 512, 0);
    blob.insert(blob.end(), 512, 0x11);
    blob.push_back(0);
    blob.push_back(0);
    append_u64(blob, 0);
    append_u64(blob, 0);

    parser_context_t ctx;
    EXPECT_EQ(dataitem_parse(&ctx, blob.data(), blob.size()), parser_ok);
}

TEST(DataItem, RejectsTagCountWithoutTagBytes) {
    std::vector<uint8_t> blob = {1, 0};
    blob.insert(blob.end(), 512, 0);
    blob.insert(blob.end(), 512, 0x11);
    blob.push_back(0);
    blob.push_back(0);
    append_u64(blob, 1);
    append_u64(blob, 0);

    parser_context_t ctx;
    EXPECT_EQ(dataitem_parse(&ctx, blob.data(), blob.size()), parser_unexpected_number_items);
}

TEST(DataItem, RejectsDuplicateTagNames) {
    const std::vector<uint8_t> tags = {4, 2, 'A', 2, '1', 2, 'A', 2, '2', 0};
    const auto blob = dataitem_with_tags(2, tags);
    parser_context_t ctx;
    EXPECT_EQ(dataitem_parse(&ctx, blob.data(), blob.size()), parser_unexpected_field);
}

TEST(DataItem, RejectsControlCharactersInTags) {
    const std::vector<uint8_t> tags = {2, 2, 1, 2, '1', 0};
    const auto blob = dataitem_with_tags(1, tags);
    parser_context_t ctx;
    EXPECT_EQ(dataitem_parse(&ctx, blob.data(), blob.size()), parser_unexpected_characters);
}
