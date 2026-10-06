// ============================================================================
//  RootBrowser — main.cpp (bookmarks + downloads + download panel)
//
//  Build:
//    g++ -std=c++17 -O2 -fPIC \
//        main.cpp connector.cpp viewpagesource.cpp webadblocker.cpp \
//        bookmarkstore.cpp bookmarkpage.cpp \
//        downloadmanager.cpp downloadpage.cpp downloadpanel.cpp \
//        -o graphite \
//        $(pkg-config --cflags --libs Qt6WebEngineWidgets Qt6Widgets)
//    ./graphite
// ============================================================================

#include "connector.h"
#include "webadblocker.h"
#include "bookmarkstore.h"
#include "historystore.h"
#include "settingsstore.h"
#include "torcontroller.h"
#include "privatemodepage.h"
#include "privatebrowser.h"
#include "privatehomepage.h"
#include "settingspage.h"
#include "historypage.h"
#include "bookmarkpage.h"
#include "downloadmanager.h"
#include "downloadpage.h"
#include "downloadpanel.h"
#include "downloadnotification.h"
#include "downloadtoast.h"
#include "findinpage.h"
#include "commandpalette.h"
#include "httpsonly.h"
#include "fingerprintprotection.h"
#include "shortcuthelp.h"
#include "urlautocomplete.h"
#include "bookmarksbar.h"
#include "background.h"

#include <QApplication>
#include <QDebug>
#include <QNetworkProxy>
#include <QWidget>
#include <QAbstractButton>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QContextMenuEvent>
#include <QChildEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QGraphicsDropShadowEffect>
#include <QElapsedTimer>
#include <QDateTime>
#include <QLocale>
#include <QUrl>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QPalette>
#include <QWindow>
#include <QImage>
#include <QStandardPaths>
#include <QIcon>
#include <QPixmap>
#include <QClipboard>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHash>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QFileDialog>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineHistory>
#include <QWebEngineDownloadRequest>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineContextMenuRequest>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <functional>
#include <algorithm>
#include <cmath>

static const char* kAppName = "RootBrowser";
static const double kPi = 3.14159265358979323846;

// ============================================================================
//  YouTube ad neutralization script
// ============================================================================
static const char* kYouTubeAdNeutralizer = R"JS(
(function() {
    'use strict';
    const AD_KEYS = [
        'adPlacements', 'playerAds', 'adSlots', 'adBreakHeartbeatParams',
        'adThrottled', 'adSafetyReason', 'importantForAds', 'adParams'
    ];
    function stripAds(obj) {
        if (!obj || typeof obj !== 'object') return obj;
        for (const k of AD_KEYS) { try { delete obj[k]; } catch (e) {} }
        if (obj.playerResponse) stripAds(obj.playerResponse);
        return obj;
    }
    let _ytInitialPlayerResponse = undefined;
    try {
        Object.defineProperty(window, 'ytInitialPlayerResponse', {
            configurable: true,
            get() { return _ytInitialPlayerResponse; },
            set(v) { _ytInitialPlayerResponse = stripAds(v); }
        });
    } catch (e) {}
    const origFetch = window.fetch;
    if (origFetch) {
        window.fetch = function(input, init) {
            const url = (typeof input === 'string') ? input
                       : (input && input.url) ? input.url : '';
            const p = origFetch.apply(this, arguments);
            if (url.indexOf('/youtubei/v1/player') === -1) return p;
            return p.then(resp => resp.clone().json().then(data => {
                stripAds(data);
                return new Response(JSON.stringify(data), {
                    status: resp.status, statusText: resp.statusText,
                    headers: resp.headers
                });
            }).catch(() => resp));
        };
    }
    const skipObserver = new MutationObserver(() => {
        const skip = document.querySelector('.ytp-ad-skip-button, .ytp-skip-ad-button');
        if (skip) { try { skip.click(); } catch (e) {} }
        const overlay = document.querySelector('.ytp-ad-overlay-close-button');
        if (overlay) { try { overlay.click(); } catch (e) {} }
    });
    try {
        skipObserver.observe(document.documentElement, { childList: true, subtree: true });
    } catch (e) {}
})();
)JS";

// ============================================================================
//  FaviconLoader
// ============================================================================
class FaviconLoader : public QObject {
public:
    static FaviconLoader& instance() { static FaviconLoader f; return f; }

    QPixmap get(const QString& pageUrl, std::function<void(const QPixmap&)> ready = {}) {
        const QString key = cacheKey(pageUrl);
        if (key.isEmpty()) return QPixmap();
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            if (ready && !it->isNull()) ready(*it);
            return *it;
        }
        if (pending_.contains(key)) {
            if (ready) callbacks_[key].append(ready);
            return QPixmap();
        }
        pending_.insert(key);
        callbacks_[key].clear();
        if (ready) callbacks_[key].append(ready);
        if (loadFromDisk(key)) return QPixmap();
        fetch(key, 1);
        return QPixmap();
    }

private:
    FaviconLoader() { nam_ = new QNetworkAccessManager(this); }

    static QString cacheKey(const QString& pageUrl) {
        QUrl u = QUrl::fromUserInput(pageUrl);
        QString host = u.host().toLower();
        if (host.startsWith("www.")) host = host.mid(4);
        return host;
    }
    static QString diskPath(const QString& key) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/favicons";
        QDir().mkpath(dir);
        return dir + "/" + key + ".png";
    }
    bool loadFromDisk(const QString& key) {
        const QString path = diskPath(key);
        if (!QFile::exists(path)) return false;
        QPixmap pm;
        if (!pm.load(path) || pm.isNull()) return false;
        cache_.insert(key, pm);
        pending_.remove(key);
        notify(key, pm);
        return true;
    }
    void saveToDisk(const QString& key, const QPixmap& pm) { pm.save(diskPath(key), "PNG"); }

    QString sourceUrl(int n, const QString& key) const {
        switch (n) {
        case 1: return QString("https://%1/favicon.ico").arg(key);
        case 2: return QString("https://icons.duckduckgo.com/ip3/%1.ico").arg(key);
        case 3: return QString("https://www.google.com/s2/favicons?domain=%1&sz=64").arg(key);
        default: return QString();
        }
    }

    void fetch(const QString& key, int source) {
        const QString url = sourceUrl(source, key);
        if (url.isEmpty()) { pending_.remove(key); callbacks_[key].clear(); return; }
        QNetworkRequest req{QUrl(url)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (X11; Linux x86_64) RootBrowser/1.0");
        req.setTransferTimeout(6000);
        QNetworkReply* r = nam_->get(req);
        QObject::connect(r, &QNetworkReply::finished, this, [this, r, key, source]() {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) { fetch(key, source + 1); return; }
            const QByteArray data = r->readAll();
            QPixmap pm;
            if (!pm.loadFromData(data) || pm.isNull() || pm.width() < 8) {
                fetch(key, source + 1); return;
            }
            QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
            bool hasContent = false;
            for (int y = 0; y < img.height() && !hasContent; y += 2)
                for (int x = 0; x < img.width(); x += 2)
                    if (qAlpha(img.pixel(x, y)) > 30) { hasContent = true; break; }
            if (!hasContent) { fetch(key, source + 1); return; }
            if (pm.width() < 32)
                pm = pm.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            cache_.insert(key, pm);
            saveToDisk(key, pm);
            pending_.remove(key);
            notify(key, pm);
        });
    }
    void notify(const QString& key, const QPixmap& pm) {
        if (!callbacks_.contains(key)) return;
        for (auto& cb : callbacks_[key]) if (cb) cb(pm);
        callbacks_[key].clear();
    }
    QNetworkAccessManager* nam_ = nullptr;
    QHash<QString, QPixmap> cache_;
    QHash<QString, QList<std::function<void(const QPixmap&)>>> callbacks_;
    QSet<QString> pending_;
};

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------
static QColor lerpColor(const QColor& a, const QColor& b, qreal t) {
    t = std::clamp<qreal>(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

struct Engine { const char* name; const char* url; };
static const Engine kEngines[] = {
    {"Google",     "https://www.google.com/search?q=%1"},
    {"DuckDuckGo", "https://duckduckgo.com/?q=%1"},
    {"Bing",       "https://www.bing.com/search?q=%1"},
    {"Brave",      "https://search.brave.com/search?q=%1"},
};
static const int kEngineCount = 4;
static int g_engine = 0;

static QString normalizeInput(QString s) {
    s = s.trimmed();
    if (s.isEmpty()) return QString();

    if (s.startsWith("rootbrowser://", Qt::CaseInsensitive)
        || s.startsWith("rootbrowser:", Qt::CaseInsensitive)
        || s.startsWith("rootbrowser-goto:", Qt::CaseInsensitive)
        || s.startsWith("rootbrowser-bookmark-del:", Qt::CaseInsensitive)
        || s.startsWith("rootbrowser-dl-", Qt::CaseInsensitive))
        return s;

    if (s.contains("://") || s.startsWith("about:") || s.startsWith("file:")) return s;
    const bool hasSpace = s.contains(' ');
    if (!hasSpace && (s.startsWith("localhost") || s.startsWith("127.0.0.1")))
        return "http://" + s;
    if (!hasSpace && s.contains('.'))
        return "https://" + s;
    return QString(kEngines[g_engine].url)
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(s)));
}

static QString titleFor(const QString& u) {
    if (u.isEmpty()) return "New Tab";
    QUrl q(u);
    QUrlQuery qq(q);
    const QString term = qq.queryItemValue("q", QUrl::FullyDecoded);
    if (!term.isEmpty()) return term;
    QString h = q.host();
    if (h.startsWith("www.")) h = h.mid(4);
    return h.isEmpty() ? u : h;
}

// ----------------------------------------------------------------------------
//  Toolbar icons
// ----------------------------------------------------------------------------
enum class Ic { Back, Forward, Reload, Home, Menu, Plus, Close, Search, Full, Exit };

static void drawIcon(QPainter& p, Ic k, const QRectF& r, const QColor& col) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = r.width();
    auto P = [&](qreal x, qreal y) { return QPointF(r.x() + x * s, r.y() + y * s); };
    p.setPen(QPen(col, std::max<qreal>(1.5, s * 0.085),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (k) {
    case Ic::Back: {
        p.drawLine(P(.82, .5), P(.2, .5));
        QPointF pts[3] = {P(.48, .24), P(.2, .5), P(.48, .76)};
        p.drawPolyline(pts, 3);
        break;
    }
    case Ic::Forward: {
        p.drawLine(P(.18, .5), P(.8, .5));
        QPointF pts[3] = {P(.52, .24), P(.8, .5), P(.52, .76)};
        p.drawPolyline(pts, 3);
        break;
    }
    case Ic::Reload: {
        QRectF a(r.x() + .2 * s, r.y() + .2 * s, .6 * s, .6 * s);
        p.drawArc(a, 110 * 16, 290 * 16);
        const qreal th = 40.0 * kPi / 180.0, rad = .3 * s;
        const QPointF c = P(.5, .5);
        const QPointF e(c.x() + rad * std::cos(th), c.y() - rad * std::sin(th));
        const QPointF t(-std::sin(th), -std::cos(th));
        const QPointF tip(e.x() + t.x() * .08 * s, e.y() + t.y() * .08 * s);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const qreal ph = sgn * 40.0 * kPi / 180.0;
            const QPointF b(-t.x(), -t.y());
            const QPointF w(b.x() * std::cos(ph) - b.y() * std::sin(ph),
                            b.x() * std::sin(ph) + b.y() * std::cos(ph));
            p.drawLine(tip, QPointF(tip.x() + w.x() * .22 * s, tip.y() + w.y() * .22 * s));
        }
        break;
    }
    case Ic::Home: {
        QPointF roof[3] = {P(.16, .52), P(.5, .2), P(.84, .52)};
        p.drawPolyline(roof, 3);
        QPointF body[4] = {P(.27, .45), P(.27, .8), P(.73, .8), P(.73, .45)};
        p.drawPolyline(body, 4);
        break;
    }
    case Ic::Menu: {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        const qreal d = s * .075;
        p.drawEllipse(P(.5, .2), d, d);
        p.drawEllipse(P(.5, .5), d, d);
        p.drawEllipse(P(.5, .8), d, d);
        break;
    }
    case Ic::Plus:
        p.drawLine(P(.5, .22), P(.5, .78));
        p.drawLine(P(.22, .5), P(.78, .5));
        break;
    case Ic::Close:
        p.drawLine(P(.28, .28), P(.72, .72));
        p.drawLine(P(.72, .28), P(.28, .72));
        break;
    case Ic::Search:
        p.drawEllipse(P(.45, .45), .24 * s, .24 * s);
        p.drawLine(P(.63, .63), P(.82, .82));
        break;
    case Ic::Full: {
        QPointF a[3] = {P(.2, .38), P(.2, .2), P(.38, .2)};
        QPointF b[3] = {P(.62, .2), P(.8, .2), P(.8, .38)};
        QPointF c[3] = {P(.2, .62), P(.2, .8), P(.38, .8)};
        QPointF d[3] = {P(.62, .8), P(.8, .8), P(.8, .62)};
        p.drawPolyline(a, 3); p.drawPolyline(b, 3); p.drawPolyline(c, 3); p.drawPolyline(d, 3);
        break;
    }
    case Ic::Exit: {
        QPointF a[3] = {P(.38, .2), P(.38, .38), P(.2, .38)};
        QPointF b[3] = {P(.62, .2), P(.62, .38), P(.8, .38)};
        QPointF c[3] = {P(.38, .8), P(.38, .62), P(.2, .62)};
        QPointF d[3] = {P(.62, .8), P(.62, .62), P(.8, .62)};
        p.drawPolyline(a, 3); p.drawPolyline(b, 3); p.drawPolyline(c, 3); p.drawPolyline(d, 3);
        break;
    }
    }
    p.restore();
}

