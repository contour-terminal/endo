#pragma once

#include <shell/UnixPipe.h>

#include <mutex>
#include <sstream>
#include <thread>

#include <sys/ioctl.h>

#include <termios.h>

namespace endo
{

class TTY
{
  public:
    TTY() = default;
    virtual ~TTY() = default;

    TTY(TTY const&) = delete;
    TTY& operator=(TTY const&) = delete;

    TTY(TTY&&) = default;
    TTY& operator=(TTY&&) = default;

    [[nodiscard]] virtual int inputFd() const noexcept = 0;
    [[nodiscard]] virtual int outputFd() const noexcept = 0;

    virtual void setRawMode() = 0;
    virtual void restoreMode() = 0;

    virtual void writeToStdout(std::string_view str) const = 0;

    template <typename... Args>
    void writeToStdout(fmt::format_string<Args...> const& fmt, Args&&... args) const
    {
        writeToStdout(fmt::format(fmt, std::forward<Args>(args)...));
    }

    virtual void writeToStdin(std::string_view str) const = 0;

    template <typename... Args>
    void writeToStdin(fmt::format_string<Args...> const& fmt, Args&&... args) const
    {
        writeToStdin(fmt::format(fmt, std::forward<Args>(args)...));
    }
};

class RealTTY final: public TTY
{
  public:
    RealTTY();
    ~RealTTY() override;

    [[nodiscard]] static RealTTY& instance();

    [[nodiscard]] int inputFd() const noexcept override;
    [[nodiscard]] int outputFd() const noexcept override;

    void setRawMode() override;
    void restoreMode() override;

    void writeToStdout(std::string_view str) const override;
    void writeToStdin(std::string_view str) const override;

  private:
    termios _originalTermios {};
};

// This is a TTY implementation that can be used for testing.
// It uses a PTY to simulate a TTY.
// The output of the TTY is stored in a buffer that can be inspected.
class TestPTY final: public TTY
{
  public:
    TestPTY();
    ~TestPTY() override;

    [[nodiscard]] int inputFd() const noexcept override;
    [[nodiscard]] int outputFd() const noexcept override;

    void setRawMode() override;
    void restoreMode() override;

    void writeToStdout(std::string_view str) const override;
    void writeToStdin(std::string_view str) const override;

    // Returns the output that was written to the TTY.
    [[nodiscard]] std::string_view output() const noexcept;

  private:
    // This is the thread that reads from the output pipe and stores the output for later introspection.
    void outputUpdateLoop();

  private:
    std::string _output;
    std::thread _updateThread;
    mutable std::mutex _outputMutex;

    int _ptyMaster = -1;
    int _ptySlave = -1;
    termios _baseTermios {};
    winsize _windowSize {};

    bool _closed = false;
};

} // namespace endo
