#include "rgb_protocol.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string Hex(const rgb_protocol::Frame& frame) {
    char text[32] = {0};
    std::snprintf(text, sizeof(text), "%02X %02X %02X %02X %02X %02X",
                  frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
    return text;
}

void CheckFrame(rgb_protocol::Rgb color, const char* expected) {
    const auto frame = rgb_protocol::MakeFrame(color);
    Check(Hex(frame) == expected,
          "frame for (" + std::to_string(color.red) + "," +
              std::to_string(color.green) + "," + std::to_string(color.blue) +
              ") = " + Hex(frame) + ", expected " + expected);
}

}  // namespace

int main() {
    const std::uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    Check(rgb_protocol::Crc16CcittFalse(check, sizeof(check)) == 0x29B1u,
          "CRC check value for 123456789");

    CheckFrame({0, 0, 0}, "A5 00 00 00 F3 D2");
    CheckFrame({255, 0, 0}, "A5 FF 00 00 90 1D");
    CheckFrame({0, 255, 0}, "A5 00 FF 00 0C D1");
    CheckFrame({0, 0, 255}, "A5 00 00 FF 03 CC");
    CheckFrame({255, 255, 255}, "A5 FF FF FF 9F 00");
    CheckFrame({0, 160, 255}, "A5 00 A0 FF 7D D1");

    const rgb_protocol::Rgb base{0, 160, 255};
    const auto dark = rgb_protocol::CalculateBreathColor(base, 0.0, 0.5);
    Check(dark.red == 0 && dark.green == 0 && dark.blue == 0,
          "breath curve starts dark");
    const auto peak = rgb_protocol::CalculateBreathColor(base, 1.0, 0.5);
    Check(peak.red == 0 && peak.green == 160 && peak.blue == 255,
          "breath curve reaches base colour after half a period");
    const auto dark_again = rgb_protocol::CalculateBreathColor(base, 2.0, 0.5);
    Check(dark_again.red == 0 && dark_again.green == 0 && dark_again.blue == 0,
          "breath curve returns to dark after one period");

    // Garbage, one good frame, one corrupt frame, then another good frame.
    const std::uint8_t stream[] = {
        0x00, 0x11,
        0xA5, 0xFF, 0x00, 0x00, 0x90, 0x1D,
        0xA5, 0x00, 0xFF, 0x00, 0x0C, 0xD0,
        0xA5, 0x00, 0x00, 0xFF, 0x03, 0xCC,
    };

    rgb_protocol::Decoder decoder;
    int accepted = 0;
    int rejected = 0;
    bool saw_red = false;
    bool saw_blue = false;
    for (std::uint8_t byte : stream) {
        rgb_protocol::Rgb color;
        const auto result = decoder.Push(byte, color);
        if (result == rgb_protocol::Decoder::Result::Accepted) {
            ++accepted;
            saw_red = saw_red || (color.red == 255 && color.green == 0 && color.blue == 0);
            saw_blue = saw_blue || (color.red == 0 && color.green == 0 && color.blue == 255);
        } else if (result == rgb_protocol::Decoder::Result::Rejected) {
            ++rejected;
        }
    }

    Check(accepted == 2, "decoder accepts exactly the two valid frames");
    Check(rejected >= 3, "decoder rejects garbage and the corrupt frame");
    Check(saw_red && saw_blue, "decoder recovers the red and blue payloads");

    if (g_failures == 0) {
        std::printf("All protocol tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed.\n", g_failures);
    return 1;
}