// ----------------------------------------------------------------------------
//  Small building blocks
// ----------------------------------------------------------------------------
class IconButton : public QAbstractButton {
public:
    explicit IconButton(Ic k, QWidget* parent = nullptr, int box = 36)
        : QAbstractButton(parent), kind_(k), box_(box) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(box, box);
    }
    void setKind(Ic k) { kind_ = k; update(); }
protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::HoverEnter) { hover_ = true; update(); }
        else if (e->type() == QEvent::HoverLeave) { hover_ = false; update(); }
        return QAbstractButton::event(e);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool en = isEnabled();

        if (en && (hover_ || isDown())) {
            // Purple-blue glow
            QRadialGradient glow(rect().center(), width() * 0.75);
            glow.setColorAt(0.0, QColor(100, 130, 200, 70));
            glow.setColorAt(1.0, QColor(100, 130, 200, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(rect().adjusted(-4, -4, 4, 4));

            // Pill background
            p.setPen(QPen(QColor(120, 150, 220, 60), 1));
            p.setBrush(QColor(60, 80, 140, isDown() ? 100 : 55));
            p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 9, 9);
        }

        QColor c = !en ? QColor(255, 255, 255, 48)
                       : (hover_ ? QColor("#e8eeff") : QColor("#a8afc4"));
        const qreal ic = box_ * 0.55;
        drawIcon(p, kind_, QRectF((width() - ic) / 2.0, (height() - ic) / 2.0, ic, ic), c);
    }
private:
    Ic kind_;
    int box_;
    bool hover_ = false;
};

class Panel : public QWidget {
public:
    Panel(const QColor& c, bool bottomLine, QWidget* parent = nullptr)
        : QWidget(parent), c_(c), line_(bottomLine) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Semi-transparent background — lets wallpaper hint through
        QColor bg = c_;
        bg.setAlpha(235);
        p.fillRect(rect(), bg);

        // Subtle gradient tint (purple-blue for night theme)
        QLinearGradient tint(0, 0, width(), 0);
        tint.setColorAt(0.0, QColor(80, 100, 180, 8));
        tint.setColorAt(0.5, QColor(100, 80, 160, 6));
        tint.setColorAt(1.0, QColor(60, 90, 160, 10));
        p.fillRect(rect(), tint);

        // Top highlight line
        p.setPen(QPen(QColor(255, 255, 255, 10), 1));
        p.drawLine(0, 0, width(), 0);

        // Bottom subtle border
        if (line_) {
            QLinearGradient border(0, 0, width(), 0);
            border.setColorAt(0.0, QColor(60, 70, 100, 0));
            border.setColorAt(0.5, QColor(60, 70, 100, 120));
            border.setColorAt(1.0, QColor(60, 70, 100, 0));
            p.setPen(QPen(QBrush(border), 1));
            p.drawLine(0, height() - 1, width(), height() - 1);
        }
    }
private:
    QColor c_;
    bool line_;
};

class AddressEdit : public QLineEdit {
public:
    using QLineEdit::QLineEdit;
protected:
    void focusInEvent(QFocusEvent* e) override {
        QLineEdit::focusInEvent(e);
        if (e->reason() == Qt::MouseFocusReason)
            QTimer::singleShot(0, this, [this] { selectAll(); });
        else
            selectAll();
    }
};

// ----------------------------------------------------------------------------
//  Tab strip
// ----------------------------------------------------------------------------
class TabStrip : public QWidget {
public:
    std::function<void(int)> onCurrent, onClose;
    std::function<void()> onNew, onMin, onMaxToggle, onCloseWin, onDrag;

    explicit TabStrip(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(46);
        setMouseTracking(true);
    }
    int addTab(const QString& t) {
        titles_.push_back(t);
        icons_.push_back(QPixmap());
        privates_.push_back(false);
        update();
        return int(titles_.size()) - 1;
    }

    void setPrivate(int index, bool priv) {
        if (index < 0) return;
        while (privates_.size() <= index) privates_.push_back(false);
        privates_[index] = priv;
        update();
    }
    void removeTab(int i) {
        if (i < 0 || i >= titles_.size()) return;
        titles_.removeAt(i);
        icons_.removeAt(i);
        if (i < privates_.size()) privates_.removeAt(i);
        if (cur_ >= titles_.size()) cur_ = int(titles_.size()) - 1;
        hover_ = -1; hoverClose_ = false;
        update();
    }
    void setTitle(int i, const QString& t) { if (i >= 0 && i < titles_.size()) { titles_[i] = t; update(); } }
    void setIcon(int i, const QPixmap& pm) { if (i >= 0 && i < icons_.size()) { icons_[i] = pm; update(); } }
    void setNote(const QString& n) { note_ = n; update(); }
    void setMaximized(bool m) { maximized_ = m; update(); }
    void setCurrent(int i) { cur_ = i; update(); }
    int count() const { return int(titles_.size()); }

protected:
    static constexpr int kCtl = 124;

    qreal tabW() const {
        const int n = std::max<int>(1, int(titles_.size()));
        return std::clamp<qreal>((width() - 12 - 50 - kCtl - 30) / n, 84.0, 240.0);
    }
    QRectF tabRect(int i) const { return QRectF(10 + i * tabW(), 9, tabW() - 2, height() - 9); }
    QRectF closeRect(int i) const {
        QRectF r = tabRect(i);
        return QRectF(r.right() - 28, r.center().y() - 10, 20, 20);
    }
    bool closeVisible(int i) const { return i == cur_ || tabW() >= 112; }
    QRectF plusRect() const {
        qreal x = 10 + titles_.size() * tabW() + 4;
        x = std::min<qreal>(x, width() - kCtl - 40);
        return QRectF(x, 12, 30, 30);
    }
    QRectF ctlRect(int k) const {
        const qreal right = width() - 8 - (2 - k) * 38;
        return QRectF(right - 36, 8, 36, 30);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(10, 12, 18, 240));
        // Purple tint
        QLinearGradient tt(0, 0, 0, height());
        tt.setColorAt(0.0, QColor(60, 70, 130, 12));
        tt.setColorAt(1.0, QColor(40, 50, 100, 4));
        p.fillRect(rect(), tt);
        QFont f = font();
        f.setPixelSize(13);
        p.setFont(f);
        const QFontMetrics fm(f);

        for (int i = 0; i < titles_.size(); ++i) {
            const QRectF r = tabRect(i);
            const bool act = (i == cur_);
            p.setPen(Qt::NoPen);
            if (act) {
                QLinearGradient tg(r.topLeft(), r.bottomLeft());
                tg.setColorAt(0.0, QColor(40, 44, 58, 240));
                tg.setColorAt(1.0, QColor(26, 30, 42, 250));
                p.setPen(QPen(QColor(120, 150, 220, 90), 1));
                p.setBrush(tg);
                p.drawRoundedRect(r, 10, 10);
                // Purple-blue accent line at top
                p.setPen(QPen(QColor(120, 160, 240, 220), 2.0,
                              Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(r.left() + 14, r.top() + 1.2),
                           QPointF(r.right() - 14, r.top() + 1.2));
            } else if (i == hover_) {
                p.setBrush(QColor(255, 255, 255, 14));
                p.drawRoundedRect(r.adjusted(0, 3, 0, -6), 10, 10);
            }

            const QRectF fav(r.x() + 13, r.center().y() - 8, 17, 17);
            if (!icons_[i].isNull()) {
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawPixmap(fav.toRect(), icons_[i]);
            } else {
                QLinearGradient g(fav.topLeft(), fav.bottomRight());
                g.setColorAt(0, QColor("#99a1b3"));
                g.setColorAt(1, QColor("#4f5565"));
                p.setBrush(g);
                p.drawEllipse(fav);
                QFont ff = f; ff.setPixelSize(10); ff.setBold(true);
                p.setFont(ff);
                p.setPen(QColor("#0c0c0e"));
                p.drawText(fav, Qt::AlignCenter, titles_[i].left(1).toUpper());
                p.setFont(f);
            }

            const bool showClose = closeVisible(i);
            const qreal tx = fav.right() + 9;
            const qreal tw = r.right() - tx - (showClose ? 32 : 12);
            p.setPen(act ? QColor("#f1f2f5") : QColor("#9aa0ac"));
            p.drawText(QRectF(tx, r.y(), tw, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(titles_[i], Qt::ElideRight, int(tw)));

            if (showClose) {
                const QRectF cr = closeRect(i);
                if (i == hover_ && hoverClose_) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(255, 255, 255, 30));
                    p.drawEllipse(cr);
                }
                drawIcon(p, Ic::Close, cr.adjusted(4, 4, -4, -4),
                         (i == hover_ && hoverClose_) ? QColor("#ffffff") : QColor("#8d929e"));
            }
        }

