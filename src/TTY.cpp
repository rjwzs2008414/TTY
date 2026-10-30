#include "TTY.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

int RemainingMilliseconds(std::chrono::steady_clock::time_point deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
        return 0;
    }
    const auto capped = std::min<long long>(remaining.count(), 2147483647LL);
    return static_cast<int>(capped);
}

#ifdef _WIN32

std::string WindowsErrorText(DWORD code) {
    // Numeric text avoids mojibake when the Windows system message is localized
    // and the console code page does not match the source encoding.
    return "Windows error " + std::to_string(code);
}

#else

speed_t BaudToSpeed(int baud) {
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B500000
    case 500000: return B500000;
#endif
#ifdef B576000
    case 576000: return B576000;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1152000
    case 1152000: return B1152000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B2500000
    case 2500000: return B2500000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
#ifdef B3500000
    case 3500000: return B3500000;
#endif
#ifdef B4000000
    case 4000000: return B4000000;
#endif
    default: return 0;
    }
}



#endif  // _WIN32

}  // namespace

TTY::TTY(const std::string& device, int baud) {
    if (device.empty()) {
        throw std::invalid_argument("serial port name is empty");
    }
    if (baud <= 0) {
        throw std::invalid_argument("baud rate must be positive");
    }
    if (!IsSupportedBaud(baud)) {
        throw std::invalid_argument("unsupported baud rate: " + std::to_string(baud));
    }

#ifdef _WIN32
    const std::string path = "\\\\.\\" + device;
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        SetError("cannot open " + device + ": " + WindowsErrorText(GetLastError()));
        throw std::runtime_error(error_);
    }
    handle_ = handle;

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (GetCommState(handle, &dcb) == 0) {
        SetError("GetCommState failed: " + WindowsErrorText(GetLastError()));
        Close();
        throw std::runtime_error(error_);
    }

    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;

    if (SetCommState(handle, &dcb) == 0) {
        SetError("SetCommState failed: " + WindowsErrorText(GetLastError()));
        Close();
        throw std::runtime_error(error_);
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;
    if (SetCommTimeouts(handle, &timeouts) == 0) {
        SetError("SetCommTimeouts failed: " + WindowsErrorText(GetLastError()));
        Close();
        throw std::runtime_error(error_);
    }

    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
#else
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        SetError("cannot open " + device + ": " + std::strerror(errno));
        throw std::runtime_error(error_);
    }

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        SetError("tcgetattr failed: " + std::string(std::strerror(errno)));
        Close();
        throw std::runtime_error(error_);
    }

    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    const speed_t speed = BaudToSpeed(baud);
    if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) {
        SetError("cfsetispeed/cfsetospeed failed: " + std::string(std::strerror(errno)));
        Close();
        throw std::runtime_error(error_);
    }
    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        SetError("tcsetattr failed: " + std::string(std::strerror(errno)));
        Close();
        throw std::runtime_error(error_);
    }

    tcflush(fd_, TCIOFLUSH);
#endif
}

TTY::~TTY() {
    Close();
}

TTY::TTY(TTY&& other) noexcept {
#ifdef _WIN32
    handle_ = other.handle_;
    other.handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    error_ = std::move(other.error_);
}

TTY& TTY::operator=(TTY&& other) noexcept {
    if (this != &other) {
        Close();
#ifdef _WIN32
        handle_ = other.handle_;
        other.handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        error_ = std::move(other.error_);
    }
    return *this;
}

bool TTY::IsOpen() const noexcept {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

bool TTY::Write(const void* data, std::size_t length) {
    if (!IsOpen()) {
        SetError("serial port is not open");
        return false;
    }
    if (data == nullptr && length != 0) {
        SetError("write buffer is null");
        return false;
    }
    if (length == 0) {
        return true;
    }

    const auto* bytes = static_cast<const unsigned char*>(data);

#ifdef _WIN32
    std::size_t offset = 0;
    while (offset < length) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(length - offset, 0xFFFFFFFFu));
        DWORD written = 0;
        if (WriteFile(static_cast<HANDLE>(handle_), bytes + offset, chunk, &written, nullptr) == 0) {
            SetError("WriteFile failed: " + WindowsErrorText(GetLastError()));
            return false;
        }
        if (written == 0) {
            SetError("WriteFile wrote 0 bytes");
            return false;
        }
        offset += written;
    }
    return true;
