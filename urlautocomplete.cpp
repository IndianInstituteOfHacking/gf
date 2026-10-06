// ============================================================================
//  urlautocomplete.cpp — Chrome-quality URL autocomplete.
// ============================================================================

#include "urlautocomplete.h"
#include "bookmarkstore.h"
#include "historystore.h"

#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QUrl>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// ============================================================================
//  Palette (matches browser theme)
// ============================================================================
namespace AutoCol {
    const QColor bg           = QColor("#18191d");
    const QColor bgHover      = QColor("#22242b");
    const QColor bgSelected   = QColor("#2a2d36");
    const QColor border       = QColor("#2e3138");
    const QColor divider      = QColor("#23262c");

    const QColor textPrimary  = QColor("#e6e8ec");
    const QColor textMuted    = QColor("#8a90a0");
    const QColor textFaint    = QColor("#5c606b");

    const QColor accent       = QColor("#5d9df1");
    const QColor bookmark     = QColor("#5d9df1");
    const QColor history      = QColor("#8a90a0");
    const QColor search       = QColor("#4aaf7a");
    const QColor direct       = QColor("#f1c75c");
}

// ============================================================================
//  Icon painting (small, monochrome)
// ============================================================================
namespace AutoIcon {

enum class Kind { Bookmark, History, Search, Direct };

static void paint(QPainter& p, Kind k, const QRectF& r, const QColor& c) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = r.width();
    const QPointF ctr = r.center();
    p.setPen(QPen(c, std::max<qreal>(1.4, s * 0.11),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (k) {
    case Kind::Bookmark: {
        QPainterPath path;
        path.moveTo(ctr.x() - s * 0.18, ctr.y() - s * 0.24);
        path.lineTo(ctr.x() + s * 0.18, ctr.y() - s * 0.24);
        path.lineTo(ctr.x() + s * 0.18, ctr.y() + s * 0.26);
        path.lineTo(ctr.x(), ctr.y() + s * 0.10);
        path.lineTo(ctr.x() - s * 0.18, ctr.y() + s * 0.26);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Kind::History:
        p.drawEllipse(ctr, s * 0.26, s * 0.26);
        p.drawLine(QPointF(ctr.x(), ctr.y() - s * 0.14),
                   QPointF(ctr.x(), ctr.y() + s * 0.02));
        p.drawLine(QPointF(ctr.x(), ctr.y() + s * 0.02),
                   QPointF(ctr.x() + s * 0.12, ctr.y() + s * 0.10));
        break;
    case Kind::Search:
        p.drawEllipse(QPointF(ctr.x() - s * 0.06, ctr.y() - s * 0.06),
                      s * 0.20, s * 0.20);
        p.drawLine(QPointF(ctr.x() + s * 0.08, ctr.y() + s * 0.08),
                   QPointF(ctr.x() + s * 0.24, ctr.y() + s * 0.24));
        break;
    case Kind::Direct: {
        // Glowing dot
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawEllipse(ctr, s * 0.14, s * 0.14);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, 1.4));
        p.drawEllipse(ctr, s * 0.26, s * 0.26);
        break;
    }
    }
    p.restore();
}

} // namespace AutoIcon

// ============================================================================
//  UrlAutocomplete
// ============================================================================
UrlAutocomplete::UrlAutocomplete(QWidget* parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint |
                       Qt::NoDropShadowWindowHint |
                       Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);

    // Soft shadow effect via layered painting
    setFixedWidth(520);
    hide();
}

// ────────────────────────────────────────────────────────────────────────────
//  Attach to line edit
// ────────────────────────────────────────────────────────────────────────────
void UrlAutocomplete::attach(QLineEdit* edit) {
    edit_ = edit;
    if (!edit) return;

    // Text change → refresh suggestions
    QObject::connect(edit, &QLineEdit::textEdited, this, [this](const QString& t){
        showFor(t);
    });

    // When the address bar loses focus (user clicks elsewhere), hide dropdown
    QObject::connect(edit, &QLineEdit::editingFinished, this, [this]{
        QTimer::singleShot(150, this, [this]{
            if (!underMouse()) hideDropdown();
        });
    });

    // Do NOT install event filter here — handleKeyPress is invoked from main.cpp
}