        const QRectF pr = plusRect();
        if (hoverPlus_) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 22));
            p.drawRoundedRect(pr, 10, 10);
        }
        drawIcon(p, Ic::Plus, pr.adjusted(7, 7, -7, -7),
                 hoverPlus_ ? QColor("#ffffff") : QColor("#9aa0ac"));

        for (int k = 0; k < 3; ++k) {
            const QRectF cr = ctlRect(k);
            const bool h = (hoverCtl_ == k);
            if (h) {
                p.setPen(Qt::NoPen);
                p.setBrush(k == 2 ? QColor(226, 70, 70) : QColor(255, 255, 255, 24));
                p.drawRoundedRect(cr, 9, 9);
            }
            p.setPen(QPen(h ? QColor("#ffffff") : QColor("#9aa0ac"), 1.4,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            const QPointF c = cr.center();
            if (k == 0) {
                p.drawLine(c + QPointF(-5, 0), c + QPointF(5, 0));
            } else if (k == 1) {
                if (maximized_) {
                    p.drawRoundedRect(QRectF(c.x() - 5, c.y() - 2, 7, 7), 1.5, 1.5);
                    p.drawLine(QPointF(c.x() - 2, c.y() - 5), QPointF(c.x() + 5, c.y() - 5));
                    p.drawLine(QPointF(c.x() + 5, c.y() - 5), QPointF(c.x() + 5, c.y() + 2));
                } else {
                    p.drawRoundedRect(QRectF(c.x() - 5, c.y() - 5, 10, 10), 2, 2);
                }
            } else {
                p.drawLine(c + QPointF(-4.5, -4.5), c + QPointF(4.5, 4.5));
                p.drawLine(c + QPointF(4.5, -4.5), c + QPointF(-4.5, 4.5));
            }
        }

        if (!note_.isEmpty()) {
            const qreal w = fm.horizontalAdvance(note_) + 30;
            const QRectF nr(width() - kCtl - w - 6, 10, w, 30);
            p.setPen(QPen(QColor(255, 255, 255, 24), 1));
            p.setBrush(QColor("#1f2025"));
            p.drawRoundedRect(nr, 15, 15);
            p.setPen(QColor("#d4d8e1"));
            p.drawText(nr, Qt::AlignCenter, note_);
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        const QPointF pos = e->position();
        int h = -1; bool hc = false;
        for (int i = 0; i < titles_.size(); ++i) {
            if (tabRect(i).contains(pos)) {
                h = i;
                hc = closeVisible(i) && closeRect(i).contains(pos);
                break;
            }
        }
        const bool hp = plusRect().contains(pos);
        int ctl = -1;
        for (int k = 0; k < 3; ++k) if (ctlRect(k).contains(pos)) ctl = k;
        if (h != hover_ || hc != hoverClose_ || hp != hoverPlus_ || ctl != hoverCtl_) {
            hover_ = h; hoverClose_ = hc; hoverPlus_ = hp; hoverCtl_ = ctl;
            setCursor((h >= 0 || hp || ctl >= 0) ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent*) override {
        hover_ = -1; hoverClose_ = false; hoverPlus_ = false; hoverCtl_ = -1;
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        const QPointF pos = e->position();
        if (e->button() == Qt::MiddleButton) {
            for (int i = 0; i < titles_.size(); ++i)
                if (tabRect(i).contains(pos)) { if (onClose) onClose(i); return; }
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        for (int k = 0; k < 3; ++k) {
            if (ctlRect(k).contains(pos)) {
                if (k == 0 && onMin) onMin();
                else if (k == 1 && onMaxToggle) onMaxToggle();
                else if (k == 2 && onCloseWin) onCloseWin();
                return;
            }
        }
        for (int i = 0; i < titles_.size(); ++i) {
            if (tabRect(i).contains(pos)) {
                if (closeVisible(i) && closeRect(i).contains(pos)) {
                    if (onClose) onClose(i);
                } else if (i != cur_) {
                    cur_ = i; update();
                    if (onCurrent) onCurrent(i);
                }
                return;
            }
        }
        if (plusRect().contains(pos)) { if (onNew) onNew(); return; }
        if (onDrag) onDrag();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        const QPointF pos = e->position();
        for (int i = 0; i < titles_.size(); ++i) if (tabRect(i).contains(pos)) return;
        for (int k = 0; k < 3; ++k) if (ctlRect(k).contains(pos)) return;
        if (!plusRect().contains(pos) && onMaxToggle) onMaxToggle();
    }

private:
    QStringList titles_;
    QVector<bool> privates_;
    QVector<QPixmap> icons_;
    QString note_;
    int cur_ = 0, hover_ = -1, hoverCtl_ = -1;
    bool hoverClose_ = false, hoverPlus_ = false, maximized_ = false;
};

// ----------------------------------------------------------------------------
//  Home page widgets
// ----------------------------------------------------------------------------
class Tile : public QWidget {
public:
    std::function<void(const QString&)> onClick;

    Tile(const QString& label, const QString& url, const QColor& accent, QWidget* parent = nullptr)
        : QWidget(parent), label_(label), url_(url), accent_(accent) {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setMinimumSize(118, 108);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        anim_ = new QVariantAnimation(this);
        anim_->setDuration(150);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            hov_ = v.toReal(); update();
        });
        FaviconLoader::instance().get(url_, [this](const QPixmap& pm) {
            icon_ = pm;
            update();
        });
    }

protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::HoverEnter) animateTo(1.0);
        else if (e->type() == QEvent::HoverLeave) animateTo(0.0);
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClick)
            onClick(url_);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r = QRectF(rect()).adjusted(2, 5, -2, -2);
        r.translate(0, -5.0 * hov_);

        p.setPen(QPen(QColor(255, 255, 255, int(20 + 40 * hov_)), 1));
        QColor baseTop(24, 28, 38, 180);
        QColor baseBot(16, 20, 28, 200);
        QColor hoverTop(32, 38, 52, 200);
        QColor hoverBot(22, 28, 38, 220);
        QLinearGradient tileG(r.topLeft(), r.bottomRight());
        tileG.setColorAt(0, lerpColor(baseTop, hoverTop, hov_));
        tileG.setColorAt(1, lerpColor(baseBot, hoverBot, hov_));
        p.setBrush(tileG);
        p.drawRoundedRect(r, 18, 18);

        const QRectF b(r.center().x() - 22, r.y() + 15, 44, 44);

        if (!icon_.isNull()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, int(8 + 10 * hov_)));
            p.drawRoundedRect(b, 12, 12);
            QPixmap scaled = icon_.scaled(int(b.width()) - 12, int(b.height()) - 12,
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            const QRectF target(b.center().x() - scaled.width() / 2.0,
                                b.center().y() - scaled.height() / 2.0,
                                scaled.width(), scaled.height());
            p.drawPixmap(target, scaled, QRectF(scaled.rect()));
        } else {
            QLinearGradient g(b.topLeft(), b.bottomRight());
            g.setColorAt(0, accent_.lighter(112));
            g.setColorAt(1, accent_.darker(160));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawRoundedRect(b, 13, 13);
            QFont f = font();
            f.setPixelSize(19); f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 235));
            p.drawText(b, Qt::AlignCenter, label_.left(1).toUpper());
        }

        QFont f = font();
        f.setPixelSize(13); f.setBold(false);
        p.setFont(f);
        p.setPen(lerpColor(QColor("#aeb3be"), QColor("#ffffff"), hov_));
        p.drawText(QRectF(r.x(), b.bottom() + 9, r.width(), 22), Qt::AlignCenter, label_);
    }

private:
    void animateTo(qreal v) {
        anim_->stop();
        anim_->setStartValue(hov_);
        anim_->setEndValue(v);
        anim_->start();
    }
    QString label_, url_;
    QColor accent_;
    QPixmap icon_;
    qreal hov_ = 0.0;
    QVariantAnimation* anim_;
};

class SearchBox : public QWidget {
public:
    std::function<void(const QString&)> onSubmit;
    std::function<void()> onEngine;

    explicit SearchBox(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(68);
        auto* l = new QHBoxLayout(this);
        l->setContentsMargins(24, 0, 20, 0);
        l->setSpacing(10);

        eng_ = new QPushButton(this);
        eng_->setCursor(Qt::PointingHandCursor);
        eng_->setFocusPolicy(Qt::NoFocus);
        eng_->setFixedHeight(30);
        eng_->setToolTip("Change search engine");
        eng_->setStyleSheet(
            "QPushButton{background:rgba(255,255,255,0.06);border:none;border-radius:15px;"
            "padding:0 14px;color:#c5c9d2;font-size:12px;}"
            "QPushButton:hover{background:rgba(255,255,255,0.12);color:#ffffff;}");
        refreshEngine();

        edit_ = new QLineEdit(this);
        edit_->setPlaceholderText("Search the web or type a URL");
        edit_->setStyleSheet("QLineEdit{background:transparent;border:none;}");
        QFont f = edit_->font();
        f.setPixelSize(17);
        edit_->setFont(f);
        QPalette pal = edit_->palette();
        pal.setColor(QPalette::Text, QColor("#f2f3f6"));
        pal.setColor(QPalette::PlaceholderText, QColor(160, 166, 182, 140));
        pal.setColor(QPalette::Highlight, QColor("#59627a"));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        edit_->setPalette(pal);
        edit_->installEventFilter(this);

        go_ = new IconButton(Ic::Search, this, 40);

        l->addWidget(eng_);
        l->addWidget(edit_, 1);
        l->addWidget(go_);

        QObject::connect(edit_, &QLineEdit::returnPressed, this, [this] { submit(); });
        QObject::connect(go_, &QAbstractButton::clicked, this, [this] { submit(); });
        QObject::connect(eng_, &QAbstractButton::clicked, this, [this] {
            g_engine = (g_engine + 1) % kEngineCount;
            refreshEngine();
            if (onEngine) onEngine();
        });

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(180);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            glow_ = v.toReal(); update();
        });
    }

    void refreshEngine() { eng_->setText(kEngines[g_engine].name); }
    void clear() { edit_->clear(); }
    void focusEdit() { edit_->setFocus(); }

    QLineEdit* lineEdit() const { return edit_; }

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == edit_) {
            if (e->type() == QEvent::FocusIn) animateTo(1.0);
            else if (e->type() == QEvent::FocusOut) animateTo(0.0);
        }
        return QWidget::eventFilter(o, e);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(8, 8, -8, -8);
        const qreal rad = r.height() / 2.0;
        p.setBrush(Qt::NoBrush);
        if (glow_ > 0.01) {
            for (int i = 4; i >= 1; --i) {
                p.setPen(QPen(QColor(120, 160, 240, int(30.0 * glow_ / i)), i * 2.5));
                p.drawRoundedRect(r, rad, rad);
            }
        }
        p.setPen(QPen(lerpColor(QColor("#4a4e5a"), QColor("#8a94ac"), glow_), 1.2));
        p.setBrush(lerpColor(QColor(20, 24, 32, 220), QColor(15, 18, 25, 240), glow_));
        p.drawRoundedRect(r, rad, rad);
    }

private:
    void animateTo(qreal v) {
        anim_->stop();
        anim_->setStartValue(glow_);
        anim_->setEndValue(v);
        anim_->start();
    }
    void submit() {
        const QString t = edit_->text().trimmed();
        if (!t.isEmpty() && onSubmit) onSubmit(t);
    }
    QPushButton* eng_;
    QLineEdit* edit_;
    IconButton* go_;
    QVariantAnimation* anim_;
    qreal glow_ = 0.0;
};

class HomePage : public QWidget {
public:
    std::function<void(const QString&)> onNavigate;
    std::function<void(const QPoint&)> onContextMenu;

    explicit HomePage(QWidget* parent = nullptr) : QWidget(parent) {
        clk_.start();

        clock_ = new QLabel(this);
        clock_->setAlignment(Qt::AlignCenter);
        setLabelColor(clock_, QColor("#f4f5f7"));

        // Subtle drop shadow for clock (better legibility on any wallpaper)
        {
            auto* shadow = new QGraphicsDropShadowEffect(clock_);
            shadow->setBlurRadius(24);
            shadow->setOffset(0, 2);
            shadow->setColor(QColor(0, 0, 0, 120));
            clock_->setGraphicsEffect(shadow);
        }


        greet_ = new QLabel(this);
        greet_->setAlignment(Qt::AlignCenter);
        setLabelColor(greet_, QColor("#8b909d"));

        // Greeting shadow
        {
            auto* shadow = new QGraphicsDropShadowEffect(greet_);
            shadow->setBlurRadius(16);
            shadow->setOffset(0, 1);
            shadow->setColor(QColor(0, 0, 0, 140));
            greet_->setGraphicsEffect(shadow);
        }


        hint_ = new QLabel("Ctrl+T  New tab      Ctrl+L  Address bar      F11  Full screen", this);
        hint_->setAlignment(Qt::AlignCenter);
        setLabelColor(hint_, QColor("#5b5f6a"));
        QFont hf = hint_->font(); hf.setPixelSize(12); hint_->setFont(hf);

        search_ = new SearchBox(this);
        search_->setMaximumWidth(700);
        search_->onSubmit = [this](const QString& t) { if (onNavigate) onNavigate(t); };

        // ── URL autocomplete for home search bar
        homeAutocomplete_ = new UrlAutocomplete(this);
        homeAutocomplete_->setTheme(Theme::HomePage);
        homeAutocomplete_->attach(search_->lineEdit());

        // Change submit behavior — use autocomplete when visible
        search_->onSubmit = [this](const QString& t) {
            if (homeAutocomplete_ && homeAutocomplete_->isVisible()) {
                const QString sel = homeAutocomplete_->selectedUrl();
                if (!sel.isEmpty()) {
                    homeAutocomplete_->hideDropdown();
                    if (onNavigate) onNavigate(sel);
                    return;
                }
            }
            if (onNavigate) onNavigate(t);
        };

        auto* grid = new QWidget(this);
        grid->setMaximumWidth(800);
        auto* g = new QGridLayout(grid);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(14);
        g->setVerticalSpacing(14);
        struct T { const char* n; const char* u; const char* c; };
        static const T tiles[] = {
            {"GitHub",         "https://github.com",               "#6e7681"},
            {"YouTube",        "https://www.youtube.com",          "#e0443b"},
            {"Wikipedia",      "https://www.wikipedia.org",        "#8e949f"},
            {"Reddit",         "https://www.reddit.com",           "#e8602c"},
            {"Stack Overflow", "https://stackoverflow.com",        "#e07a1f"},
            {"Hacker News",    "https://news.ycombinator.com",     "#ee7418"},
            {"Gmail",          "https://mail.google.com",          "#d9483b"},
            {"Maps",           "https://maps.google.com",          "#2f9e5b"},
        };
        for (int i = 0; i < 8; ++i) {
            auto* t = new Tile(tiles[i].n, tiles[i].u, QColor(tiles[i].c), grid);
            t->onClick = [this](const QString& u) { if (onNavigate) onNavigate(u); };
            g->addWidget(t, i / 4, i % 4);
        }

        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(24, 16, 24, 14);
        v->setSpacing(0);
        v->addStretch(3);
        v->addWidget(clock_);
        v->addWidget(greet_);
        v->addSpacing(34);
        auto* sr = new QHBoxLayout;
        sr->addStretch(1); sr->addWidget(search_, 100); sr->addStretch(1);
        v->addLayout(sr);
        v->addSpacing(34);
        auto* gr = new QHBoxLayout;
        gr->addStretch(1); gr->addWidget(grid, 100); gr->addStretch(1);
        v->addLayout(gr);
        v->addStretch(4);
        v->addWidget(hint_);

        tickClock();
        updateFonts();

        tick_ = new QTimer(this);
        QObject::connect(tick_, &QTimer::timeout, this, [this] { tickClock(); });
        anim_ = new QTimer(this);
        anim_->setInterval(40);
        QObject::connect(anim_, &QTimer::timeout, this, [this] { update(); });
    }

    void clearSearch() { search_->clear(); }

