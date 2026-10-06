// ============================================================================
//  shortcuthelp.h — Ctrl+/ shortcut cheat sheet modal.
// ============================================================================
#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

class QLineEdit;
class QTimer;

// ============================================================================
//  ShortcutHelp — modal overlay showing all keyboard shortcuts.
// ============================================================================
class ShortcutHelp : public QWidget {
public:
    explicit ShortcutHelp(QWidget* parent = nullptr);
    ~ShortcutHelp() override;

    // Show the modal with fade-in animation
    void open();

    // Hide with fade-out
    void close();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    // Data
    struct Shortcut {
        QString description;
        QStringList keys;        // e.g. {"Ctrl", "T"}
        QString category;
    };
    struct Category {
        QString name;
        QVector<Shortcut> items;
    };

    void buildData();
    void buildUi();
    void rebuildVisible();

    // Helpers
    bool matchesFilter(const Shortcut& s, const QString& q) const;

    // Data
    QVector<Category> categories_;

    // UI
    QWidget* card_ = nullptr;
    QLineEdit* search_ = nullptr;
    QWidget* listContainer_ = nullptr;

    // Animation
    class QVariantAnimation* fadeAnim_ = nullptr;
    qreal fadeOpacity_ = 1.0;

    // Search
    QTimer* filterTimer_ = nullptr;
};
