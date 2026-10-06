// ============================================================================
//  shortcuthelp.cpp — Keyboard shortcut cheat sheet.
// ============================================================================

#include "shortcuthelp.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QVariantAnimation>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QFontMetrics>
#include <cmath>

// ============================================================================
//  Palette
// ============================================================================
namespace HelpCol {
    const QColor backdrop      = QColor(0, 0, 0, 160);
    const QColor cardBg        = QColor("#141519");
    const QColor cardBgGradTop = QColor("#17181c");
    const QColor border        = QColor("#2e3138");
    const QColor borderStrong  = QColor("#3a3d45");

    const QColor textBright    = QColor("#f4f5f7");
    const QColor textPrimary   = QColor("#e6e8ec");
    const QColor textMuted     = QColor("#8a90a0");
    const QColor textFaint     = QColor("#5c606b");

    const QColor searchBg      = QColor("#1c1d21");
    const QColor searchBorder  = QColor("#2c2e34");
    const QColor searchFocus   = QColor("#5d9df1");

    const QColor catLabel      = QColor("#7c8296");
    const QColor kbdBg         = QColor("#22242b");
    const QColor kbdBgGradTop  = QColor("#2a2d36");
    const QColor kbdBorder     = QColor("#3a3d45");
    const QColor kbdText       = QColor("#d0d4dc");

    const QColor divider       = QColor("#23262c");
}