    void refreshBackground() {
        bg_ = QPixmap();  // force re-render
        update();
    }
    void focusSearch() { search_->focusEdit(); }
    void hideAutocomplete() { if (homeAutocomplete_) homeAutocomplete_->hideDropdown(); }

protected:
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        tick_->start(1000); anim_->start(); tickClock();
    }
    void hideEvent(QHideEvent* e) override {
        QWidget::hideEvent(e);
        tick_->stop(); anim_->stop();
    }
    void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e); updateFonts(); }

    void contextMenuEvent(QContextMenuEvent* ev) override {
        if (onContextMenu) {
            onContextMenu(ev->pos());
            ev->accept();
        } else {
            QWidget::contextMenuEvent(ev);
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const QSize targetSize = size();
        if (bg_.size() != targetSize) {
            bg_ = AppBackground::render(targetSize.width(), targetSize.height());
        }
        if (!bg_.isNull()) {
            p.drawPixmap(0, 0, bg_);
        }
    }

private:
    static void setLabelColor(QLabel* l, const QColor& c) {
        QPalette p = l->palette();
        p.setColor(QPalette::WindowText, c);
        l->setPalette(p);
    }
    void updateFonts() {
        QFont f = clock_->font();
        f.setPixelSize(std::clamp(int(height() * 0.12), 52, 120));
        f.setWeight(QFont::Normal);
        clock_->setFont(f);
        QFont g = greet_->font();
        g.setPixelSize(std::clamp(int(height() * 0.026), 14, 19));
        greet_->setFont(g);
    }
    void tickClock() {
        const QTime t = QTime::currentTime();
        clock_->setText(t.toString("HH:mm"));
        const int h = t.hour();
        const char* g = h < 5 ? "Good night" : h < 12 ? "Good morning"
                                  : h < 17 ? "Good afternoon" : "Good evening";
        greet_->setText(QString("%1   \u00B7   %2")
                            .arg(QLocale().toString(QDate::currentDate(), "dddd, d MMMM"))
                            .arg(g));
    }

    QLabel *clock_, *greet_, *hint_;
    SearchBox* search_;
    UrlAutocomplete* homeAutocomplete_ = nullptr;
    QTimer *tick_, *anim_;
    QElapsedTimer clk_;
    QPixmap bg_;
};

// ----------------------------------------------------------------------------
//  WebView
// ----------------------------------------------------------------------------
class WebView : public QWebEngineView {
public:
    std::function<void(const QPoint&)> onContextMenu;

    explicit WebView(QWidget* parent = nullptr) : QWebEngineView(parent) {
        installEventFilter(this);
        QTimer::singleShot(0, this, [this]{ installOnChildren(); });
        QTimer::singleShot(500, this, [this]{ installOnChildren(); });
    }

protected:
    void installOnChildren() {
        for (auto* w : findChildren<QWidget*>()) {
            if (!w->property("ctxFiltered").toBool()) {
                w->installEventFilter(this);
                w->setProperty("ctxFiltered", true);
            }
        }
    }

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::ContextMenu) {
            installOnChildren();
            auto* ce = static_cast<QContextMenuEvent*>(ev);
            const QPoint local = mapFromGlobal(ce->globalPos());
            if (onContextMenu) {
                onContextMenu(local);
                return true;
            }
        } else if (ev->type() == QEvent::ChildAdded) {
            if (auto* child = static_cast<QChildEvent*>(ev)->child()) {
                if (auto* w = qobject_cast<QWidget*>(child)) {
                    w->installEventFilter(this);
                    w->setProperty("ctxFiltered", true);
                    for (auto* sub : w->findChildren<QWidget*>()) {
                        sub->installEventFilter(this);
                        sub->setProperty("ctxFiltered", true);
                    }
                }
            }
        }
        return QWebEngineView::eventFilter(obj, ev);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        if (onContextMenu) {
            onContextMenu(event->pos());
            event->accept();
        } else {
            QWebEngineView::contextMenuEvent(event);
        }
    }
};

// ----------------------------------------------------------------------------
//  WebPage
// ----------------------------------------------------------------------------
class WebPage : public QWebEnginePage {
public:
    std::function<QWebEnginePage*()> onNewWindow;
    std::function<bool(const QUrl&)> onSpecialScheme;

    WebPage(QWebEngineProfile* prof, QObject* parent)
        : QWebEnginePage(prof, parent) {}

protected:
    QWebEnginePage* createWindow(WebWindowType) override {
        return onNewWindow ? onNewWindow() : nullptr;
    }

    bool acceptNavigationRequest(const QUrl& url,
                                 NavigationType type,
                                 bool isMainFrame) override
    {
        if (isMainFrame && onSpecialScheme && onSpecialScheme(url))
            return false;

        // HTTPS-Only mode: auto-upgrade http:// → https://
        if (isMainFrame && type != QWebEnginePage::NavigationTypeTyped) {
            QUrl upgraded = HttpsOnly::handleNavigation(url, this);
            if (!upgraded.isEmpty() && upgraded != url) {
                QTimer::singleShot(0, this, [this, upgraded]{
                    setUrl(upgraded);
                });
                return false;
            }
        }

        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
};

class LoadBar : public QWidget {
public:
    explicit LoadBar(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(2); }
    void setValue(int v) { v_ = v; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        if (v_ <= 0 || v_ >= 100) return;
        QPainter p(this);
        const int w = width() * v_ / 100;
        QLinearGradient g(0, 0, std::max(1, w), 0);
        g.setColorAt(0, QColor("#6b7690"));
        g.setColorAt(1, QColor("#cfd6ea"));
        p.fillRect(QRect(0, 0, w, height()), g);
    }
private:
    int v_ = 0;
};

// ----------------------------------------------------------------------------
//  Browser
// ----------------------------------------------------------------------------
// ============================================================================
//  PrivateWindow — a standalone Tor-connected incognito window.
// ============================================================================
class PrivateWindow : public QWidget {
public:
    explicit PrivateWindow(QWidget* parent = nullptr);
    ~PrivateWindow() override;

    void loadUrl(const QUrl& url);

private:
    void setupProfile();
    void updateTorStatus();

    QWebEngineProfile* profile_ = nullptr;
    QWebEngineView*    view_    = nullptr;
    QLineEdit*         addr_    = nullptr;
    QLabel*            status_  = nullptr;
    QTimer*            statusTimer_ = nullptr;
};

PrivateWindow::PrivateWindow(QWidget* parent)
    : QWidget(parent)
{
    
    // ── Route traffic through Tor (SOCKS5)
    {
        QNetworkProxy proxy;
        proxy.setType(QNetworkProxy::Socks5Proxy);
        proxy.setHostName("127.0.0.1");
        proxy.setPort(quint16(TorController::instance().socksPort()));
        QNetworkProxy::setApplicationProxy(proxy);
    }
setWindowTitle("RootBrowser — Private Mode (Tor)");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1100, 720);

    // ── Layout
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar (dark purple accent)
    auto* top = new QWidget(this);
    top->setFixedHeight(52);
    top->setStyleSheet("background:#121316;border-bottom:1px solid #1e2026;");
    auto* topL = new QHBoxLayout(top);
    topL->setContentsMargins(14, 0, 14, 0);
    topL->setSpacing(10);

    auto* icon = new QLabel(top);
    icon->setFixedSize(20, 20);
    icon->setPixmap([]() {
        QPixmap pm(40, 40);
        pm.setDevicePixelRatio(2.0);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#a882d1"), 1.8, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(8, 8, 24, 24));
        p.drawEllipse(QRectF(16, 16, 8, 8));
        p.end();
        return pm;
    }());
    topL->addWidget(icon);

    auto* label = new QLabel("Private Mode (Tor)", top);
    label->setStyleSheet("color:#a882d1;font-size:13px;font-weight:600;"
                         "letter-spacing:0.2px;");
    topL->addWidget(label);
    topL->addStretch(1);

    status_ = new QLabel(top);
    status_->setStyleSheet("color:#8a90a0;font-size:11px;");
    topL->addWidget(status_);

    root->addWidget(top);

    // ── Address bar
    auto* bar = new QWidget(this);
    bar->setFixedHeight(46);
    bar->setStyleSheet("background:#17181b;border-bottom:1px solid #1e2026;");
    auto* barL = new QHBoxLayout(bar);
    barL->setContentsMargins(14, 0, 14, 0);

    addr_ = new QLineEdit(bar);
    addr_->setPlaceholderText("Search or enter .onion address");
    addr_->setStyleSheet(
            "QLineEdit{"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "                              stop:0 rgba(30, 34, 46, 200),"
            "                              stop:1 rgba(22, 25, 35, 220));"
            "  border:1px solid rgba(80, 100, 160, 100);"
            "  border-radius:19px;"
            "  padding:0 16px 0 10px;"
            "  color:#e8ecf5;"
            "  font-size:13.5px;"
            "  selection-background-color:#5d9df1;"
            "  selection-color:#ffffff;"
            "}"
            "QLineEdit:hover{"
            "  border:1px solid rgba(100, 130, 200, 140);"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "                              stop:0 rgba(34, 38, 52, 210),"
            "                              stop:1 rgba(26, 29, 40, 230));"
            "}"
            "QLineEdit:focus{"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "                              stop:0 rgba(28, 32, 44, 220),"
            "                              stop:1 rgba(20, 23, 32, 240));"
            "  border:1px solid rgba(120, 160, 240, 200);"
            "}");
    barL->addWidget(addr_);

    root->addWidget(bar);

    // ── Web view
    setupProfile();
    view_ = new QWebEngineView(this);
    view_->setPage(new QWebEnginePage(profile_, view_));
    view_->page()->setBackgroundColor(QColor("#0a0a0c"));
    root->addWidget(view_, 1);

    // ── Connections
    connect(addr_, &QLineEdit::returnPressed, this, [this]{
        const QString t = addr_->text().trimmed();
        if (t.isEmpty()) return;
        QUrl u = QUrl::fromUserInput(t);
        if (u.scheme() == "http" || u.scheme() == "https")
            view_->load(u);
    });
    connect(view_, &QWebEngineView::urlChanged, this, [this](const QUrl& u){
        addr_->setText(u.toString());
    });
    connect(view_, &QWebEngineView::titleChanged, this, [this](const QString& t){
        setWindowTitle(t.isEmpty() ? "Private Mode (Tor)" : t + " — Private (Tor)");
    });

    // ── Status timer
    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(1000);
    connect(statusTimer_, &QTimer::timeout, this, [this]{ updateTorStatus(); });
    statusTimer_->start();
    updateTorStatus();
}

PrivateWindow::~PrivateWindow() {
    // Reset proxy so normal browsing doesn't route through Tor
    QNetworkProxy::setApplicationProxy(QNetworkProxy::DefaultProxy);
}

void PrivateWindow::setupProfile() {
    // Off-the-record profile: no persistence
    profile_ = new QWebEngineProfile(this);
    profile_->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    // NOTE: Qt 6 removed QWebEngineProfile::setProxy().
    // Proxy is set application-wide in the constructor and reset in dtor.

    // Hardened settings
    auto* st = profile_->settings();
    st->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
    st->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
    st->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    st->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    st->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
}

void PrivateWindow::updateTorStatus() {
    auto& T = TorController::instance();
    if (T.isReady()) {
        status_->setText(QStringLiteral("✓ Connected via Tor · port %1")
                         .arg(T.socksPort()));
        status_->setStyleSheet("color:#4aaf7a;font-size:11px;");
    } else if (T.state() == TorController::State::Bootstrapping
            || T.state() == TorController::State::Starting) {
        status_->setText(QStringLiteral("Connecting… %1%").arg(T.bootstrapPercent()));
        status_->setStyleSheet("color:#f1c75c;font-size:11px;");
    } else if (T.state() == TorController::State::Error) {
        status_->setText(QStringLiteral("⚠ Tor connection failed"));
        status_->setStyleSheet("color:#e0443b;font-size:11px;");
    } else {
        status_->setText(QStringLiteral("Tor not connected"));
        status_->setStyleSheet("color:#8a90a0;font-size:11px;");
    }
}

void PrivateWindow::loadUrl(const QUrl& url) {
    if (view_) view_->load(url);
}

class Browser : public QWidget, public Host {
public:
    Browser() {
        setWindowTitle(kAppName);
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setMouseTracking(true);
        setMinimumSize(780, 660);
        resize(1280, 800);

        profile_ = new QWebEngineProfile("graphite", this);
        QWebEngineSettings* st = profile_->settings();
        st->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
        st->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
        st->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);

        WebAdBlocker::attach(profile_);
        DownloadManager::instance().attach(profile_);
        installYouTubeAdNeutralizer();

        ContextMenu::setHost(this);
        ContextMenu::attach(profile_, this);

        noteTimer_ = new QTimer(this);
        noteTimer_->setSingleShot(true);
        QObject::connect(noteTimer_, &QTimer::timeout, this,
                         [this] { tabs_->setNote(QString()); });

        auto* root = new QVBoxLayout(this);
        root_ = root;
        root->setContentsMargins(kEdge, kEdge, kEdge, kEdge);
        root->setSpacing(0);