// ────────────────────────────────────────────────────────────────────────────
//  Show suggestions
// ────────────────────────────────────────────────────────────────────────────
void UrlAutocomplete::showFor(const QString& text) {
    if (!edit_) return;
    const QString q = text.trimmed();

    if (q.isEmpty()) { hideDropdown(); return; }

    generateSuggestions(q);
    rankAndTrim(kMaxItems);

    if (suggestions_.isEmpty()) { hideDropdown(); return; }

    selectedIndex_ = -1;
    hoverIndex_ = -1;

    rebuildLayout();

    // Position dropdown just below edit_
    QPoint editBottomLeft = edit_->mapToGlobal(QPoint(0, edit_->height() + 4));
    QRect available = QGuiApplication::screenAt(editBottomLeft)
                        ? QGuiApplication::screenAt(editBottomLeft)->availableGeometry()
                        : QRect(0, 0, 1920, 1080);

    int x = editBottomLeft.x();
    int y = editBottomLeft.y();

    if (theme_ == Theme::HomePage) {
        // Home page search: match search bar width, slight offset
        int w = edit_->width();
        // Clamp
        if (w > 720) w = 720;
        // Center align under search bar
        x = editBottomLeft.x() + (edit_->width() - w) / 2;
        setFixedWidth(w);
    } else {
        int w = (std::min)(edit_->width(), 720);
        setFixedWidth(w);
    }

    int h = height();

    // Clamp to screen
    if (x + width() > available.right() - 8)
        x = available.right() - width() - 8;
    if (x < available.left() + 8)
        x = available.left() + 8;
    if (y + h > available.bottom() - 8) {
        // Flip above edit
        y = edit_->mapToGlobal(QPoint(0, -h - 4)).y();
    }

    move(x, y);
    show();
    raise();
}

void UrlAutocomplete::hideDropdown() {
    suggestions_.clear();
    selectedIndex_ = -1;
    hoverIndex_ = -1;
    hide();
}

QString UrlAutocomplete::selectedUrl() const {
    if (selectedIndex_ < 0 || selectedIndex_ >= int(suggestions_.size()))
        return QString();
    return suggestions_[selectedIndex_].url;
}

// ============================================================================
//  Suggestion generation
// ============================================================================
void UrlAutocomplete::generateSuggestions(const QString& query) {
    suggestions_.clear();

    // Order matters: higher-priority first
    addFromBookmarks(query);
    addFromHistory(query);
    addSearchSuggestion(query);
    addDirectUrl(query);
}

