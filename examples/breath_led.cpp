#include "TTY.h"
#include "rgb_protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void OnSignal(int) {
    g_stop = 1;
}

struct Options {
    std::string port;
    int baud = 115200;
    rgb_protocol::Rgb base{0, 160, 255};
    double frequency_hz = 0.5;
    double fps = 50.0;
    double gamma = 1.0;
    double duration_seconds = 0.0;
    bool hold = false;
    bool test_colors = false;
    bool dry_run = false;
    bool quiet = false;
    bool list_ports = false;
    bool help = false;
};

void PrintUsage(const char* program) {
    std::printf(
        "UART RGB breathing-light sender (A5 R G B CRC16, 115200 8N1)\n"
        "\n"
        "Usage:\n"
        "  %s --list-ports\n"
        "  %s --dry-run [options]\n"
        "  %s --port COM5 [options]\n"
        "\n"
        "Options:\n"
        "  --port <name>       Serial port, for example COM5 or /dev/ttyUSB0\n"
        "  --baud <number>     Baud rate, default 115200\n"
        "  --color <R,G,B>     Peak colour, default 0,160,255\n"
        "  --freq <Hz>         Breathing frequency, default 0.5 (2 s period)\n"
        "  --fps <number>      Frames per second, default 50\n"
        "  --gamma <number>    Brightness gamma, default 1.0\n"
        "  --seconds <number>  Stop after N seconds, 0 means until Ctrl+C\n"
        "  --hold              Send a fixed colour (no breathing)\n"
        "  --test-colors       Cycle red, green, blue, white, off every 2 s\n"
        "  --dry-run           Generate frames without opening a serial port\n"
        "  --quiet             Do not print periodic status lines\n"
        "  --list-ports        List serial ports and exit\n"
        "  --help              Show this message\n"
        "\n"
        "Examples:\n"
        "  %s --list-ports\n"
        "  %s --dry-run --seconds 2\n"
        "  %s --port COM5 --color 255,0,0 --hold\n"
        "  %s --port COM5 --color 0,160,255 --seconds 20\n",
        program, program, program, program, program, program, program);
}

bool ParseInt(const std::string& text, int& value) {
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || parsed < -2147483647L ||
        parsed > 2147483647L) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDouble(const std::string& text, double& value) {
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseColor(const std::string& text, rgb_protocol::Rgb& color) {
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    int red = 0;
    int green = 0;
    int blue = 0;
    std::string trailing;
    if (!(stream >> red >> green >> blue) || (stream >> trailing)) {
        return false;
    }
    if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255) {
        return false;
    }
    color = rgb_protocol::Rgb{static_cast<std::uint8_t>(red),
                              static_cast<std::uint8_t>(green),
                              static_cast<std::uint8_t>(blue)};
    return true;
}

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        std::string inline_value;
        const std::size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            inline_value = argument.substr(equals + 1);
            argument.resize(equals);
        }

        auto next_value = [&](const char* name) -> const char* {
            if (!inline_value.empty()) {
                return inline_value.c_str();
            }
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (argument == "--port") {
            const char* value = next_value("--port");
            if (value == nullptr) return false;
            options.port = value;
        } else if (argument == "--baud") {
            const char* value = next_value("--baud");
            if (value == nullptr || !ParseInt(value, options.baud)) return false;
        } else if (argument == "--color") {
            const char* value = next_value("--color");
            if (value == nullptr || !ParseColor(value, options.base)) return false;
        } else if (argument == "--freq") {
            const char* value = next_value("--freq");
            if (value == nullptr || !ParseDouble(value, options.frequency_hz)) return false;
        } else if (argument == "--fps") {
            const char* value = next_value("--fps");
            if (value == nullptr || !ParseDouble(value, options.fps)) return false;
        } else if (argument == "--gamma") {
            const char* value = next_value("--gamma");
            if (value == nullptr || !ParseDouble(value, options.gamma)) return false;
        } else if (argument == "--seconds") {
            const char* value = next_value("--seconds");
            if (value == nullptr || !ParseDouble(value, options.duration_seconds)) return false;
        } else if (argument == "--hold") {
            options.hold = true;
        } else if (argument == "--test-colors") {
            options.test_colors = true;
        } else if (argument == "--dry-run") {
            options.dry_run = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--list-ports") {
            options.list_ports = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
            return true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
            return false;
        }
    }

    if (options.help || options.list_ports) {
        return true;
    }
    if (options.baud <= 0 || !TTY::IsSupportedBaud(options.baud)) {
        std::fprintf(stderr, "unsupported baud rate: %d\n", options.baud);
        return false;
    }
    if (options.frequency_hz <= 0.0 || options.frequency_hz > 10.0) {
        std::fprintf(stderr, "--freq must be in (0, 10]\n");
        return false;
    }
    if (options.fps < 1.0 || options.fps > 200.0) {
        std::fprintf(stderr, "--fps must be in [1, 200]\n");
        return false;
    }
    if (options.gamma <= 0.0 || options.gamma > 5.0) {
        std::fprintf(stderr, "--gamma must be in (0, 5]\n");
        return false;
    }
    if (options.duration_seconds < 0.0) {
        std::fprintf(stderr, "--seconds cannot be negative\n");
        return false;
    }
    if (!options.dry_run && options.port.empty()) {
        std::fprintf(stderr, "--port is required unless --dry-run or --list-ports is used\n");
        return false;
    }
    return true;
}

