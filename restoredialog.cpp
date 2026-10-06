// ============================================================================
//  restoredialog.cpp — Beautiful frameless restore prompt.
// ============================================================================

#include "restoredialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QDateTime>
#include <QUrl>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QVariantAnimation>

// ────────────────────────────────────────────────────────────────────────────
//  Palette (matching browser theme)
// ────────────────────────────────────────────────────────────────────────────
namespace Col {
    const QColor bg          = QColor("#17181b");
    const QColor border      = QColor("#2c2e34");
    const QColor borderGlow  = QColor("#3a3f4c");
    const QColor textBright  = QColor("#f5f6f8");
    const QColor text        = QColor("#d0d4dc");
    const QColor textMuted   = QColor("#8a90a0");
    const QColor textFaint   = QColor("#5c606b");
    const QColor accent      = QColor("#5d9df1");
    const QColor accentHi    = QColor("#7ab0ff");
    const QColor surface     = QColor("#1c1d21");
    const QColor surfaceHi   = QColor("#23262c");
}

// ────────────────────────────────────────────────────────────────────────────
static QString humanAgo(qint64 sec) {
    if (sec <= 0) return QStringLiteral("just now");
    if (sec < 60) return QStringLiteral("just now");
    if (sec < 3600) return QStringLiteral("%1 min ago").arg(sec / 60);
    if (sec < 86400) return QStringLiteral("%1 hr ago").arg(sec / 3600);
    return QStringLiteral("%1 days ago").arg(sec / 86400);
}

// ────────────────────────────────────────────────────────────────────────────
//  Custom frameless button with icon
// ────────────────────────────────────────────────────────────────────────────
namespace BtnIcon {
enum class Kind { Close, Check };

static void paint(QPainter& p, Kind k, const QRectF& r, const QColor& c) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = qMin(r.width(), r.height());
    const QPointF ctr = r.center();
    p.setPen(QPen(c, qMax<qreal>(1.6, s * 0.14),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (k == Kind::Close) {
        p.drawLine(QPointF(ctr.x() - s * 0.18, ctr.y() - s * 0.18),
                   QPointF(ctr.x() + s * 0.18, ctr.y() + s * 0.18));
        p.drawLine(QPointF(ctr.x() + s * 0.18, ctr.y() - s * 0.18),
                   QPointF(ctr.x() - s * 0.18, ctr.y() + s * 0.18));
    } else {
        p.drawLine(QPointF(ctr.x() - s * 0.20, ctr.y()),
                   QPointF(ctr.x() - s * 0.05, ctr.y() + s * 0.16));
        p.drawLine(QPointF(ctr.x() - s * 0.05, ctr.y() + s * 0.16),
                   QPointF(ctr.x() + s * 0.22, ctr.y() - s * 0.18));
    }
    p.restore();
}
}

// ────────────────────────────────────────────────────────────────────────────
//  Custom button with hover animation
// ────────────────────────────────────────────────────────────────────────────
class ModernButton : public QPushButton {
public:
    ModernButton(const QString& text, bool primary, QWidget* parent = nullptr)
        : QPushButton(text, parent), primary_(primary)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedHeight(38);
        setMinimumWidth(140);

        anim_ = new QVariantAnimation(this);
        anim_->setDuration(140);
        anim_->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(anim_, &QVariantAnimation::valueChanged,
                         this, [this](const QVariant& v){
            hoverT_ = v.toReal();
            update();
        });
    }

protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::Enter) animate(1.0);
        else if (e->type() == QEvent::Leave) animate(0.0);
        return QPushButton::event(e);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool press = isDown();
        const qreal t = hoverT_;

        // Background gradient
        QColor bg, border, fg;
        if (primary_) {
            const QColor base = Col::accent;
            const QColor hi = Col::accentHi;
            bg = press ? base.darker(120)
                       : QColor::fromRgbF(
                            base.redF()   + (hi.redF() - base.redF()) * t,
                            base.greenF() + (hi.greenF() - base.greenF()) * t,
                            base.blueF()  + (hi.blueF() - base.blueF()) * t);
            border = bg;
            fg = Qt::white;
        } else {
            const QColor base = Col::surface;
            const QColor hi = Col::surfaceHi;
            bg = press ? base.darker(120)
                       : QColor::fromRgbF(
                            base.redF()   + (hi.redF() - base.redF()) * t,
                            base.greenF() + (hi.greenF() - base.greenF()) * t,
                            base.blueF()  + (hi.blueF() - base.blueF()) * t);
            border = Col::border;
            fg = Col::textBright;
        }

        QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(QPen(border, 1));
        p.setBrush(bg);
        p.drawRoundedRect(r, 10, 10);

        // Icon + text
        const qreal iconSize = 14;
        const qreal gap = 8;

        QFontMetrics fm(font());
        const int textW = fm.horizontalAdvance(text());
        const int totalW = int(iconSize + gap + textW);
        const qreal startX = (width() - totalW) / 2.0;

        // Icon
        BtnIcon::Kind k = primary_ ? BtnIcon::Kind::Check : BtnIcon::Kind::Close;
        QRectF iconRect(startX, (height() - iconSize) / 2.0, iconSize, iconSize);
        BtnIcon::paint(p, k, iconRect, fg);

        // Text
        QFont f = font();
        f.setPixelSize(13);
        f.setWeight(primary_ ? QFont::DemiBold : QFont::Normal);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(startX + iconSize + gap, 0,
                          width() - startX - iconSize - gap, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    void animate(qreal v) {
        anim_->stop();
        anim_->setStartValue(hoverT_);
        anim_->setEndValue(v);
        anim_->start();
    }

    bool primary_;
    qreal hoverT_ = 0.0;
    QVariantAnimation* anim_ = nullptr;
};