// ============================================================================
//  Helper: format keys into display
// ============================================================================
static QStringList splitKeys(const QString& seq) {
    // "Ctrl+Shift+T" → {"Ctrl", "Shift", "T"}
    QStringList parts;
    QString cur;
    for (QChar c : seq) {
        if (c == '+') {
            parts << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.isEmpty()) parts << cur;
    return parts;
}

// ============================================================================
//  ShortcutHelp
// ============================================================================
ShortcutHelp::ShortcutHelp(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);   // overlay child, not a separate window
    setAttribute(Qt::WA_StyledBackground, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    buildData();
    buildUi();

    hide();
}

ShortcutHelp::~ShortcutHelp() = default;

// ────────────────────────────────────────────────────────────────────────────
//  Build shortcut data
// ────────────────────────────────────────────────────────────────────────────
void ShortcutHelp::buildData() {
    categories_.clear();

    // ── Navigation
    {
        Category c;
        c.name = QStringLiteral("Navigation");
        c.items = {
            { "Focus address bar",  {"Ctrl+L"},                    c.name },
            { "Back",               {"Alt+Left"},                  c.name },
            { "Forward",            {"Alt+Right"},                 c.name },
            { "Reload",             {"F5"},                        c.name },
            { "Reload (alt)",       {"Ctrl+R"},                    c.name },
            { "Home",               {"Alt+Home"},                  c.name },
        };
        categories_.push_back(c);
    }

    // ── Tabs
    {
        Category c;
        c.name = QStringLiteral("Tabs");
        c.items = {
            { "New tab",             {"Ctrl+T"},                    c.name },
            { "Close tab",           {"Ctrl+W"},                    c.name },
            { "Next tab",            {"Ctrl+Tab"},                  c.name },
            { "Previous tab",        {"Ctrl+Shift+Tab"},            c.name },
        };
        categories_.push_back(c);
    }

    // ── Zoom
    {
        Category c;
        c.name = QStringLiteral("Zoom");
        c.items = {
            { "Zoom in",             {"Ctrl++"},                    c.name },
            { "Zoom in (alt)",       {"Ctrl+="},                    c.name },
            { "Zoom out",            {"Ctrl+-"},                    c.name },
            { "Reset zoom",          {"Ctrl+0"},                    c.name },
        };
        categories_.push_back(c);
    }

    // ── Tools
    {
        Category c;
        c.name = QStringLiteral("Tools");
        c.items = {
            { "Find in page",        {"Ctrl+F"},                    c.name },
            { "Bookmarks page",      {"Ctrl+B"},                    c.name },
            { "Bookmark this page",  {"Ctrl+D"},                    c.name },
            { "Toggle bookmarks bar",{"Ctrl+Shift+B"},              c.name },
            { "History",             {"Ctrl+H"},                    c.name },
            { "Downloads",           {"Ctrl+J"},                    c.name },
            { "Settings",            {"Ctrl+,"},                    c.name },
        };
        categories_.push_back(c);
    }

    // ── Privacy
    {
        Category c;
        c.name = QStringLiteral("Privacy");
        c.items = {
            { "New private window (Tor)", {"Ctrl+Shift+N"},         c.name },
        };
        categories_.push_back(c);
    }

    // ── Window
    {
        Category c;
        c.name = QStringLiteral("Window");
        c.items = {
            { "Full screen",         {"F11"},                       c.name },
            { "Close window",        {"Ctrl+Q"},                    c.name },
            { "Escape",              {"Esc"},                       c.name },
            { "Show this help",      {"Ctrl+/"},                    c.name },
        };
        categories_.push_back(c);
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  Build UI
// ────────────────────────────────────────────────────────────────────────────
void ShortcutHelp::buildUi() {
    // ── Backdrop: full-size, transparent
    setStyleSheet("background:transparent;");

    // ── Card (centered)
    card_ = new QWidget(this);
    card_->setObjectName("helpCard");
    card_->setStyleSheet(QString(
        "#helpCard{"
        "  background:%1;"
        "  border:1px solid %2;"
        "  border-radius:16px;"
        "}")
        .arg(HelpCol::cardBg.name(), HelpCol::border.name()));

    auto* v = new QVBoxLayout(card_);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // ── Header
    {
        auto* header = new QWidget(card_);
        header->setFixedHeight(64);
        header->setStyleSheet(QString(
            "background:%1;"
            "border-top-left-radius:16px;"
            "border-top-right-radius:16px;"
            "border-bottom:1px solid %2;")
            .arg(HelpCol::cardBgGradTop.name(), HelpCol::divider.name()));

        auto* h = new QHBoxLayout(header);
        h->setContentsMargins(20, 0, 12, 0);

        auto* title = new QLabel(QStringLiteral("Keyboard Shortcuts"), header);
        QFont tf = title->font();
        tf.setPixelSize(15);
        tf.setWeight(QFont::DemiBold);
        title->setFont(tf);
        title->setStyleSheet(QString("color:%1;background:transparent;")
            .arg(HelpCol::textBright.name()));
        h->addWidget(title);
        h->addStretch(1);

        // Close button (×)
        auto* closeBtn = new QWidget(header);
        closeBtn->setFixedSize(28, 28);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setStyleSheet(
            "QWidget{background:transparent;border-radius:6px;}"
            "QWidget:hover{background:rgba(255,255,255,20);}");

        auto* closeBtnLayout = new QVBoxLayout(closeBtn);
        closeBtnLayout->setContentsMargins(0, 0, 0, 0);
        auto* closeLabel = new QLabel(QStringLiteral("×"), closeBtn);
        closeLabel->setAlignment(Qt::AlignCenter);
        QFont cf = closeLabel->font();
        cf.setPixelSize(20);
        closeLabel->setFont(cf);
        closeLabel->setStyleSheet(QString("color:%1;background:transparent;")
            .arg(HelpCol::textMuted.name()));
        closeBtnLayout->addWidget(closeLabel);

        closeBtn->installEventFilter(this);

        // Hook close button
        closeBtn->setProperty("isClose", true);

        h->addWidget(closeBtn);

        v->addWidget(header);
    }

    // ── Search
    {
        auto* searchWrap = new QWidget(card_);
        searchWrap->setFixedHeight(56);
        searchWrap->setStyleSheet(QString("background:%1;")
            .arg(HelpCol::cardBg.name()));

        auto* h = new QHBoxLayout(searchWrap);
        h->setContentsMargins(20, 12, 20, 8);

        search_ = new QLineEdit(searchWrap);
        search_->setPlaceholderText(QStringLiteral("Search shortcuts…"));
        search_->setFixedHeight(36);
        search_->setStyleSheet(QString(
            "QLineEdit{"
            "  background:%1;"
            "  border:1px solid %2;"
            "  border-radius:8px;"
            "  padding:0 12px;"
            "  color:%3;"
            "  font-size:13px;"
            "}"
            "QLineEdit:focus{"
            "  border-color:%4;"
            "}")
            .arg(HelpCol::searchBg.name(),
                 HelpCol::searchBorder.name(),
                 HelpCol::textPrimary.name(),
                 HelpCol::searchFocus.name()));
        h->addWidget(search_);

        QObject::connect(search_, &QLineEdit::textChanged, this,
                         [this](const QString&){ rebuildVisible(); });

        v->addWidget(searchWrap);
    }

    // ── Scrollable list
    {
        auto* scroll = new QScrollArea(card_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(
            "QScrollArea{background:transparent;border:none;}"
            "QScrollArea > QWidget > QWidget{background:transparent;}"
            "QScrollBar:vertical{background:transparent;width:8px;margin:4px 2px;}"
            "QScrollBar::handle:vertical{background:#242832;border-radius:4px;min-height:40px;}"
            "QScrollBar::handle:vertical:hover{background:#333846;}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");

        listContainer_ = new QWidget;
        listContainer_->setStyleSheet("background:transparent;");
        scroll->setWidget(listContainer_);

        v->addWidget(scroll, 1);
    }

    // ── Footer
    {
        auto* footer = new QWidget(card_);
        footer->setFixedHeight(48);
        footer->setStyleSheet(QString(
            "background:%1;"
            "border-top:1px solid %2;"
            "border-bottom-left-radius:16px;"
            "border-bottom-right-radius:16px;")
            .arg(HelpCol::cardBg.name(), HelpCol::divider.name()));

        auto* h = new QHBoxLayout(footer);
        h->setContentsMargins(20, 0, 20, 0);

        auto* lbl = new QLabel(QStringLiteral("Press Esc to close"), footer);
        QFont f = lbl->font();
        f.setPixelSize(11);
        lbl->setFont(f);
        lbl->setStyleSheet(QString("color:%1;background:transparent;")
            .arg(HelpCol::textFaint.name()));
        h->addWidget(lbl);

        h->addStretch(1);

        auto* countLbl = new QLabel(footer);
        countLbl->setObjectName("countLabel");
        QFont cf = countLbl->font();
        cf.setPixelSize(11);
        countLbl->setFont(cf);
        countLbl->setStyleSheet(QString("color:%1;background:transparent;")
            .arg(HelpCol::textFaint.name()));
        h->addWidget(countLbl);

        v->addWidget(footer);
    }

    // ── Initial render
    rebuildVisible();
}

// ────────────────────────────────────────────────────────────────────────────
//  Rebuild visible list based on search filter
// ────────────────────────────────────────────────────────────────────────────
void ShortcutHelp::rebuildVisible() {
    if (!listContainer_) return;

    // Delete old children
    QLayoutItem* item;
    if (auto* oldL = listContainer_->layout()) {
        while ((item = oldL->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        delete oldL;
    }

    auto* v = new QVBoxLayout(listContainer_);
    v->setContentsMargins(20, 12, 20, 16);
    v->setSpacing(4);

    const QString query = search_ ? search_->text().trimmed().toLower() : QString();

    int totalVisible = 0;

    for (const Category& cat : categories_) {
        // Filter items
        QVector<Shortcut> visible;
        for (const Shortcut& s : cat.items) {
            if (matchesFilter(s, query))
                visible.push_back(s);
        }
        if (visible.isEmpty()) continue;

        // Category header
        auto* catLabel = new QLabel(cat.name.toUpper(), listContainer_);
        QFont cf = catLabel->font();
        cf.setPixelSize(10);
        cf.setWeight(QFont::Bold);
        cf.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        catLabel->setFont(cf);
        catLabel->setStyleSheet(QString(
            "color:%1;background:transparent;padding-top:8px;")
            .arg(HelpCol::catLabel.name()));
        v->addWidget(catLabel);

        // Separator under category
        auto* sep = new QFrame(listContainer_);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet(QString(
            "background:%1;max-height:1px;")
            .arg(HelpCol::divider.name()));
        sep->setFixedHeight(1);
        v->addWidget(sep);

        // Items
        for (const Shortcut& s : visible) {
            auto* row = new QWidget(listContainer_);
            row->setFixedHeight(36);
            row->setStyleSheet("background:transparent;");

            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(4, 0, 4, 0);
            h->setSpacing(8);

            // Description
            auto* desc = new QLabel(s.description, row);
            QFont df = desc->font();
            df.setPixelSize(12.5);
            desc->setFont(df);
            desc->setStyleSheet(QString(
                "color:%1;background:transparent;")
                .arg(HelpCol::textPrimary.name()));
            h->addWidget(desc);
            h->addStretch(1);

            // Keys as kbd chips
            const QStringList keys = splitKeys(s.keys.value(0));
            for (int i = 0; i < keys.size(); ++i) {
                auto* kbd = new QLabel(keys[i], row);
                QFont kf = kbd->font();
                kf.setPixelSize(11);
                kf.setWeight(QFont::DemiBold);
                kbd->setFont(kf);
                kbd->setAlignment(Qt::AlignCenter);
                kbd->setMinimumWidth(28);
                kbd->setFixedHeight(22);
                kbd->setStyleSheet(QString(
                    "QLabel{"
                    "  background:qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                    "                              stop:0 %1, stop:1 %2);"
                    "  border:1px solid %3;"
                    "  border-bottom-width:2px;"
                    "  border-radius:5px;"
                    "  padding:0 8px;"
                    "  color:%4;"
                    "}")
                    .arg(HelpCol::kbdBgGradTop.name(),
                         HelpCol::kbdBg.name(),
                         HelpCol::kbdBorder.name(),
                         HelpCol::kbdText.name()));
                h->addWidget(kbd);

                // "+" between keys
                if (i < keys.size() - 1) {
                    auto* plus = new QLabel(QStringLiteral("+"), row);
                    plus->setStyleSheet(QString("color:%1;background:transparent;")
                        .arg(HelpCol::textFaint.name()));
                    QFont pf = plus->font();
                    pf.setPixelSize(11);
                    plus->setFont(pf);
                    h->addWidget(plus);
                }
            }

            v->addWidget(row);
            totalVisible++;
        }

        v->addSpacing(8);
    }

    if (totalVisible == 0) {
        auto* empty = new QLabel(
            QStringLiteral("No shortcuts match your search."), listContainer_);
        empty->setAlignment(Qt::AlignCenter);
        QFont ef = empty->font();
        ef.setPixelSize(13);
        empty->setFont(ef);
        empty->setStyleSheet(QString(
            "color:%1;background:transparent;padding:40px 0;")
            .arg(HelpCol::textFaint.name()));
        v->addWidget(empty);
    }

    v->addStretch(1);

    // Update count in footer
    if (auto* cnt = card_->findChild<QLabel*>("countLabel")) {
        cnt->setText(QString("%1 shortcuts").arg(totalVisible));
    }
}

bool ShortcutHelp::matchesFilter(const Shortcut& s, const QString& q) const {
    if (q.isEmpty()) return true;
    return s.description.toLower().contains(q) ||
           s.keys.value(0).toLower().contains(q) ||
           s.category.toLower().contains(q);
}

// ────────────────────────────────────────────────────────────────────────────
//  Show / hide
// ────────────────────────────────────────────────────────────────────────────
void ShortcutHelp::open() {
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }

    // Center the card
    const int cardW = 640;
    const int cardH = 640;
    const int x = (width() - cardW) / 2;
    const int y = (height() - cardH) / 2;
    card_->setGeometry(x, y, cardW, cardH);

    // Focus search
    if (search_) {
        search_->clear();
        search_->setFocus();
    }

    // Rebuild with full list
    rebuildVisible();

    // Fade in
    show();
    raise();
    setFocus();

    if (!fadeAnim_) {
        fadeAnim_ = new QVariantAnimation(this);
        fadeAnim_->setDuration(180);
        fadeAnim_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(fadeAnim_, &QVariantAnimation::valueChanged,
                         this, [this](const QVariant& v){
            fadeOpacity_ = v.toReal();
            update();
        });
    }

    fadeAnim_->stop();
    fadeAnim_->setStartValue(0.0);
    fadeAnim_->setEndValue(1.0);
    fadeAnim_->start();
}

void ShortcutHelp::close() {
    if (!fadeAnim_) { hide(); return; }

    fadeAnim_->stop();
    fadeAnim_->setStartValue(fadeOpacity_);
    fadeAnim_->setEndValue(0.0);

    QObject::disconnect(fadeAnim_, &QVariantAnimation::finished, this, nullptr);
    QObject::connect(fadeAnim_, &QVariantAnimation::finished, this, [this]{
        hide();
    });
    fadeAnim_->start();
}

// ────────────────────────────────────────────────────────────────────────────
//  Events
// ────────────────────────────────────────────────────────────────────────────
void ShortcutHelp::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Backdrop
    QColor backdrop = HelpCol::backdrop;
    backdrop.setAlpha(int(backdrop.alpha() * fadeOpacity_));
    p.fillRect(rect(), backdrop);

    // If fading, apply opacity to card too
    if (fadeOpacity_ < 1.0) {
        p.setOpacity(fadeOpacity_);
    }

    // Card shadow (drawn by parent's paint)
    if (card_) {
        // Draw drop shadow behind the card
        for (int i = 8; i >= 1; --i) {
            QRectF cardR = card_->geometry().adjusted(-i, -i + 2, i, i + 2);
            QPainterPath path;
            path.addRoundedRect(cardR, 16 + i, 16 + i);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, int(30 * fadeOpacity_ / i)));
            p.drawPath(path);
        }
    }

    p.setOpacity(1.0);
}

void ShortcutHelp::mousePressEvent(QMouseEvent* e) {
    // Click outside card → close
    if (card_ && !card_->geometry().contains(e->pos())) {
        close();
    }
    QWidget::mousePressEvent(e);
}

void ShortcutHelp::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void ShortcutHelp::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (parentWidget() && card_) {
        const int cardW = 640;
        const int cardH = 640;
        const int x = (width() - cardW) / 2;
        const int y = (height() - cardH) / 2;
        card_->setGeometry(x, y, cardW, cardH);
    }
}


// ============================================================================
//  eventFilter — handles close button click
// ============================================================================
bool ShortcutHelp::eventFilter(QObject* obj, QEvent* ev) {
    // Close button click
    if (obj->property("isClose").toBool()) {
        if (ev->type() == QEvent::MouseButtonPress) {
            close();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}