rgb_protocol::Rgb ChooseColor(const Options& options, double elapsed_seconds) {
    if (options.test_colors) {
        static constexpr rgb_protocol::Rgb kPalette[] = {
            {255, 0, 0},
            {0, 255, 0},
            {0, 0, 255},
            {255, 255, 255},
            {0, 0, 0},
        };
        const std::size_t index =
            static_cast<std::size_t>(elapsed_seconds / 2.0) % std::size(kPalette);
        return kPalette[index];
    }
    if (options.hold) {
        return options.base;
    }
    return rgb_protocol::CalculateBreathColor(options.base, elapsed_seconds,
                                              options.frequency_hz, options.gamma);
}

void PrintStatus(double elapsed_seconds, rgb_protocol::Rgb color,
                 const rgb_protocol::Frame& frame) {
    std::printf("%8.3f s  RGB=(%3u,%3u,%3u)  %02X %02X %02X %02X %02X %02X\n",
                elapsed_seconds, static_cast<unsigned>(color.red),
                static_cast<unsigned>(color.green), static_cast<unsigned>(color.blue),
                frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        if (argc == 1) {
            PrintUsage(argv[0]);
        }
        return 1;
    }
    if (options.help) {
        PrintUsage(argv[0]);
        return 0;
    }
    if (options.list_ports) {
        const auto ports = TTY::ListPorts();
        if (ports.empty()) {
            std::printf("No serial ports found.\n");
        } else {
            for (const std::string& port : ports) {
                std::printf("%s\n", port.c_str());
            }
        }
        return 0;
    }

    std::signal(SIGINT, OnSignal);
#ifndef _WIN32
    std::signal(SIGTERM, OnSignal);
#endif

    std::unique_ptr<TTY> tty;
    if (!options.dry_run) {
        try {
            tty = std::make_unique<TTY>(options.port, options.baud);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return 2;
        }
        std::printf("Opened %s at %d 8N1, protocol A5 R G B CRC16.\n",
                    options.port.c_str(), options.baud);
        std::fflush(stdout);
    } else {
        std::printf("Dry run: no serial port opened.\n");
    }

    const auto period = std::chrono::duration<double>(1.0 / options.fps);
    const auto start = std::chrono::steady_clock::now();
    auto next_wake = start;
    double next_report = 0.0;
    std::uint64_t frames_sent = 0;
    bool write_failed = false;

    while (!g_stop) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start).count();
        if (options.duration_seconds > 0.0 && elapsed >= options.duration_seconds) {
            break;
        }

        const rgb_protocol::Rgb color = ChooseColor(options, elapsed);
        const rgb_protocol::Frame frame = rgb_protocol::MakeFrame(color);

        if (tty && !tty->Write(frame.data(), frame.size())) {
            std::fprintf(stderr, "serial write failed: %s\n", tty->LastError().c_str());
            write_failed = true;
            break;
        }
        ++frames_sent;

        if (!options.quiet && elapsed >= next_report) {
            PrintStatus(elapsed, color, frame);
            next_report += 0.25;
        }

        next_wake += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        const auto after_work = std::chrono::steady_clock::now();
        if (next_wake > after_work) {
            std::this_thread::sleep_until(next_wake);
        } else {
            next_wake = after_work +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        }
    }

    if (tty) {
        const rgb_protocol::Frame off = rgb_protocol::MakeFrame({0, 0, 0});
        if (!tty->Write(off.data(), off.size())) {
            std::fprintf(stderr, "could not send the final off frame: %s\n",
                         tty->LastError().c_str());
        }
    }

    std::printf("Stopped after %llu frames.\n",
                static_cast<unsigned long long>(frames_sent));
    return write_failed ? 3 : 0;
}