// ============================================================================
//  RestoreDialog
// ============================================================================
RestoreDialog::RestoreDialog(const QVector<QPair<QString, QString>>& tabs,
                             qint64 savedAt,
                             QWidget* parent)
    : QDialog(parent), tabs_(tabs), savedAt_(savedAt)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint |
                   Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(460);

    buildUi();

    // Center on screen
    if (auto* s = QGuiApplication::primaryScreen()) {
        const QRect g = s->availableGeometry();
        move(g.center().x() - width() / 2,
             g.center().y() - height() / 2);
    }
}

void RestoreDialog::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Content card
    auto* card = new QWidget(this);
    outer->addWidget(card);

    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(32, 28, 32, 24);
    v->setSpacing(0);

    // ── Header row (icon + title)
    {
        auto* hrow = new QHBoxLayout;
        hrow->setSpacing(14);

        // Rotate icon
        auto* icon = new QLabel(card);
        icon->setFixedSize(44, 44);
        icon->setPixmap([]() {
            QPixmap pm(88, 88);
            pm.setDevicePixelRatio(2.0);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Col::accent, 2.2, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QRectF r(14, 14, 32, 32);
            p.drawArc(r, 70 * 16, 250 * 16);
            QPointF a[3] = {
                QPointF(38, 14), QPointF(48, 22), QPointF(34, 26)
            };
            p.drawPolyline(a, 3);
            p.end();
            return pm;
        }());
        hrow->addWidget(icon);

        auto* txtCol = new QVBoxLayout;
        txtCol->setSpacing(2);

        auto* h1 = new QLabel(QStringLiteral("Restore your session?"), card);
        QFont hf = h1->font();
        hf.setPixelSize(18);
        hf.setWeight(QFont::DemiBold);
        h1->setFont(hf);
        h1->setStyleSheet(QString("color:%1;").arg(Col::textBright.name()));
        txtCol->addWidget(h1);

        auto* sub = new QLabel(
            QString("%1 tabs were open when you closed RootBrowser.")
                .arg(tabs_.size()),
            card);
        QFont sf = sub->font();
        sf.setPixelSize(12);
        sub->setFont(sf);
        sub->setStyleSheet(QString("color:%1;").arg(Col::textMuted.name()));
        txtCol->addWidget(sub);

        hrow->addLayout(txtCol, 1);
        v->addLayout(hrow);
    }

    v->addSpacing(20);

    // ── Tabs preview list
    {
        auto* listBox = new QLabel(card);
        listBox->setTextFormat(Qt::RichText);
        listBox->setStyleSheet(QString(
            "QLabel{"
            "background:%1;"
            "border:1px solid %2;"
            "border-radius:10px;"
            "padding:10px 14px;"
            "}")
            .arg(Col::surface.name(), Col::border.name()));

        // Build rich text list
        QString html;
        const int kMaxShow = 6;
        const int showN = qMin(tabs_.size(), kMaxShow);

        for (int i = 0; i < showN; ++i) {
            const QString title = tabs_[i].first.isEmpty()
                ? tabs_[i].second
                : tabs_[i].first;

            QString safeTitle = title.toHtmlEscaped();
            // Elide to keep clean
            if (safeTitle.length() > 48)
                safeTitle = safeTitle.left(48) + "…";

            html += QString(
                "<div style='margin:3px 0;'>"
                "<span style='color:%1;font-size:13px;'>●</span>"
                "<span style='color:%2;font-size:12px;margin-left:10px;'>%3</span>"
                "</div>")
                .arg(Col::accent.name(), Col::text.name(), safeTitle);
        }

        if (tabs_.size() > kMaxShow) {
            html += QString(
                "<div style='margin:8px 0 3px;'>"
                "<span style='color:%1;font-size:11px;font-style:italic;'>"
                "+ %2 more tab(s)"
                "</span></div>")
                .arg(Col::textFaint.name())
                .arg(tabs_.size() - kMaxShow);
        }

        listBox->setText(html);
        listBox->setWordWrap(true);
        v->addWidget(listBox);
    }

    v->addSpacing(14);

    // ── Last saved timestamp
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 diff = (savedAt_ > 0 && now > savedAt_) ? (now - savedAt_) : 0;

        auto* timeLbl = new QLabel(
            QStringLiteral("Last saved %1").arg(humanAgo(diff)),
            card);
        QFont tf = timeLbl->font();
        tf.setPixelSize(11);
        timeLbl->setFont(tf);
        timeLbl->setStyleSheet(QString("color:%1;").arg(Col::textFaint.name()));
        timeLbl->setAlignment(Qt::AlignCenter);
        v->addWidget(timeLbl);
    }

    v->addSpacing(20);

    // ── Buttons row
    {
        auto* brow = new QHBoxLayout;
        brow->setSpacing(10);
        brow->addStretch(1);

        auto* freshBtn = new ModernButton(QStringLiteral("Start fresh"),
                                          false, card);
        auto* restoreBtn = new ModernButton(QStringLiteral("Restore tabs"),
                                            true, card);

        brow->addWidget(freshBtn);
        brow->addWidget(restoreBtn);
        brow->addStretch(1);

        v->addLayout(brow);

        connect(freshBtn, &QPushButton::clicked, this, [this]{
            applyChoice(alwaysCheck_ && alwaysCheck_->isChecked()
                        ? RestoreChoice::FreshAlways
                        : RestoreChoice::FreshStart);
        });
        connect(restoreBtn, &QPushButton::clicked, this, [this]{
            applyChoice(alwaysCheck_ && alwaysCheck_->isChecked()
                        ? RestoreChoice::RestoreAlways
                        : RestoreChoice::Restore);
        });

        // Keyboard focus on Restore
        restoreBtn->setDefault(true);
        restoreBtn->setFocus();
    }

    v->addSpacing(14);

    // ── "Always do this" checkbox
    {
        auto* crow = new QHBoxLayout;
        crow->addStretch(1);

        alwaysCheck_ = new QCheckBox(QStringLiteral("Remember my choice"), card);
        QFont cf = alwaysCheck_->font();
        cf.setPixelSize(12);
        alwaysCheck_->setFont(cf);
        alwaysCheck_->setCursor(Qt::PointingHandCursor);
        alwaysCheck_->setStyleSheet(QString(
            "QCheckBox{color:%1;spacing:8px;}"
            "QCheckBox::indicator{"
            "  width:16px; height:16px; border-radius:4px;"
            "  background:%2; border:1px solid %3;"
            "}"
            "QCheckBox::indicator:checked{"
            "  background:%4; border:1px solid %4;"
            "  image:none;"
            "}"
            "QCheckBox::indicator:hover{"
            "  border:1px solid %4;"
            "}")
            .arg(Col::textMuted.name(),
                 Col::surface.name(),
                 Col::border.name(),
                 Col::accent.name()));

        crow->addWidget(alwaysCheck_);
        crow->addStretch(1);
        v->addLayout(crow);
    }
}

void RestoreDialog::applyChoice(RestoreChoice c) {
    choice_ = c;
    accept();
}

void RestoreDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // Shadow layers
    for (int i = 8; i >= 1; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 8 / i));
        p.drawRoundedRect(r.adjusted(-i * 1.5, -i * 1.5 + 3, i * 1.5, i * 1.5 + 3),
                          18 + i, 18 + i);
    }

    // Card
    QPainterPath path;
    path.addRoundedRect(r, 18, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(Col::bg);
    p.drawPath(path);

    // Border
    p.setPen(QPen(Col::border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Subtle top highlight
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    p.drawLine(QPointF(r.left() + 20, r.top() + 1),
               QPointF(r.right() - 20, r.top() + 1));
}

void RestoreDialog::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Escape:
        // Esc = start fresh (safe default)
        applyChoice(RestoreChoice::FreshStart);
        e->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Enter = restore (if focus is not on some other button)
        applyChoice(alwaysCheck_ && alwaysCheck_->isChecked()
                    ? RestoreChoice::RestoreAlways
                    : RestoreChoice::Restore);
        e->accept();
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(e);
}
