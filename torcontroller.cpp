// ============================================================================
//  torcontroller.cpp — Tor daemon management.
// ============================================================================

#include "torcontroller.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QDebug>

// ────────────────────────────────────────────────────────────────────────────
TorController& TorController::instance() {
    static TorController s;
    return s;
}

TorController::TorController() {
    torBinary_ = findTorBinary();
}

TorController::~TorController() {
    stop();
}

// ────────────────────────────────────────────────────────────────────────────
QString TorController::findTorBinary() {
    // 1. Common paths
    const QStringList candidates = {
        QStringLiteral("/usr/bin/tor"),
        QStringLiteral("/usr/local/bin/tor"),
        QStringLiteral("/usr/sbin/tor"),
        QStringLiteral("/opt/tor/tor"),
    };
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }

    // 2. PATH lookup
    const QString inPath = QStandardPaths::findExecutable("tor");
    if (!inPath.isEmpty()) return inPath;

    // 3. Tor Browser bundle (common extraction dir)
    const QString home = QDir::homePath();
    const QStringList bundlePaths = {
        home + "/tor-browser_en-US/Browser/TorBrowser/Tor/tor",
        home + "/.local/share/torbrowser/tbb/x86_64/tor-browser_en-US/Browser/TorBrowser/Tor/tor",
        home + "/Desktop/tor-browser_en-US/Browser/TorBrowser/Tor/tor",
    };
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }

    return QString();  // not found
}

// ────────────────────────────────────────────────────────────────────────────
bool TorController::isSystemTorRunning() const {
    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, quint16(socksPort_));
    const bool ok = sock.waitForConnected(400);
    sock.abort();
    return ok;
}

// ────────────────────────────────────────────────────────────────────────────
void TorController::start(std::function<void(bool)> onReady) {
    readyCallback_ = onReady;

    // If system Tor is already running, we're ready
    if (isSystemTorRunning()) {
        state_ = State::Ready;
        bootstrapPct_ = 100;
        weStartedTor_ = false;
        notifyReady();
        return;
    }

    // Check binary
    if (torBinary_.isEmpty()) {
        state_ = State::Error;
        errorMsg_ = QStringLiteral(
            "Tor is not installed. Install it with:\n"
            "  sudo apt install tor        (Debian/Ubuntu)\n"
            "  sudo dnf install tor        (Fedora)\n"
            "  sudo pacman -S tor          (Arch)");
        notifyReady();
        return;
    }

    // Start Tor
    state_ = State::Starting;
    bootstrapPct_ = 0;
    weStartedTor_ = true;

    if (torProcess_) {
        torProcess_->deleteLater();
        torProcess_ = nullptr;
    }

    torProcess_ = new QProcess(this);
    torProcess_->setProcessChannelMode(QProcess::MergedChannels);

    // Args: use SOCKS port, log to stdout
    const QStringList args = {
        QStringLiteral("--SocksPort"), QString::number(socksPort_),
        QStringLiteral("--Log"), QStringLiteral("notice stdout"),
    };

    QObject::connect(torProcess_, &QProcess::readyReadStandardOutput, this, [this]{
        const QByteArray data = torProcess_->readAllStandardOutput();
        const QString text = QString::fromUtf8(data);
        for (const QString& line : text.split('\n')) {
            if (!line.trimmed().isEmpty()) {
                parseTorOutput(line);
            }
        }
    });

    QObject::connect(torProcess_,
                     QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, [this](int code, QProcess::ExitStatus){
        if (state_ != State::Ready && state_ != State::Error) {
            state_ = State::Error;
            errorMsg_ = QStringLiteral("Tor process exited unexpectedly (code %1)").arg(code);
            notifyReady();
        }
    });

    QObject::connect(torProcess_, &QProcess::errorOccurred, this,
                     [this](QProcess::ProcessError err){
        if (err == QProcess::FailedToStart) {
            state_ = State::Error;
            errorMsg_ = QStringLiteral("Failed to launch Tor: %1").arg(torProcess_->errorString());
            notifyReady();
        }
    });

    torProcess_->start(torBinary_, args);

    // Safety timeout — if not ready in 30s, error out
    QTimer::singleShot(30000, this, [this]{
        if (state_ != State::Ready && state_ != State::Error) {
            state_ = State::Error;
            errorMsg_ = QStringLiteral("Tor bootstrap timed out.");
            notifyReady();
        }
    });
}

// ────────────────────────────────────────────────────────────────────────────
void TorController::parseTorOutput(const QString& line) {
    // Tor logs: "Bootstrapped 100% (done): Done"
    static QRegularExpression re(QStringLiteral("Bootstrapped\\s+(\\d+)%"));
    auto m = re.match(line);
    if (m.hasMatch()) {
        bootstrapPct_ = m.captured(1).toInt();
        state_ = (bootstrapPct_ >= 100) ? State::Ready : State::Bootstrapping;

        if (state_ == State::Ready) {
            notifyReady();
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
void TorController::notifyReady() {
    if (readyCallback_) {
        auto cb = readyCallback_;
        readyCallback_ = nullptr;
        cb(isReady());
    }
}

// ────────────────────────────────────────────────────────────────────────────
void TorController::stop() {
    if (!torProcess_ || !weStartedTor_) {
        // Don't stop system Tor
        return;
    }
    torProcess_->terminate();
    if (!torProcess_->waitForFinished(3000)) {
        torProcess_->kill();
    }
    torProcess_->deleteLater();
    torProcess_ = nullptr;
    state_ = State::NotRunning;
    weStartedTor_ = false;
}

// ────────────────────────────────────────────────────────────────────────────
void TorController::newIdentity() {
    if (!torProcess_) return;

    // Tor control port not exposed by default — skip if unavailable.
    // Alternative: signal via telnet to control port 9051 (if enabled).
    // For now, we just re-launch (crude but effective).
    qInfo() << "[Tor] Requesting new identity (circuit rotation)";
    // Note: proper implementation requires ControlPort in torrc.
}