// ────────────────────────────────────────────────────────────────────────────
//  From bookmarks
// ────────────────────────────────────────────────────────────────────────────
void UrlAutocomplete::addFromBookmarks(const QString& query) {
    auto& store = BookmarkStore::instance();
    const QString q = query.toLower();

    for (const Bookmark& bm : store.all()) {
        const QString url   = bm.url;
        const QString title = bm.title.isEmpty() ? url : bm.title;

        bool match = false;
        if (title.toLower().contains(q)) match = true;
        if (url.toLower().contains(q)) match = true;
        if (!match) continue;

        UrlSuggestion s;
        s.type = UrlSuggestion::Bookmark;
        s.url = url;
        s.title = title;
        s.subtitle = QStringLiteral("Bookmark");
        s.score = 1000;  // top priority

        // Bonus if query is a prefix of the title
        if (title.toLower().startsWith(q)) s.score += 500;

        suggestions_.push_back(s);
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  From history
// ────────────────────────────────────────────────────────────────────────────
void UrlAutocomplete::addFromHistory(const QString& query) {
    auto& store = HistoryStore::instance();
    const QString q = query.toLower();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // Frequency map (host → best entry)
    QHash<QString, UrlSuggestion> best;

    for (const HistoryEntry& e : store.all()) {
        const QString url   = e.url;
        const QString title = e.title.isEmpty() ? url : e.title;

        // Skip internal pages
        if (url.startsWith("rootbrowser://") ||
            url.startsWith("about:") ||
            url.startsWith("data:"))
            continue;

        bool match = false;
        if (title.toLower().contains(q)) match = true;
        if (url.toLower().contains(q))   match = true;
        if (!match) continue;

        UrlSuggestion s;
        s.type = UrlSuggestion::History;
        s.url = url;
        s.title = title;

        // Compute subtitle with visit count + recency
        const qint64 age = now - e.visitedAt;
        QString when;
        if (age < 60) when = "just now";
        else if (age < 3600) when = QString("%1 min ago").arg(age / 60);
        else if (age < 86400) when = QString("%1 hr ago").arg(age / 3600);
        else if (age < 604800) when = QString("%1 days ago").arg(age / 86400);
        else when = QDateTime::fromSecsSinceEpoch(e.visitedAt).toString("MMM d");

        const int visits = e.visitCount;
        s.subtitle = visits > 1
            ? QString("%1 visits · %2").arg(visits).arg(when)
            : when;

        // Score: recency + frequency
        s.score = 100;
        s.score += std::min(500, visits * 20);           // frequency bonus (max 500)
        s.score += std::max(0, 200 - int(age / 3600));    // recency bonus (max 200)
        if (title.toLower().startsWith(q)) s.score += 300;

        // Keep the highest-scoring entry per URL
        auto it = best.find(url);
        if (it == best.end() || it->score < s.score)
            best.insert(url, s);
    }

    for (const auto& s : best)
        suggestions_.push_back(s);
}

// ────────────────────────────────────────────────────────────────────────────
//  Search suggestion (fallback)
// ────────────────────────────────────────────────────────────────────────────
void UrlAutocomplete::addSearchSuggestion(const QString& query) {
    // Skip if query looks like a URL
    const bool isUrl = query.contains('.') && !query.contains(' ');
    if (isUrl) return;

    UrlSuggestion s;
    s.type = UrlSuggestion::Search;
    s.url = QString("https://duckduckgo.com/?q=%1")
                .arg(QString::fromUtf8(QUrl::toPercentEncoding(query)));
    s.title = query;
    s.subtitle = QStringLiteral("Search with DuckDuckGo");
    s.score = 50;

    suggestions_.push_back(s);
}

// ────────────────────────────────────────────────────────────────────────────
//  Direct URL suggestion
// ────────────────────────────────────────────────────────────────────────────
void UrlAutocomplete::addDirectUrl(const QString& query) {
    if (query.isEmpty()) return;

    // Normalize
    QString normalized = query;
    if (!normalized.contains("://") &&
        normalized.contains('.') &&
        !normalized.contains(' '))
    {
        normalized = "https://" + normalized;
    } else if (normalized.contains(' ') || !normalized.contains('.')) {
        return;  // not a URL
    } else {
        return;  // already has scheme, keep as-is in suggestions if not duplicate
    }

    // Skip if we already have this URL as top suggestion
    for (const auto& s : suggestions_) {
        if (s.url == normalized && s.type == UrlSuggestion::History)
            return;
    }

    UrlSuggestion s;
    s.type = UrlSuggestion::Direct;
    s.url = normalized;
    s.title = normalized;
    s.subtitle = QStringLiteral("Open this URL");
    s.score = 200;   // high priority — user typed a URL
    suggestions_.push_back(s);
}

// ============================================================================
//  Ranking + dedup
// ============================================================================
void UrlAutocomplete::rankAndTrim(int maxItems) {
    // Dedupe by URL (keep highest score)
    QHash<QString, UrlSuggestion> uniq;
    for (const auto& s : suggestions_) {
        auto it = uniq.find(s.url);
        if (it == uniq.end() || it->score < s.score)
            uniq.insert(s.url, s);
    }
    suggestions_.clear();
    for (const auto& s : uniq)
        suggestions_.push_back(s);

    // Sort by score desc
    std::sort(suggestions_.begin(), suggestions_.end(),
              [](const UrlSuggestion& a, const UrlSuggestion& b){
        return a.score > b.score;
    });

    // Trim
    if (int(suggestions_.size()) > maxItems)
        suggestions_.resize(maxItems);
}

// ============================================================================
//  Layout
// ============================================================================
void UrlAutocomplete::rebuildLayout() {
    const int h = kPadding * 2 + suggestions_.size() * kItemHeight;
    setFixedHeight(h);
    update();
}

QRect UrlAutocomplete::itemRect(int index) const {
    return QRect(0,
                 kPadding + index * kItemHeight,
                 width(),
                 kItemHeight);
}

int UrlAutocomplete::itemAt(int y) const {
    const int innerY = y - kPadding;
    if (innerY < 0) return -1;
    const int idx = innerY / kItemHeight;
    if (idx < 0 || idx >= int(suggestions_.size())) return -1;
    return idx;
}

// ============================================================================
//  Paint
// ============================================================================
void UrlAutocomplete::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Card — theme-aware
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kBorderRadius, kBorderRadius);

    if (theme_ == Theme::HomePage) {
        // Glass-style for home page (matches search box)
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0.0, QColor(20, 24, 32, 235));
        g.setColorAt(1.0, QColor(15, 18, 25, 245));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(path);

        // Brighter border to match home search bar
        p.setPen(QPen(QColor(74, 78, 90, 180), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        // Subtle inner highlight
        p.setPen(QPen(QColor(255, 255, 255, 12), 1));
        p.drawLine(QPointF(r.left() + 16, r.top() + 1),
                   QPointF(r.right() - 16, r.top() + 1));
    } else {
        // Standard graphite card
        p.setPen(Qt::NoPen);
        p.setBrush(AutoCol::bg);
        p.drawPath(path);
        p.setPen(QPen(AutoCol::border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Items
    QFont titleFont = font();
    titleFont.setPixelSize(13);

    QFont subtitleFont = font();
    subtitleFont.setPixelSize(11);

    for (int i = 0; i < int(suggestions_.size()); ++i) {
        const auto& s = suggestions_[i];
        const QRect item = itemRect(i);

        // Divider between items (skip first)
        if (i > 0) {
            p.setPen(QPen(AutoCol::divider, 1));
            p.drawLine(item.left() + kPadding,
                       item.top(),
                       item.right() - kPadding,
                       item.top());
        }

        // Hover / selection background
        const bool isSelected = (i == selectedIndex_);
        const bool isHover    = (i == hoverIndex_);
        if (isSelected || isHover) {
            QRectF bgRect = item.adjusted(kPadding - 2, 2, -(kPadding - 2), -2);
            p.setPen(Qt::NoPen);
            if (theme_ == Theme::HomePage) {
                // Slightly brighter for home page
                QLinearGradient hg(bgRect.topLeft(), bgRect.bottomRight());
                hg.setColorAt(0.0, isSelected ? QColor(42, 46, 56) : QColor(32, 36, 46));
                hg.setColorAt(1.0, isSelected ? QColor(34, 38, 48) : QColor(26, 30, 38));
                p.setBrush(hg);
            } else {
                p.setBrush(isSelected ? AutoCol::bgSelected : AutoCol::bgHover);
            }
            p.drawRoundedRect(bgRect, 8, 8);
        }

        // Icon
        QColor iconColor;
        AutoIcon::Kind iconKind = AutoIcon::Kind::History;
        switch (s.type) {
        case UrlSuggestion::Bookmark:
            iconKind = AutoIcon::Kind::Bookmark;
            iconColor = AutoCol::bookmark;
            break;
        case UrlSuggestion::History:
            iconKind = AutoIcon::Kind::History;
            iconColor = AutoCol::history;
            break;
        case UrlSuggestion::Search:
            iconKind = AutoIcon::Kind::Search;
            iconColor = AutoCol::search;
            break;
        case UrlSuggestion::Direct:
            iconKind = AutoIcon::Kind::Direct;
            iconColor = AutoCol::direct;
            break;
        }

        const QRectF iconRect(kPadding + 6,
                              item.center().y() - kIconSize / 2.0,
                              kIconSize, kIconSize);
        AutoIcon::paint(p, iconKind, iconRect.adjusted(4, 4, -4, -4), iconColor);

        // Title (with subtle match highlight)
        const int textX = int(iconRect.right()) + 14;
        const int textW = item.right() - textX - 16;

        p.setFont(titleFont);
        p.setPen(AutoCol::textPrimary);

        // Elide title
        QFontMetrics fmT(titleFont);
        const QString elidedTitle = fmT.elidedText(s.title, Qt::ElideRight, textW);

        const int titleY = item.top() + 10;
        p.drawText(QRect(textX, titleY, textW, 18),
                   Qt::AlignVCenter | Qt::AlignLeft, elidedTitle);

        // Subtitle
        p.setFont(subtitleFont);
        p.setPen(AutoCol::textMuted);

        QFontMetrics fmS(subtitleFont);
        const QString subtitle = (s.type == UrlSuggestion::History ||
                                  s.type == UrlSuggestion::Bookmark)
                                    ? s.subtitle + " · " + s.url
                                    : s.subtitle;
        const QString elidedSub = fmS.elidedText(subtitle, Qt::ElideMiddle, textW);

        p.drawText(QRect(textX, titleY + 18, textW, 16),
                   Qt::AlignVCenter | Qt::AlignLeft, elidedSub);
    }
}

// ============================================================================
//  Mouse events
// ============================================================================
void UrlAutocomplete::mouseMoveEvent(QMouseEvent* e) {
    const int idx = itemAt(int(e->position().y()));
    if (idx != hoverIndex_) {
        hoverIndex_ = idx;
        update();
    }
}

void UrlAutocomplete::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int idx = itemAt(int(e->position().y()));
    if (idx < 0) return;
    selectedIndex_ = idx;
    update();
}

void UrlAutocomplete::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int idx = itemAt(int(e->position().y()));
    if (idx < 0 || idx != selectedIndex_) return;

    // Commit selection
    const QString url = suggestions_[idx].url;
    if (edit_) {
        edit_->setText(url);
        // Move cursor to end
        edit_->setCursorPosition(url.length());
    }
    hideDropdown();
}

void UrlAutocomplete::leaveEvent(QEvent*) {
    hoverIndex_ = -1;
    update();
}

bool UrlAutocomplete::event(QEvent* e) {
    // Dropdown is non-focusable; do not react to focus events
    return QWidget::event(e);
}

// ============================================================================
//  Keyboard navigation
// ============================================================================
bool UrlAutocomplete::handleKeyPress(int key, int modifiers) {
    if (!isVisible() || suggestions_.isEmpty()) return false;

    switch (key) {
    case Qt::Key_Down:
        selectedIndex_ = (std::min)(selectedIndex_ + 1,
                                    int(suggestions_.size()) - 1);
        if (selectedIndex_ < 0) selectedIndex_ = 0;
        hoverIndex_ = selectedIndex_;
        update();
        return true;

    case Qt::Key_Up:
        selectedIndex_ = std::max(selectedIndex_ - 1, -1);
        hoverIndex_ = selectedIndex_;
        update();
        return true;

    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (selectedIndex_ >= 0 && selectedIndex_ < int(suggestions_.size())) {
            const QString url = suggestions_[selectedIndex_].url;
            if (edit_) {
                edit_->setText(url);
                edit_->setCursorPosition(url.length());
            }
            hideDropdown();
            return true;   // consumed
        }
        return false;

    case Qt::Key_Escape:
        hideDropdown();
        return true;

    case Qt::Key_Tab:
        // Autocomplete to first suggestion (without navigating)
        if (!suggestions_.isEmpty()) {
            const QString url = suggestions_.first().url;
            if (edit_) {
                edit_->setText(url);
                edit_->setCursorPosition(url.length());
            }
            selectedIndex_ = 0;
            hoverIndex_ = 0;
            update();
            return true;
        }
        return false;
    }
    return false;
}