        tabs_ = new TabStrip(this);

        toolbar_ = new Panel(QColor("#14161c"), true, this);
        toolbar_->setFixedHeight(56);
        auto* tb = new QHBoxLayout(toolbar_);
        tb->setContentsMargins(12, 0, 12, 0);
        tb->setSpacing(4);

        back_    = new IconButton(Ic::Back, toolbar_);    back_->setToolTip("Back (Alt+Left)");
        fwd_     = new IconButton(Ic::Forward, toolbar_); fwd_->setToolTip("Forward (Alt+Right)");
        reload_  = new IconButton(Ic::Reload, toolbar_);  reload_->setToolTip("Reload (F5)");
        homeBtn_ = new IconButton(Ic::Home, toolbar_);    homeBtn_->setToolTip("Home (Alt+Home)");
        full_    = new IconButton(Ic::Full, toolbar_);    full_->setToolTip("Full screen (F11)");
        menuBtn_ = new IconButton(Ic::Menu, toolbar_);    menuBtn_->setToolTip("Menu");

        // Download button (hidden until first download starts)
        dlBtn_ = new DownloadButton(toolbar_);
        dlBtn_->setToolTip("Downloads");
        dlBtn_->hide();

        // Download popup panel
        dlPanel_ = new DownloadPanel(this);

        // ── Find in page bar
        findBar_ = new FindBar(this);

        // ── Wallpaper live update on settings change
        SettingsStore::instance().onChange("appearance.wallpaper",
            [this](const QString&, const QVariant&){
                AppBackground::clearCache();
                if (home_) home_->refreshBackground();
                qDebug("[Background] Wallpaper changed — refreshed");
            });
        findBar_->hide();

        // Wire download toasts to this window
        DownloadToastManager::instance().reposition(this);

        QObject::connect(dlBtn_, &QAbstractButton::clicked, this, [this]{
            dlPanel_->popupUnder(dlBtn_);
        });

        addr_ = new AddressEdit(toolbar_);
        addr_->setPlaceholderText("Search or enter address");
        addr_->setFixedHeight(34);
        addr_->setClearButtonEnabled(true);
        QFont af = addr_->font(); af.setPixelSize(14); addr_->setFont(af);
        QPalette ap = addr_->palette();
        ap.setColor(QPalette::PlaceholderText, QColor(160, 166, 182, 130));
        addr_->setPalette(ap);
        addr_->setStyleSheet(
            "QLineEdit{"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "                              stop:0 #202225, stop:1 #18191c);"
            "  border:1px solid #2c2e34;"
            "  border-radius:20px;"
            "  padding:0 16px 0 8px;"
            "  color:#e8e9ec;"
            "  font-size:13.5px;"
            "  selection-background-color:#5d9df1;"
            "  selection-color:#ffffff;"
            "}"
            "QLineEdit:hover{"
            "  border-color:#3a3d45;"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "                              stop:0 #24262a, stop:1 #1c1d21);"
            "}"
            "QLineEdit:focus{"
            "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "                              stop:0 #1a1c20, stop:1 #141519);"
            "  border:1px solid #5d9df1;"
            "}");
        {
            QPixmap pm(36, 36);
            pm.setDevicePixelRatio(2.0);
            pm.fill(Qt::transparent);
            QPainter pp(&pm);
            drawIcon(pp, Ic::Search, QRectF(0, 0, 18, 18), QColor("#8a8f9c"));
            pp.end();
            addr_->addAction(QIcon(pm), QLineEdit::LeadingPosition);
        }

        tb->addWidget(back_);
        tb->addWidget(fwd_);
        tb->addWidget(reload_);
        tb->addWidget(homeBtn_);
        tb->addSpacing(8);
        tb->addWidget(addr_, 1);

        // ── URL autocomplete dropdown
        autocomplete_ = new UrlAutocomplete(this);
        autocomplete_->attach(addr_);
        addr_->installEventFilter(this);
        tb->addSpacing(8);
        tb->addWidget(dlBtn_);   // ← download button before full/menu
        tb->addWidget(full_);
        tb->addWidget(menuBtn_);

        bar_ = new LoadBar(this);

        stack_ = new QStackedWidget(this);
        home_ = new HomePage(stack_);
        stack_->addWidget(home_);

        root->addWidget(tabs_);
        root->addWidget(toolbar_);

        // ── Bookmarks bar (below toolbar)
        bookmarksBar_ = new BookmarksBar(this);

        // ── Command Palette (Ctrl+K)
        commandPalette_ = new CommandPalette(this);
        commandPalette_->hide();
        commandPalette_->add("New Tab",              "Ctrl+T",     "Tabs",      "", [this]{ newTab(); });
        commandPalette_->add("Close Tab",            "Ctrl+W",     "Tabs",      "", [this]{ closeTab(cur_); });
        commandPalette_->add("Next Tab",             "Ctrl+Tab",   "Tabs",      "", [this]{ switchBy(1); });
        commandPalette_->add("Previous Tab",         "Ctrl+Shift+Tab", "Tabs",  "", [this]{ switchBy(-1); });
        commandPalette_->add("New Private Window",   "Ctrl+Shift+N", "Privacy", "", [this]{ showPrivateModePage(); });
        commandPalette_->add("Focus Address Bar",    "Ctrl+L",     "Navigation","", [this]{ focusAddress(); });
        commandPalette_->add("Reload",               "F5",         "Navigation","", [this]{ reloadPage(); });
        commandPalette_->add("Back",                 "Alt+Left",   "Navigation","", [this]{ goBack(); });
        commandPalette_->add("Forward",              "Alt+Right",  "Navigation","", [this]{ goForward(); });
        commandPalette_->add("Home",                 "Alt+Home",   "Navigation","", [this]{ goHome(); });
        commandPalette_->add("Bookmarks",            "Ctrl+B",     "Tools",     "", [this]{ showBookmarksPage(); });
        commandPalette_->add("Bookmark This Page",   "Ctrl+D",     "Tools",     "", [this]{ bookmarkCurrentPage(); });
        commandPalette_->add("Toggle Bookmarks Bar", "Ctrl+Shift+B","Tools",    "", [this]{ if (bookmarksBar_) bookmarksBar_->setVisible(!bookmarksBar_->isVisible()); });
        commandPalette_->add("History",              "Ctrl+H",     "Tools",     "", [this]{ showHistoryPage(); });
        commandPalette_->add("Downloads",            "Ctrl+J",     "Tools",     "", [this]{ showDownloadsPage(); });
        commandPalette_->add("Find in Page",         "Ctrl+F",     "Tools",     "", [this]{ if (findBar_) { findBar_->attach(data_[cur_].view); findBar_->open(); } });
        commandPalette_->add("Settings",             "Ctrl+,",     "Tools",     "", [this]{ showSettingsPage(); });
        commandPalette_->add("Zoom In",              "Ctrl++",     "View",      "", [this]{ zoomBy(0.1); });
        commandPalette_->add("Zoom Out",             "Ctrl+-",     "View",      "", [this]{ zoomBy(-0.1); });
        commandPalette_->add("Reset Zoom",           "Ctrl+0",     "View",      "", [this]{ zoomBy(0.0, true); });
        commandPalette_->add("Toggle Full Screen",   "F11",        "View",      "", [this]{ toggleFullscreen(); });
        commandPalette_->add("Keyboard Shortcuts",   "Ctrl+/",     "Help",      "", [this]{ if (shortcutHelp_) shortcutHelp_->open(); });
        commandPalette_->add("Quit RootBrowser",     "Ctrl+Q",     "App",       "", [this]{ close(); });
bookmarksBar_->onOpenUrl = [this](const QString& url){
            navigate(url);
        };
        bookmarksBar_->onOpenInNewTab = [this](const QString& url){
            openUrlInNewTab(QUrl(url));
        };

        // Insert it into the layout right after the toolbar
        root->insertWidget(2, bookmarksBar_);
        root->addWidget(bar_);
        root->addWidget(stack_, 1);

        tabs_->onCurrent = [this](int i) { cur_ = i; refresh(); };
        tabs_->onClose   = [this](int i) { closeTab(i); };
        tabs_->onNew     = [this] { newTab(); };
        tabs_->onMin       = [this] { showMinimized(); };
        tabs_->onCloseWin  = [this] { close(); };
        tabs_->onMaxToggle = [this] {
            if (isFullScreen()) return;
            if (isMaximized()) showNormal(); else showMaximized();
        };
        tabs_->onDrag = [this] {
            if (!isFullScreen() && windowHandle()) windowHandle()->startSystemMove();
        };

        home_->onNavigate = [this](const QString& s) { navigate(s); };
        home_->onContextMenu = [this](const QPoint& pos) {
            QMenu menu(this);
            ContextMenu::populateMenuForHost(&menu, this);
            if (!menu.isEmpty())
                menu.exec(home_->mapToGlobal(pos));
        };

        QObject::connect(back_,    &QAbstractButton::clicked, this, [this] { goBack(); });
        QObject::connect(fwd_,     &QAbstractButton::clicked, this, [this] { goForward(); });
        QObject::connect(reload_,  &QAbstractButton::clicked, this, [this] { reloadPage(); });
        QObject::connect(homeBtn_, &QAbstractButton::clicked, this, [this] { goHome(); });
        QObject::connect(full_,    &QAbstractButton::clicked, this, [this] { toggleFullscreen(); });
        QObject::connect(menuBtn_, &QAbstractButton::clicked, this, [this] { showMenu(); });
        QObject::connect(addr_, &QLineEdit::returnPressed, this,
                         [this] { navigate(addr_->text()); });

        // ── Live download-activity timer (500ms)
        auto* dlTimer = new QTimer(this);
        QObject::connect(dlTimer, &QTimer::timeout, this, [this]{
            const int active = DownloadManager::instance().activeCount();
            const int total  = DownloadManager::instance().count();

            if (total > 0) {
                dlBtn_->show();
                dlBtn_->setActive(active > 0);
            } else {
                dlBtn_->hide();
                dlBtn_->setActive(false);
            }

            if (active > 0)
                refreshDownloadsPage();

            if (dlPanel_ && dlPanel_->isVisible())
                dlPanel_->refresh();
        });
        dlTimer->start(500);

        // ── Keyboard shortcut help modal
        shortcutHelp_ = new ShortcutHelp(this);
        shortcutHelp_->hide();

        auto sc = [this](const QKeySequence& k, std::function<void()> fn) {
            auto* s = new QShortcut(k, this);
            QObject::connect(s, &QShortcut::activated, this, [fn] { fn(); });
            return s;
        };
        sc(QKeySequence("Ctrl+T"),         [this] { newTab(); });
        sc(QKeySequence("Ctrl+W"),         [this] { closeTab(cur_); });
        sc(QKeySequence("Ctrl+L"),         [this] { focusAddress(); });
        sc(QKeySequence("F6"),             [this] { focusAddress(); });
        sc(QKeySequence("F11"),            [this] { toggleFullscreen(); });
        sc(QKeySequence("Alt+Left"),       [this] { goBack(); });
        sc(QKeySequence("Alt+Right"),      [this] { goForward(); });
        sc(QKeySequence("Alt+Home"),       [this] { goHome(); });
        sc(QKeySequence("F5"),             [this] { reloadPage(); });
        sc(QKeySequence("Ctrl+R"),         [this] { reloadPage(); });
        sc(QKeySequence("Ctrl+Tab"),       [this] { switchBy(1); });
        sc(QKeySequence("Ctrl+Shift+Tab"), [this] { switchBy(-1); });
        sc(QKeySequence("Ctrl+Q"),         [this] { close(); });
        sc(QKeySequence("Ctrl++"),         [this] { zoomBy(0.1); });
        sc(QKeySequence("Ctrl+="),         [this] { zoomBy(0.1); });
        sc(QKeySequence("Ctrl+-"),         [this] { zoomBy(-0.1); });
        sc(QKeySequence("Ctrl+0"),         [this] { zoomBy(0.0, true); });

        sc(QKeySequence("Ctrl+B"),         [this] { showBookmarksPage(); });
        sc(QKeySequence("Ctrl+D"),         [this] { bookmarkCurrentPage(); });
        sc(QKeySequence("Ctrl+J"),         [this] { showDownloadsPage(); });

        sc(QKeySequence("Ctrl+H"),         [this] { showHistoryPage(); });
        sc(QKeySequence("Ctrl+,"),         [this] { showSettingsPage(); });

        sc(QKeySequence("Ctrl+Shift+B"),   [this] {
            if (bookmarksBar_) bookmarksBar_->setVisible(!bookmarksBar_->isVisible());
        });
        sc(QKeySequence("Ctrl+Shift+N"),   [this] { showPrivateModePage(); });