#else
    std::size_t offset = 0;
    while (offset < length) {
        const ssize_t written = ::write(fd_, bytes + offset, length - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = fd_;
            pfd.events = POLLOUT;
            const int ready = ::poll(&pfd, 1, 1000);
            if (ready > 0) {
                continue;
            }
            if (ready == 0) {
                SetError("write timed out");
            } else if (errno != EINTR) {
                SetError("poll(POLLOUT) failed: " + std::string(std::strerror(errno)));
            }
            return false;
        }
        SetError("write failed: " + std::string(std::strerror(errno)));
        return false;
    }
    return true;
#endif
}

bool TTY::Read(void* data, std::size_t length, int timeout_ms) {
    if (!IsOpen()) {
        SetError("serial port is not open");
        return false;
    }
    if (data == nullptr && length != 0) {
        SetError("read buffer is null");
        return false;
    }
    if (length == 0) {
        return true;
    }

    auto* bytes = static_cast<unsigned char*>(data);
    const auto deadline = timeout_ms < 0
                              ? std::chrono::steady_clock::time_point::max()
                              : std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(timeout_ms);

#ifdef _WIN32
    std::size_t offset = 0;
    while (offset < length) {
        int wait_ms = -1;
        if (timeout_ms >= 0) {
            wait_ms = RemainingMilliseconds(deadline);
            if (wait_ms == 0 && offset == 0) {
                SetError("read timed out");
                return false;
            }
            if (wait_ms == 0) {
                wait_ms = 1;
            }
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 20;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = wait_ms < 0 ? 0 : static_cast<DWORD>(wait_ms);
        SetCommTimeouts(static_cast<HANDLE>(handle_), &timeouts);

        DWORD got = 0;
        if (ReadFile(static_cast<HANDLE>(handle_), bytes + offset,
                     static_cast<DWORD>(std::min<std::size_t>(length - offset, 0xFFFFFFFFu)),
                     &got, nullptr) == 0) {
            SetError("ReadFile failed: " + WindowsErrorText(GetLastError()));
            return false;
        }
        if (got == 0) {
            SetError("read timed out");
            return false;
        }
        offset += got;
    }
    return true;
#else
    std::size_t offset = 0;
    while (offset < length) {
        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        int wait_ms = -1;
        if (timeout_ms >= 0) {
            wait_ms = RemainingMilliseconds(deadline);
            if (wait_ms == 0 && offset == 0) {
                SetError("read timed out");
                return false;
            }
        }

        const int ready = ::poll(&pfd, 1, wait_ms);
        if (ready == 0) {
            SetError("read timed out");
            return false;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            SetError("poll(POLLIN) failed: " + std::string(std::strerror(errno)));
            return false;
        }

        const ssize_t got = ::read(fd_, bytes + offset, length - offset);
        if (got > 0) {
            offset += static_cast<std::size_t>(got);
            continue;
        }
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        SetError("read failed: " + std::string(std::strerror(errno)));
        return false;
    }
    return true;
#endif
}

void TTY::Close() noexcept {
#ifdef _WIN32
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(handle_));
    }
    handle_ = nullptr;
#else
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
#endif
}

void TTY::SetError(const std::string& message) {
    error_ = message;
}

std::vector<std::string> TTY::ListPorts() {
    std::vector<std::string> ports;
#ifdef _WIN32
    for (int number = 1; number <= 256; ++number) {
        const std::string name = "COM" + std::to_string(number);
        char target[512] = {0};
        if (QueryDosDeviceA(name.c_str(), target, sizeof(target)) != 0) {
            ports.push_back(name);
        }
    }
#else
    const char* patterns[] = {
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
        "/dev/ttyTHS*",
        "/dev/ttyS*",
    };
    for (const char* pattern : patterns) {
        glob_t matches{};
        if (::glob(pattern, 0, nullptr, &matches) == 0) {
            for (std::size_t i = 0; i < matches.gl_pathc; ++i) {
                ports.emplace_back(matches.gl_pathv[i]);
            }
        }
        ::globfree(&matches);
    }
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
#endif
    return ports;
}

bool TTY::IsSupportedBaud(int baud) noexcept {
#ifdef _WIN32
    return baud > 0;
#else
    return BaudToSpeed(baud) != 0;
#endif
}