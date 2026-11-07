// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The content lane (src/app/CommunityContent.h): signed credits and testimonials from the website, checked before
// anything of them is used.
#include "TestHarness.h"

#include "app/CommunityContent.h"

#include <string>

using namespace sf;

namespace {
// A throwaway key made for these tests with `openssl genpkey -algorithm ed25519` (its private half was deleted).
const char* kTestKey = "0f29fc5f985f0a69c54c1322ede0de755c7f4572b561620ccaac1a765f22c307";
const std::string kJson =
    R"({"schema":1,"serial":100,"supporters":["Ada","Bob\u001b[31m"],"founding":{"limit":100,"taken":2,"names":["Ada"]},)"
    R"("testimonials":[{"quote":"Fits like a glove.","name":"Ada","role":"artist"}]})";
const char* kSigHex = "f670358cb0da29563f8afab6248e8ba82b052c370a1009772a965c9cd3e47f59"
                      "c3391fbeba9c6040a5d8e5e006df2fdf36db64309869445207f7ecb2d285d104";

std::string unhex(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out += char(std::stoi(h.substr(i, 2), nullptr, 16));
    return out;
}
} // namespace

TEST(content_key_is_not_the_release_key) {
    CHECK(std::string(kContentPublicKeyHex) != std::string(update::kUpdatePublicKeyHex));
    if (*kContentPublicKeyHex) { // once the key exists it must be one a signature could check out under
        uint8_t key[32];
        CHECK(update::parseHexKey(kContentPublicKeyHex, key) && ed25519PublicKeyValid(key));
    }
    CHECK(std::string(kContentUrl) == "https://sculptforge.app/content/v1.json");
    CHECK(std::string(kContentMirrorUrl) == "https://islandmed.github.io/content/v1.json");
}

TEST(content_signed_file_is_accepted_and_sanitised) {
    CommunityContent c;
    std::string error;
    CHECK(acceptCommunityContent(kJson, unhex(kSigHex), kTestKey, 0, c, error));
    CHECK(c.serial == 100);
    CHECK(c.hasSupporters && c.supporters.size() == 2 && c.supporters[1] == "Bob[31m"); // the escape is gone
    CHECK(c.hasFounding && c.founding.taken == 2 && c.founding.names.size() == 1);
    CHECK(c.testimonials.size() == 1 && c.testimonials[0].quote == "Fits like a glove." && c.testimonials[0].role == "artist");
}

TEST(content_refused_without_a_good_signature) {
    CommunityContent c;
    c.serial = 7;
    std::string error, sig = unhex(kSigHex);
    std::string tampered = kJson;
    tampered.replace(tampered.find("Ada"), 3, "Eve");
    CHECK(!acceptCommunityContent(tampered, sig, kTestKey, 0, c, error) && error == "bad signature");
    sig[3] ^= 1;
    CHECK(!acceptCommunityContent(kJson, sig, kTestKey, 0, c, error));
    CHECK(!acceptCommunityContent(kJson, "", kTestKey, 0, c, error));
    CHECK(!acceptCommunityContent(kJson, unhex(kSigHex), "", 0, c, error) && error == "no content key");
    CHECK(!acceptCommunityContent(kJson, unhex(kSigHex), update::kUpdatePublicKeyHex, 0, c, error)); // another key
    CHECK(!acceptCommunityContent(std::string(kContentMaxBytes + 1, ' '), unhex(kSigHex), kTestKey, 0, c, error));
    CHECK(c.serial == 7); // nothing was taken over
}

TEST(content_older_serial_is_refused) {
    CommunityContent c;
    std::string error;
    CHECK(!acceptCommunityContent(kJson, unhex(kSigHex), kTestKey, 101, c, error));
    CHECK(acceptCommunityContent(kJson, unhex(kSigHex), kTestKey, 100, c, error));
}

TEST(content_parser_is_strict) {
    CommunityContent c;
    std::string error;
    CHECK(!parseCommunityContent("[]", c, error));
    CHECK(!parseCommunityContent(R"({"schema":2,"serial":1})", c, error));
    CHECK(!parseCommunityContent(R"({"schema":1})", c, error));
    CHECK(parseCommunityContent(R"({"schema":1,"serial":5,"extra":{"x":1}})", c, error));
    CHECK(!c.hasSupporters && !c.hasFounding && c.testimonials.empty()); // missing sections: the compiled-in copy
    // testimonials follow parseTestimonials: an approved quote is shown exactly or skipped, never cut
    std::string many = R"({"schema":1,"serial":1,"testimonials":[)";
    for (int i = 0; i < 3; ++i) many += std::string(i ? "," : "") + R"({"quote":"Q)" + std::to_string(i) + R"(","name":"N"})";
    many += R"(,{"quote":")" + std::string(kTestimonialQuoteMax + 1, 'q') + R"(","name":"N"})";
    many += R"(,{"quote":"no name"},{"quote":"\u202Ehidden","name":"N"}]})";
    CHECK(parseCommunityContent(many, c, error));
    CHECK(c.hasTestimonials && c.testimonials.size() == 3 && c.testimonials[2].quote == "Q2");
}