        // ── Find in page ─────────────────────────────────────────
        sc(QKeySequence("Ctrl+F"), [this] {
            if (!findBar_) return;
            findBar_->attach(data_[cur_].view);
            const int topOffset = kEdge + 46 + 56 + 6;
            findBar_->move(width() - findBar_->width() - 16, topOffset);
            findBar_->open();
        });

        sc(QKeySequence("F3"), [this] {
            if (findBar_ && findBar_->isVisible()) findBar_->findNext();
        });

        sc(QKeySequence("Shift+F3"), [this] {
            if (findBar_ && findBar_->isVisible()) findBar_->findPrevious();
        });

        escSc_ = sc(QKeySequence("Esc"), [this] {
            if (findBar_ && findBar_->isVisible()) { findBar_->close(); return; }
            onEscape();
        });

        // ── Ctrl+K Command Palette
        sc(QKeySequence("Ctrl+K"), [this] {
            if (!commandPalette_) return;
            if (commandPalette_->isOpen())
                commandPalette_->close();
            else
                commandPalette_->open();
        });

        // ── Ctrl+/ Keyboard Shortcut Help
        sc(QKeySequence("Ctrl+/"), [this] {
            if (!shortcutHelp_) return;
            if (shortcutHelp_->isVisible())
                shortcutHelp_->close();
            else
                shortcutHelp_->open();
        });
        escSc_->setEnabled(false);

