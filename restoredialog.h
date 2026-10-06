// ============================================================================
//  restoredialog.h — Startup prompt: "Restore tabs?"
// ============================================================================
#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QCheckBox;
class QLabel;

// Result of the dialog
enum class RestoreChoice {
    Restore,       // user clicked "Restore tabs"
    FreshStart,    // user clicked "Start fresh"
    FreshAlways,   // user wants to always start fresh
    RestoreAlways  // user wants to always restore
};

class RestoreDialog : public QDialog {
public:
    // `tabs` = list of (title, url) pairs to preview
    RestoreDialog(const QVector<QPair<QString, QString>>& tabs,
                  qint64 savedAt,
                  QWidget* parent = nullptr);

    RestoreChoice choice() const { return choice_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void buildUi();
    void applyChoice(RestoreChoice c);
    void rebuildTabsList();

    QVector<QPair<QString, QString>> tabs_;
    qint64 savedAt_ = 0;
    QCheckBox* alwaysCheck_ = nullptr;
    QLabel* tabsListLabel_ = nullptr;
    RestoreChoice choice_ = RestoreChoice::FreshStart;
};
