// ============================================================================
//  urlautocomplete.h — Chrome-style URL autocomplete dropdown.
// ============================================================================
#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

class QLineEdit;
class QTimer;

// ============================================================================
//  Suggestion data
// ============================================================================
struct UrlSuggestion {
    enum Type {
        Bookmark,       // from bookmarks
        History,        // from history
        Search,         // search query
        Direct          // direct URL
    };

    Type    type = History;
    QString url;            // full URL to navigate to
    QString title;          // page title (or the query)
    QString subtitle;       // extra info: "12 visits · 2 min ago"
    QString faviconUrl;     // optional favicon path
    int     score = 0;      // internal ranking
};

// ============================================================================
//  UrlAutocomplete — dropdown popup
// ============================================================================
enum class Theme {
    TopBar,     // classic dropdown under address bar
    HomePage    // centered glass theme for home search
};

class UrlAutocomplete : public QWidget {
public:
    explicit UrlAutocomplete(QWidget* parent = nullptr);

    // Attach to a line edit — intercepts typing
    void attach(QLineEdit* edit);
    void setTheme(Theme t) { theme_ = t; }

    // Show suggestions for the given text
    void showFor(const QString& text);

    // Hide dropdown
    void hideDropdown();

    // Handle keys — returns true if consumed
    bool handleKeyPress(int key, int modifiers);

    // Get current selection (empty if none)
    QString selectedUrl() const;

    // True if a suggestion is selected
    bool hasSelection() const { return selectedIndex_ >= 0; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    bool event(QEvent*) override;

private:
    // Suggestion generation
    void generateSuggestions(const QString& query);
    void addFromBookmarks(const QString& query);
    void addFromHistory(const QString& query);
    void addSearchSuggestion(const QString& query);
    void addDirectUrl(const QString& query);

    // Ranking + dedup
    void rankAndTrim(int maxItems = 8);

    // Layout
    void rebuildLayout();
    int  itemAt(int y) const;
    QRect itemRect(int index) const;

    // Helpers
    static QString matchHighlight(const QString& text, const QString& query);
    static QString elide(const QString& text, int maxWidthPx,
                         const QFont& font, int flags = Qt::ElideRight);

    // Data
    Theme theme_ = Theme::TopBar;
    QLineEdit* edit_ = nullptr;
    QVector<UrlSuggestion> suggestions_;
    int selectedIndex_ = -1;
    int hoverIndex_ = -1;

    // Popup placement
    QWidget* anchor_ = nullptr;

    // Sizing
    static constexpr int kItemHeight   = 48;
    static constexpr int kMaxItems     = 8;
    static constexpr int kPadding      = 8;
    static constexpr int kIconSize     = 24;
    static constexpr int kBorderRadius = 12;
};