        // Fresh start — always open a single blank tab
        data_.push_back(TabData());
        tabs_->addTab("New Tab");
        cur_ = 0;
        refresh();
    }

    ~Browser() override {
        for (auto& t : data_) { delete t.view; t.view = nullptr; }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void openUrl(const QString& u) { navigate(u); }

    // ========================================================================
    //  Host
    // ========================================================================
    void openUrlInNewTab(const QUrl& u) override {
        TabData t;
        t.onHome = false;
        t.view = makeView();
        data_.push_back(t);
        tabs_->addTab(titleFor(u.toString()));
        cur_ = int(data_.size()) - 1;
        t.view->load(u);
        refresh();
        t.view->setFocus();
    }
    void openSearchInNewTab(const QString& q) override {
        const QString u = normalizeInput(q);
        if (u.isEmpty()) return;
        openUrlInNewTab(QUrl(u));
    }
    void openHtmlInNewTab(const QString& html, const QString& title) override {
        TabData t;
        t.onHome = false;
        t.view = makeView();
        data_.push_back(t);
        tabs_->addTab(title.isEmpty() ? "Page" : title);
        cur_ = int(data_.size()) - 1;
        t.view->setHtml(html);
        refresh();
        t.view->setFocus();
    }
    void saveCurrentPage(QWebEnginePage* p) override {
        ContextMenu::savePageAs(this, p);
        showNote("Saving page…");
    }
    void printCurrentPage(QWebEnginePage* p) override {
        ContextMenu::printPageToPdf(p);
        showNote("Exported to PDF in Downloads");
    }
    void viewPageSource(QWebEnginePage* p) override {
        ContextMenu::openPageSource(p);
    }
    void inspectPage(QWebEnginePage* p) override {
        ContextMenu::openDevTools(p);
    }
    void toggleFullscreen() override {
        if (webFull_) { onEscape(); return; }
        if (isFullScreen()) {
            if (wasMax_) showMaximized(); else showNormal();
        } else {
            wasMax_ = isMaximized();
            showFullScreen();
        }
    }

protected:
    void changeEvent(QEvent* e) override {
        QWidget::changeEvent(e);
        if (e->type() == QEvent::WindowStateChange && full_ && escSc_ && root_) {
            full_->setKind(isFullScreen() ? Ic::Exit : Ic::Full);
            escSc_->setEnabled(isFullScreen());
            const int m = (isMaximized() || isFullScreen()) ? 0 : kEdge;
            root_->setContentsMargins(m, m, m, m);
            tabs_->setMaximized(isMaximized());
            update();
        }
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#0c0c0e"));
        if (!isMaximized() && !isFullScreen()) {
            p.setPen(QColor(255, 255, 255, 30));
            p.drawRect(rect().adjusted(kEdge - 1, kEdge - 1, -(kEdge - 1), -(kEdge - 1)));
        }
    }

    Qt::Edges edgesAt(const QPoint& p) const {
        Qt::Edges e;
        if (isMaximized() || isFullScreen()) return e;
        const bool L = p.x() < kEdge, R = p.x() >= width() - kEdge;
        const bool T = p.y() < kEdge, B = p.y() >= height() - kEdge;
        if (L) e |= Qt::LeftEdge;
        if (R) e |= Qt::RightEdge;
        if (T) e |= Qt::TopEdge;
        if (B) e |= Qt::BottomEdge;
        const int c = 14;
        if (L || R) { if (p.y() < c) e |= Qt::TopEdge; if (p.y() >= height() - c) e |= Qt::BottomEdge; }
        if (T || B) { if (p.x() < c) e |= Qt::LeftEdge; if (p.x() >= width() - c) e |= Qt::RightEdge; }
        return e;
    }
    void mouseMoveEvent(QMouseEvent* ev) override {
        const Qt::Edges e = edgesAt(ev->position().toPoint());
        const bool h = e.testFlag(Qt::LeftEdge) || e.testFlag(Qt::RightEdge);
        const bool v = e.testFlag(Qt::TopEdge) || e.testFlag(Qt::BottomEdge);
        if (h && v) {
            const bool diagF = (e.testFlag(Qt::LeftEdge) && e.testFlag(Qt::TopEdge)) ||
                               (e.testFlag(Qt::RightEdge) && e.testFlag(Qt::BottomEdge));
            setCursor(diagF ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        } else if (h) setCursor(Qt::SizeHorCursor);
        else if (v) setCursor(Qt::SizeVerCursor);
        else unsetCursor();
    }
    bool eventFilter(QObject* obj, QEvent* e) override {
        // URL autocomplete keyboard navigation
        if (obj == addr_ && e->type() == QEvent::KeyPress && autocomplete_) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (autocomplete_->handleKeyPress(ke->key(), int(ke->modifiers())))
                return true;
        }
        return QWidget::eventFilter(obj, e);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        if (dlPanel_ && dlPanel_->isVisible()) {
            const QPoint g = ev->globalPosition().toPoint();
            const QRect panelRect(dlPanel_->mapToGlobal(QPoint(0, 0)),
                                  dlPanel_->size());
            bool onButton = false;
            if (dlBtn_) {
                const QRect btnRect(dlBtn_->mapToGlobal(QPoint(0, 0)),
                                    dlBtn_->size());
                onButton = btnRect.contains(g);
            }
            if (!panelRect.contains(g) && !onButton)
                dlPanel_->hide();
        }

        if (ev->button() == Qt::LeftButton && windowHandle()) {
            const Qt::Edges e = edgesAt(ev->position().toPoint());
            if (e.testFlag(Qt::LeftEdge) || e.testFlag(Qt::RightEdge) ||
                e.testFlag(Qt::TopEdge) || e.testFlag(Qt::BottomEdge)) {
                windowHandle()->startSystemResize(e);
                return;
            }
        }
        QWidget::mousePressEvent(ev);
    }
    void leaveEvent(QEvent* ev) override { unsetCursor(); QWidget::leaveEvent(ev); }

private:
    struct TabData {
        WebView* view = nullptr;
        bool onHome = true;
        bool isPrivate = false;      // Tor private tab
        bool proxyActive = false;
    };

    int indexOf(WebView* v) const {
        for (int i = 0; i < data_.size(); ++i) if (data_[i].view == v) return i;
        return -1;
    }

    static QString shown(const QUrl& u) {
        const QString s = u.toString();
        return s == "about:blank" ? QString() : s;
    }

    void installYouTubeAdNeutralizer() {
        QWebEngineScript s;
        s.setName(QStringLiteral("graphite-yt-ad-neutralizer"));
        s.setSourceCode(QString::fromUtf8(kYouTubeAdNeutralizer));
        s.setInjectionPoint(QWebEngineScript::DocumentCreation);
        s.setWorldId(QWebEngineScript::MainWorld);
        s.setRunsOnSubFrames(true);
        profile_->scripts()->insert(s);

        // ── Fingerprint protection script
        QWebEngineScript fp;
        fp.setName(QStringLiteral("graphite-fingerprint-protection"));
        fp.setSourceCode(FingerprintProtection::injectScript());
        fp.setInjectionPoint(QWebEngineScript::DocumentCreation);
        fp.setWorldId(QWebEngineScript::MainWorld);
        fp.setRunsOnSubFrames(true);
        profile_->scripts()->insert(fp);
    }

    QWebEngineProfile* getPrivateProfile() {
        if (!privateProfile_) {
            privateProfile_ = new QWebEngineProfile(this);   // off-the-record
            privateProfile_->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
            privateProfile_->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

            auto* st = privateProfile_->settings();
            st->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
            st->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
            st->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
        }
        return privateProfile_;
    }

    void applyProxyForTab(const TabData& t) {
        const bool needProxy = t.isPrivate && TorController::instance().isReady();

        if (needProxy && !proxyActive_) {
            QNetworkProxy proxy;
            proxy.setType(QNetworkProxy::Socks5Proxy);
            proxy.setHostName("127.0.0.1");
            proxy.setPort(quint16(TorController::instance().socksPort()));
            QNetworkProxy::setApplicationProxy(proxy);
            proxyActive_ = true;
        } else if (!needProxy && proxyActive_) {
            QNetworkProxy::setApplicationProxy(QNetworkProxy::DefaultProxy);
            proxyActive_ = false;
        }
    }

    void openPrivateTab(const QUrl& initialUrl = QUrl()) {
        // Tor ready check
        if (!TorController::instance().isReady()) {
            TabData t;
            t.view = makeView();
            data_.push_back(t);
            tabs_->addTab("Private Mode");
            cur_ = int(data_.size()) - 1;
            t.view->setHtml(PrivateModePage::build(),
                            QUrl(QStringLiteral("rootbrowser://privatemode")));
            refresh();
            return;
        }

        // Open a proper private tab
        TabData t;
        t.isPrivate = true;
        t.view = makeView(true);       // ← use private profile
        data_.push_back(t);
        tabs_->addTab("\U0001F9C5 Private (Tor)");
        cur_ = int(data_.size()) - 1;

        // Mark tab as private in strip
        tabs_->setPrivate(cur_, true);

        // Apply Tor proxy
        applyProxyForTab(t);

        // Load URL
        const QUrl url = initialUrl.isEmpty()
            ? QUrl(QStringLiteral("https://check.torproject.org"))
            : initialUrl;
        t.view->load(url);
        refresh();
        t.view->setFocus();
    }

    WebView* makeView(bool isPrivate = false) {
        auto* v = new WebView(stack_);
        auto* pg = new WebPage(isPrivate ? getPrivateProfile() : profile_, v);
        pg->setBackgroundColor(QColor("#0c0c0e"));
        pg->onNewWindow = [this]() -> QWebEnginePage* { return openBlankTab(); };

        pg->onSpecialScheme = [this](const QUrl& url) -> bool {
            const QString s = url.toString();

            if (s.startsWith("rootbrowser-goto:", Qt::CaseInsensitive)) {
                const QString target = s.mid(QStringLiteral("rootbrowser-goto:").length());
                TabData& t = data_[cur_];
                if (t.view) t.view->load(QUrl(target));
                return true;
            }
            if (s.startsWith("rootbrowser-bookmark-del:", Qt::CaseInsensitive)) {
                const QString target = s.mid(QStringLiteral("rootbrowser-bookmark-del:").length());
                BookmarkStore::instance().remove(target);
                const TabData& t = data_[cur_];
                if (t.view)
                    t.view->setHtml(BookmarkPage::build(),
                                    QUrl(QStringLiteral("rootbrowser://bookmarks")));
                return true;
            }

            // ── Private Mode (Tor) actions ─────────────────────────
            if (s.startsWith("rootbrowser-privatemode:", Qt::CaseInsensitive)) {
                const QString action = s.mid(QStringLiteral("rootbrowser-privatemode:").length());

                if (action == "start-tor") {
                    TorController::instance().start([](bool ok){
                        Q_UNUSED(ok);
                    });
                    // Refresh the page
                    QTimer::singleShot(200, this, [this]{
                        const TabData& t = data_[cur_];
                        if (t.view) {
                            t.view->setHtml(PrivateModePage::build(),
                                            QUrl(QStringLiteral("rootbrowser://privatemode")));
                        }
                    });
                }
                else if (action == "refresh") {
                    const TabData& t = data_[cur_];
                    if (t.view) {
                        t.view->setHtml(PrivateModePage::build(),
                                        QUrl(QStringLiteral("rootbrowser://privatemode")));
                    }
                }
                else if (action == "open-window") {
                    if (TorController::instance().isReady()) {
                        // Close the current privatemode status tab
                        const int idx = cur_;
                        // Open a fresh private tab
                        openPrivateTab();
                        // Remove the status tab if it's still around
                        if (idx < data_.size() && data_[idx].view &&
                            data_[idx].view->url().toString().startsWith("rootbrowser://privatemode")) {
                            // Schedule removal for next tick
                            QTimer::singleShot(0, this, [this, idx]{
                                if (idx < data_.size()) closeTab(idx);
                            });
                        }
                    }
                }
                return true;
            }

            // ── Settings page actions ─────────────────────────────
            if (s.startsWith("rootbrowser-settings-set:", Qt::CaseInsensitive)) {
                // Format: rootbrowser-settings-set:<key>:<type>:<value>
                const QString payload = s.mid(QStringLiteral("rootbrowser-settings-set:").length());
                const QStringList parts = payload.split(':');
                if (parts.size() >= 3) {
                    const QString key   = QUrl::fromPercentEncoding(parts[0].toUtf8());
                    const QString type  = parts[1];
                    const QString value = QUrl::fromPercentEncoding(parts.mid(2).join(':').toUtf8());

                    auto& S = SettingsStore::instance();
                    if (type == "bool")
                        S.setBool(key, value == "true");
                    else if (type == "int")
                        S.setInt(key, value.toInt());
                    else if (type == "double")
                        S.setDouble(key, value.toDouble());
                    else
                        S.setString(key, value);

                    // Reload settings page silently
                    const TabData& t = data_[cur_];
                    if (t.view) {
                        t.view->setHtml(SettingsPage::build(),
                                        QUrl(QStringLiteral("rootbrowser://settings")));
                    }
                }
                return true;
            }
            if (s.startsWith("rootbrowser-settings-action:", Qt::CaseInsensitive)) {
                const QString action = s.mid(QStringLiteral("rootbrowser-settings-action:").length());

                if (action == "pick-download-folder") {
                    const QString dir = QFileDialog::getExistingDirectory(
                        this, "Choose download folder",
                        DownloadManager::instance().defaultFolder());
                    if (!dir.isEmpty()) {
                        DownloadManager::instance().setDefaultFolder(dir);
                        SettingsStore::instance().setString("downloads.folder", dir);
                    }
                    const TabData& t = data_[cur_];
                    if (t.view) {
                        t.view->setHtml(SettingsPage::build(),
                                        QUrl(QStringLiteral("rootbrowser://settings")));
                    }
                }
                else if (action == "clear-data") {
                    // Wipe cookies, cache, history, downloads list, session
                    DownloadManager::instance().clearAll();
                    HistoryStore::instance().clearAll();
                    
                    SettingsStore::instance().clearAllData();

                    const TabData& t = data_[cur_];
                    if (t.view) {
                        t.view->setHtml(SettingsPage::build(),
                                        QUrl(QStringLiteral("rootbrowser://settings")));
                    }
                }
                else if (action == "reset") {
                    SettingsStore::instance().resetAll();
                    applyAllSettings();   // re-apply

                    const TabData& t = data_[cur_];
                    if (t.view) {
                        t.view->setHtml(SettingsPage::build(),
                                        QUrl(QStringLiteral("rootbrowser://settings")));
                    }
                }
                else if (action == "open-folder") {
                    const QString dataDir = QStandardPaths::writableLocation(
                        QStandardPaths::AppDataLocation);
                    QDesktopServices::openUrl(QUrl::fromLocalFile(dataDir));
                }
                return true;
            }

            // ── History page actions ───────────────────────────────
            if (s.startsWith("rootbrowser-history-del:", Qt::CaseInsensitive)) {
                const QString encoded = s.mid(QStringLiteral("rootbrowser-history-del:").length());
                const QString url = QUrl::fromPercentEncoding(encoded.toUtf8());
                HistoryStore::instance().remove(url);
                refreshHistoryPage();
                return true;
            }
            if (s.startsWith("rootbrowser-history-clear:", Qt::CaseInsensitive)) {
                const QString what = s.mid(QStringLiteral("rootbrowser-history-clear:").length()).toLower();
                int days = -1;   // -1 = clear all
                if      (what == "hour")  days = 0;   // handled as clear all under 1h
                else if (what == "today") days = 1;
                else if (what == "week")  days = 7;
                else if (what == "month") days = 30;
                else                      days = 0;   // "all" → clear all

                if (what == "all" || what == "hour")
                    HistoryStore::instance().clearAll();
                else
                    HistoryStore::instance().clearOlderThan(days);

                refreshHistoryPage();
                return true;
            }
            if (s.startsWith("rootbrowser-history-day-del:", Qt::CaseInsensitive)) {
                // Delete all entries for a specific calendar day.
                // Format: YYYY-M-D
                const QString dayKey = s.mid(QStringLiteral("rootbrowser-history-day-del:").length());
                const QStringList parts = dayKey.split('-');
                if (parts.size() == 3) {
                    QDate d(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
                    if (d.isValid()) {
                        const qint64 startOfDay =
                            QDateTime(d, QTime(0, 0, 0)).toSecsSinceEpoch();
                        const qint64 endOfDay = startOfDay + 24 * 3600;

                        // Collect URLs to remove
                        QVector<QString> toRemove;
                        for (const HistoryEntry& e : HistoryStore::instance().all()) {
                            if (e.visitedAt >= startOfDay && e.visitedAt < endOfDay)
                                toRemove.push_back(e.url);
                        }
                        // Remove each (dedup handled internally)
                        for (const QString& u : toRemove)
                            HistoryStore::instance().remove(u);
                    }
                }
                refreshHistoryPage();
                return true;
            }

            if (s.startsWith("rootbrowser-dl-pause:", Qt::CaseInsensitive)) {
                DownloadManager::instance().pause(s.mid(21));
                refreshDownloadsPage();
                return true;
            }
            if (s.startsWith("rootbrowser-dl-resume:", Qt::CaseInsensitive)) {
                DownloadManager::instance().resume(s.mid(22));
                refreshDownloadsPage();
                return true;
            }
            if (s.startsWith("rootbrowser-dl-cancel:", Qt::CaseInsensitive)) {
                DownloadManager::instance().cancel(s.mid(22));
                refreshDownloadsPage();
                return true;
            }
            if (s.startsWith("rootbrowser-dl-remove:", Qt::CaseInsensitive)) {
                DownloadManager::instance().remove(s.mid(22));
                refreshDownloadsPage();
                return true;
            }
            if (s.startsWith("rootbrowser-dl-open:", Qt::CaseInsensitive)) {
                DownloadManager::instance().openFile(s.mid(20));
                return true;
            }
            if (s.startsWith("rootbrowser-dl-folder:", Qt::CaseInsensitive)) {
                const QString arg = s.mid(22);
                if (arg == "pick") {
                    const QString dir = QFileDialog::getExistingDirectory(
                        this, "Choose download folder",
                        DownloadManager::instance().defaultFolder());
                    if (!dir.isEmpty())
                        DownloadManager::instance().setDefaultFolder(dir);
                    refreshDownloadsPage();
                } else {
                    DownloadManager::instance().openFolder(arg);
                }
                return true;
            }
            if (s.startsWith("rootbrowser-dl-retry:", Qt::CaseInsensitive)) {
                DownloadManager::instance().retry(s.mid(21));
                refreshDownloadsPage();
                return true;
            }
            if (s.startsWith("rootbrowser-dl-clear:", Qt::CaseInsensitive)) {
                DownloadManager::instance().clearFinished();
                refreshDownloadsPage();
                return true;
            }

            return false;
        };

        v->setPage(pg);
        stack_->addWidget(v);

        v->onContextMenu = [this, v](const QPoint& pos) {
            auto* page = v->page();
            if (!page) return;
            QWebEngineContextMenuRequest* req = v->lastContextMenuRequest();
            QMenu menu(this);
            ContextMenu::populateMenu(&menu, page, req);
            if (!menu.isEmpty())
                menu.exec(v->mapToGlobal(pos));
        };

        QObject::connect(v, &QWebEngineView::titleChanged, this,
                         [this, v](const QString&) {
            const int i = indexOf(v); if (i >= 0) syncTitle(i);
        });
        QObject::connect(v, &QWebEngineView::urlChanged, this,
                         [this, v](const QUrl& u) {
            const int i = indexOf(v);
            if (i < 0) return;
            syncTitle(i);
            if (i == cur_) {
                if (!addr_->hasFocus() && !data_[i].onHome)
                    addr_->setText(shown(u));
                updateNav();
            }
        });
        QObject::connect(v, &QWebEngineView::iconChanged, this,
                         [this, v](const QIcon& ic) {
            const int i = indexOf(v);
            if (i >= 0) tabs_->setIcon(i, ic.isNull() ? QPixmap() : ic.pixmap(32, 32));
        });
        QObject::connect(v, &QWebEngineView::loadStarted, this, [this, v] {
            if (indexOf(v) == cur_) bar_->setValue(6);
        });
        QObject::connect(v, &QWebEngineView::loadProgress, this, [this, v](int p) {
            if (indexOf(v) == cur_) bar_->setValue(std::max(6, p));
        });
        QObject::connect(v, &QWebEngineView::loadFinished, this, [this, v](bool ok) {
            if (indexOf(v) == cur_) { bar_->setValue(100); updateNav(); }

            // Record history (skip internal pages + errors)
            if (ok && v && v->page()) {
                const QUrl u = v->url();
                const QString s = u.toString();
                if (!s.startsWith("rootbrowser://") &&
                    !s.startsWith("rootbrowser:") &&
                    !s.startsWith("about:") &&
                    !s.isEmpty() &&
                    s != "about:blank")
                {
                    if (SettingsStore::instance().getBool("history.record", true))
                        HistoryStore::instance().record(s, v->title());
                }
            }
        });
        QObject::connect(pg, &QWebEnginePage::fullScreenRequested, this,
                         [this](QWebEngineFullScreenRequest r) {
            r.accept();
            setWebFullscreen(r.toggleOn());
        });
        QObject::connect(pg, &QWebEnginePage::windowCloseRequested, this, [this, v] {
            const int i = indexOf(v); if (i >= 0) closeTab(i);
        });
        return v;
    }

    QWebEnginePage* openBlankTab() {
        TabData t;
        t.onHome = false;
        t.view = makeView();
        data_.push_back(t);
        tabs_->addTab("New Tab");
        cur_ = int(data_.size()) - 1;
        refresh();
        return t.view->page();
    }

    void syncTitle(int i) {
        const TabData& t = data_[i];
        QString title = "New Tab";
        if (!t.onHome && t.view) {
            title = t.view->title();
            if (title.isEmpty()) title = titleFor(t.view->url().toString());
        }
        tabs_->setTitle(i, title);
        if (i == cur_)
            setWindowTitle((t.onHome || !t.view) ? QString(kAppName)
                                                 : title + " \u2014 " + kAppName);
    }

    void updateNav() {
        const TabData& t = data_[cur_];
        bool b = false, f = false;
        if (t.view && !t.onHome) { b = true; f = t.view->history()->canGoForward(); }
        else if (t.view && t.onHome) { f = true; }
        back_->setEnabled(b);
        fwd_->setEnabled(f);
    }

    void refresh() {
        const TabData& t = data_[cur_];

        // Apply/reset Tor proxy based on tab
        applyProxyForTab(t);
        const bool page = (t.view && !t.onHome);
        stack_->setCurrentWidget(page ? static_cast<QWidget*>(t.view)
                                      : static_cast<QWidget*>(home_));
        addr_->setText(page ? shown(t.view->url()) : QString());
        tabs_->setCurrent(cur_);
        syncTitle(cur_);
        updateNav();
        bar_->setValue(0);

        // Re-attach find bar to current view
        if (findBar_ && findBar_->isVisible() && t.view)
            findBar_->attach(t.view);
    }

    void showPrivateModePage() {
        // Ensure Tor is ready (start async if not)
        auto& T = TorController::instance();
        if (!T.isReady()) {
            T.start([](bool){ /* ignore — new window will show status */ });
        }

        // Open a dedicated Private Browser window
        auto* w = new PrivateBrowser(this);
        w->show();
        w->raise();
        w->activateWindow();
    }

    void showSettingsPage() {
        TabData& t = data_[cur_];
        if (!t.view) t.view = makeView();
        t.onHome = false;
        t.view->setHtml(SettingsPage::build(),
                        QUrl(QStringLiteral("rootbrowser://settings")));
        home_->clearSearch();
        refresh();
        t.view->setFocus();
    }

    void showHistoryPage() {
        TabData& t = data_[cur_];
        if (!t.view) t.view = makeView();
        t.onHome = false;
        t.view->setHtml(HistoryPage::build(),
                        QUrl(QStringLiteral("rootbrowser://history")));
        home_->clearSearch();
        refresh();
        t.view->setFocus();
    }

    void refreshHistoryPage() {
        const TabData& t = data_[cur_];
        if (!t.view) return;
        const QString url = t.view->url().toString();
        if (url != "rootbrowser://history") return;
        t.view->setHtml(HistoryPage::build(),
                        QUrl(QStringLiteral("rootbrowser://history")));
    }

    void showBookmarksPage() {
        TabData& t = data_[cur_];
        if (!t.view) t.view = makeView();
        t.onHome = false;
        t.view->setHtml(BookmarkPage::build(),
                        QUrl(QStringLiteral("rootbrowser://bookmarks")));
        home_->clearSearch();
        refresh();
        t.view->setFocus();
    }

    void bookmarkCurrentPage() {
        const TabData& t = data_[cur_];
        if (!t.view || t.onHome) return;
        const QString url = t.view->url().toString();
        const QString title = t.view->title();
        if (url.isEmpty()) return;
        BookmarkStore::instance().addOrUpdate(url, title);
        if (bookmarksBar_) bookmarksBar_->reload();
        showNote("Bookmarked");
    }

    void showDownloadsPage() {
        TabData& t = data_[cur_];
        if (!t.view) t.view = makeView();
        t.onHome = false;
        t.view->setHtml(DownloadPage::build(),
                        QUrl(QStringLiteral("rootbrowser://downloads")));
        home_->clearSearch();
        refresh();
        t.view->setFocus();
    }

    void refreshDownloadsPage() {
        const TabData& t = data_[cur_];
        if (!t.view) return;
        const QString url = t.view->url().toString();
        if (url != "rootbrowser://downloads") return;
        t.view->setHtml(DownloadPage::build(),
                        QUrl(QStringLiteral("rootbrowser://downloads")));
    }

    void navigate(const QString& input) {
        const QString u = normalizeInput(input);
        if (u.isEmpty()) return;

        if (u == "rootbrowser://bookmarks" || u == "rootbrowser:bookmarks") {
            showBookmarksPage();
            return;
        }
        if (u == "rootbrowser://downloads" || u == "rootbrowser:downloads") {
            showDownloadsPage();
            return;
        }

        if (u.startsWith("rootbrowser-goto:", Qt::CaseInsensitive)) {
            const QString url = u.mid(QStringLiteral("rootbrowser-goto:").length());
            TabData& t = data_[cur_];
            if (!t.view) t.view = makeView();
            t.onHome = false;
            t.view->load(QUrl(url));
            refresh();
            return;
        }
        if (u.startsWith("rootbrowser-bookmark-del:", Qt::CaseInsensitive)) {
            const QString url = u.mid(QStringLiteral("rootbrowser-bookmark-del:").length());
            BookmarkStore::instance().remove(url);
            showBookmarksPage();
            return;
        }

        TabData& t = data_[cur_];
        if (!t.view) t.view = makeView();
        t.onHome = false;
        if (findBar_ && findBar_->isVisible()) findBar_->close();

        t.view->load(QUrl::fromUserInput(u));
        home_->clearSearch();
        refresh();
        t.view->setFocus();
    }
    void goBack() {
        TabData& t = data_[cur_];
        if (!t.view || t.onHome) return;
        if (t.view->history()->canGoBack()) t.view->back();
        else { t.onHome = true; refresh(); }
    }
    void goForward() {
        TabData& t = data_[cur_];
        if (!t.view) return;
        if (t.onHome) { t.onHome = false; refresh(); t.view->setFocus(); }
        else t.view->forward();
    }
    void goHome() {
        TabData& t = data_[cur_];
        if (t.onHome) { home_->focusSearch(); return; }
        t.onHome = true;
        refresh();
    }
    void reloadPage() {
        const TabData& t = data_[cur_];
        if (t.view && !t.onHome) t.view->reload();
        else refresh();
    }
    void zoomBy(double d, bool reset = false) {
        const TabData& t = data_[cur_];
        if (!t.view || t.onHome) return;
        const double z = reset ? 1.0 : std::clamp(t.view->zoomFactor() + d, 0.25, 5.0);
        t.view->setZoomFactor(z);
        showNote(QString("Zoom %1%").arg(int(std::lround(z * 100))));
    }



    void applyAllSettings() {
        auto& S = SettingsStore::instance();

        // ── Ad blocker
        WebAdBlocker::setEnabled(S.getBool("privacy.adblock", true));

        // ── Search engine (index into kEngines)
        const QString engineName = S.getString("search.engine", "DuckDuckGo");
        for (int i = 0; i < kEngineCount; ++i) {
            if (QString::fromLatin1(kEngines[i].name) == engineName) {
                g_engine = i;
                break;
            }
        }

        // ── Web engine settings
        if (profile_) {
            auto* st = profile_->settings();

            st->setAttribute(QWebEngineSettings::JavascriptEnabled,
                             S.getBool("behavior.javascript", true));
            st->setAttribute(QWebEngineSettings::AutoLoadImages,
                             S.getBool("behavior.loadImages", true));
            st->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled,
                             S.getBool("behavior.smoothScroll", true));
            st->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,
                             S.getBool("behavior.fullscreenNotif", true));

            // Do Not Track
            profile_->setHttpUserAgent(
                S.getBool("privacy.dnt", false)
                    ? profile_->httpUserAgent() // we can't easily inject DNT header post-hoc,
                                                // but the WebAdBlocker handles tracking
                    : profile_->httpUserAgent());
        }

        // ── Download folder
        const QString dlFolder = S.getString("downloads.folder");
        if (!dlFolder.isEmpty())
            DownloadManager::instance().setDefaultFolder(dlFolder);

        // ── Bookmark bar toggle (only if exists)
        if (auto* bb = findChild<QWidget*>("BookmarkBar"))
            bb->setVisible(S.getBool("appearance.bookmarkBar", false));

        // ── Update home page search engine label
        if (home_) home_->update();

        update();
    }

    void newTab() {
        data_.push_back(TabData());
        tabs_->addTab("New Tab");
        cur_ = int(data_.size()) - 1;
        refresh();
        focusAddress();
    }
    void closeTab(int i) {
        if (i < 0 || i >= data_.size()) return;
        if (data_.size() == 1) { close(); return; }
        WebView* v = data_[i].view;
        data_.removeAt(i);
        tabs_->removeTab(i);
        if (cur_ > i) cur_--;
        else if (cur_ == i) cur_ = std::min<int>(i, int(data_.size()) - 1);
        if (v) { stack_->removeWidget(v); v->hide(); v->deleteLater(); }
        refresh();
    }
    void switchBy(int d) {
        const int n = int(data_.size());
        cur_ = ((cur_ + d) % n + n) % n;
        refresh();
    }
    void focusAddress() { addr_->setFocus(); addr_->selectAll(); }

    void showNote(const QString& t) {
        tabs_->setNote(t);
        noteTimer_->start(3500);
    }

    void setWebFullscreen(bool on) {
        if (on == webFull_) return;
        webFull_ = on;
        tabs_->setVisible(!on);
        toolbar_->setVisible(!on);
        bar_->setVisible(!on);
        if (on) {
            prevFull_ = isFullScreen();
            if (!prevFull_) { wasMax_ = isMaximized(); showFullScreen(); }
        } else if (!prevFull_) {
            if (wasMax_) showMaximized(); else showNormal();
        }
    }
    void onEscape() {
        if (webFull_) {
            const TabData& t = data_[cur_];
            if (t.view) t.view->triggerPageAction(QWebEnginePage::ExitFullScreen);
            setWebFullscreen(false);
        } else if (isFullScreen()) {
            toggleFullscreen();
        }
    }

    void showMenu() {
        QMenu m(this);
        auto add = [&](const QString& text, std::function<void()> fn) {
            QAction* a = m.addAction(text);
            QObject::connect(a, &QAction::triggered, this, [fn] { fn(); });
        };
        add("New tab\tCtrl+T",       [this] { newTab(); });
        add("Close tab\tCtrl+W",     [this] { closeTab(cur_); });
        m.addSeparator();

        add("Bookmarks\tCtrl+B", [this] { showBookmarksPage(); });
        add("Bookmark this page\tCtrl+D", [this] { bookmarkCurrentPage(); });
        add("History\tCtrl+H", [this] { showHistoryPage(); });
        add("Downloads\tCtrl+J", [this] { showDownloadsPage(); });
        add("Settings\tCtrl+,", [this] { showSettingsPage(); });

        m.addSeparator();
        add("Home\tAlt+Home",        [this] { goHome(); });
        add("Reload\tF5",            [this] { reloadPage(); });
        add("Zoom in\tCtrl++",       [this] { zoomBy(0.1); });
        add("Zoom out\tCtrl+-",      [this] { zoomBy(-0.1); });
        add("Reset zoom\tCtrl+0",    [this] { zoomBy(0.0, true); });
        m.addSeparator();
        add(isFullScreen() ? "Exit full screen\tF11" : "Full screen\tF11",
            [this] { toggleFullscreen(); });
        m.addSeparator();
        add("Exit\tCtrl+Q",          [this] { close(); });
        QPoint pt = menuBtn_->mapToGlobal(QPoint(menuBtn_->width(), menuBtn_->height() + 6));
        pt.setX(pt.x() - m.sizeHint().width());
        m.exec(pt);
    }

    QWebEngineProfile* profile_ = nullptr;
    TabStrip* tabs_ = nullptr;
    Panel* toolbar_ = nullptr;
    LoadBar* bar_ = nullptr;
    IconButton *back_ = nullptr, *fwd_ = nullptr, *reload_ = nullptr,
               *homeBtn_ = nullptr, *full_ = nullptr, *menuBtn_ = nullptr;
    DownloadButton* dlBtn_ = nullptr;
    DownloadPanel*  dlPanel_ = nullptr;
    FindBar*        findBar_ = nullptr;
    QWebEngineProfile* privateProfile_ = nullptr;
    bool               proxyActive_ = false;
    AddressEdit* addr_ = nullptr;
    UrlAutocomplete* autocomplete_ = nullptr;
    BookmarksBar* bookmarksBar_ = nullptr;
    CommandPalette* commandPalette_ = nullptr;
    ShortcutHelp* shortcutHelp_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    HomePage* home_ = nullptr;
    QShortcut* escSc_ = nullptr;
    QTimer* noteTimer_ = nullptr;
    QList<TabData> data_;
    int cur_ = 0;
    QVBoxLayout* root_ = nullptr;
    static constexpr int kEdge = 5;
    bool wasMax_ = false, webFull_ = false, prevFull_ = false;
};

