// ============================================================================
//  torcontroller.h — Manages Tor daemon lifecycle + SOCKS proxy.
// ============================================================================
#pragma once

#include <QString>
#include <QObject>
#include <QProcess>
#include <functional>

class TorController : public QObject {
public:
    static TorController& instance();

    // ── Status ──────────────────────────────────────────────────────────
    enum class State {
        NotRunning,
        Starting,
        Bootstrapping,
        Ready,
        Error
    };

    State state() const { return state_; }
    bool  isReady() const { return state_ == State::Ready; }
    int   bootstrapPercent() const { return bootstrapPct_; }
    QString errorMessage() const { return errorMsg_; }
    int   socksPort() const { return socksPort_; }

    // ── Control ─────────────────────────────────────────────────────────
    // Start Tor if not already running. Async: `onReady` fires when
    // bootstrap reaches 100% (or immediately if already running).
    void start(std::function<void(bool ok)> onReady = nullptr);

    // Stop Tor (only if we started it — respects existing system Tor).
    void stop();

    // Rotate circuit (new identity). Sends NEWNYM signal.
    void newIdentity();

    // ── Config ──────────────────────────────────────────────────────────
    void setSocksPort(int port) { socksPort_ = port; }
    void setTorBinary(const QString& path) { torBinary_ = path; }

    // Detect common Tor binary locations + whether system Tor is running.
    static QString findTorBinary();
    bool isSystemTorRunning() const;

private:
    TorController();
    ~TorController();
    TorController(const TorController&) = delete;
    TorController& operator=(const TorController&) = delete;

    void parseTorOutput(const QString& line);
    void checkBootstrapFromLog();
    void notifyReady();

    QProcess*     torProcess_ = nullptr;
    State         state_ = State::NotRunning;
    int           bootstrapPct_ = 0;
    int           socksPort_ = 9050;
    QString       torBinary_;
    QString       errorMsg_;
    bool          weStartedTor_ = false;
    std::function<void(bool)> readyCallback_;
};
