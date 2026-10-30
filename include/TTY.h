#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Small cross-platform serial-port wrapper.
//
// The class is a transport only: it writes/reads raw bytes and never appends a
// checksum on its own.  The RGB protocol (header + RGB + CRC-16) lives in
// rgb_protocol.h so that both sides can use the same frame definition.
class TTY {
public:
    // Opens the port and configures 8N1, no flow control.
    // Throws std::runtime_error when the port cannot be opened/configured.
    TTY(const std::string& device, int baud);
    ~TTY();

    TTY(const TTY&) = delete;
    TTY& operator=(const TTY&) = delete;
    TTY(TTY&& other) noexcept;
    TTY& operator=(TTY&& other) noexcept;

    bool IsOpen() const noexcept;

    // Writes exactly `length` bytes.  Returns false on error.
    bool Write(const void* data, std::size_t length);

    // Reads exactly `length` bytes or times out.  Returns false on timeout or
    // error; `timeout_ms < 0` means wait forever.
    bool Read(void* data, std::size_t length, int timeout_ms = 1000);

    // Compatibility helpers.  `count` is the number of T objects, not bytes.
    template <typename T>
    bool SendData(const T* data, std::size_t count) {
        return Write(data, count * sizeof(T));
    }

    bool SendData(const std::string& data) {
        return Write(data.data(), data.size());
    }

    template <typename T>
    bool ReceiveData(T* data, std::size_t count, int timeout_ms = 1000) {
        return Read(data, count * sizeof(T), timeout_ms);
    }

    void Close() noexcept;

    const std::string& LastError() const noexcept { return error_; }

    // Windows: COM1..COM256 that the OS reports.
    // Linux: /dev/ttyUSB*, /dev/ttyACM*, /dev/ttyTHS*, /dev/ttyS*.
    static std::vector<std::string> ListPorts();

    static bool IsSupportedBaud(int baud) noexcept;

private:
    void SetError(const std::string& message);

#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    std::string error_;
};