// ----------------------------------------------------------------------------
int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    // Initialize download notifications
    DownloadNotification::instance().init(nullptr);

    // Custom in-app toast manager — callbacks routed to DownloadManager
    // (Parent window set later via reposition())
    DownloadToastManager::instance().onOpen = [](const QString& id){
        DownloadManager::instance().openFile(id);
    };
    DownloadToastManager::instance().onShowFolder = [](const QString& id){
        DownloadManager::instance().openFolder(id);
    };
    DownloadNotification::instance().onOpen = [](const QString& id){
        DownloadManager::instance().openFile(id);
    };
    DownloadNotification::instance().onShowFolder = [](const QString& id){
        DownloadManager::instance().openFolder(id);
    };
    app.setApplicationName(kAppName);
    app.setStyle("Fusion");

    QFont f = app.font();
    f.setFamilies({"Segoe UI Variable Text", "Segoe UI", "Inter", "Ubuntu",
                   "Noto Sans", "Cantarell", "DejaVu Sans"});
    f.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(f);

    QPalette pal;
    pal.setColor(QPalette::Window,          QColor("#0c0c0e"));
    pal.setColor(QPalette::WindowText,      QColor("#e6e8ec"));
    pal.setColor(QPalette::Base,            QColor("#121316"));
    pal.setColor(QPalette::AlternateBase,   QColor("#1c1d21"));
    pal.setColor(QPalette::Text,            QColor("#e6e8ec"));
    pal.setColor(QPalette::Button,          QColor("#1c1d21"));
    pal.setColor(QPalette::ButtonText,      QColor("#e6e8ec"));
    pal.setColor(QPalette::Highlight,       QColor("#59627a"));
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::ToolTipBase,     QColor("#1c1d21"));
    pal.setColor(QPalette::ToolTipText,     QColor("#dcdee4"));
    pal.setColor(QPalette::PlaceholderText, QColor(160, 166, 182, 130));
    app.setPalette(pal);

    app.setStyleSheet(
        "QToolTip{background:#1c1d21;color:#dcdee4;border:1px solid #2c2e34;padding:5px 8px;}"
        "QMenu{background:#1c1d21;color:#d7d9de;border:1px solid #2c2e34;padding:6px;border-radius:10px;}"
        "QMenu::item{padding:8px 30px 8px 34px;color:#d7d9de;border-radius:6px;}"
        "QMenu::item:selected{background:#2b2d34;color:#ffffff;}"
        "QMenu::item:disabled{color:#5c606b;}"
        "QMenu::separator{height:1px;background:#2c2e34;margin:6px 8px;}"
        "QMenu::right-arrow{width:8px;height:8px;margin-right:10px;}"
        "QMenu::indicator{width:0;}");

    Browser w;
    w.show();
    if (argc > 1) w.openUrl(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
