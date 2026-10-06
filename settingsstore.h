// ============================================================================
//  settingsstore.h — Persistent, live-updating browser settings.
//  Saves to ~/.local/share/RootBrowser/settings.json
// ============================================================================
#pragma once

#include <QString>
#include <QHash>
#include <functional>

// ============================================================================
//  SettingsStore — singleton, key/value pairs with change callbacks.
// ============================================================================
class SettingsStore {
public:
    static SettingsStore& instance();

    // ── Get / set ────────────────────────────────────────────────────────
    bool     getBool  (const QString& key, bool     def = false) const;
    int      getInt   (const QString& key, int      def = 0)     const;
    QString  getString(const QString& key, const QString& def = {}) const;
    double   getDouble(const QString& key, double   def = 0.0)   const;

    void setBool  (const QString& key, bool     value);
    void setInt   (const QString& key, int      value);
    void setString(const QString& key, const QString& value);
    void setDouble(const QString& key, double   value);

    // ── Change listeners ────────────────────────────────────────────────
    using ChangeCallback = std::function<void(const QString& key,
                                              const QVariant& newValue)>;
    // Register a listener for a specific key
    void onChange(const QString& key, ChangeCallback cb);
    // Register a listener for ALL changes
    void onAnyChange(ChangeCallback cb);

    // ── Bulk ────────────────────────────────────────────────────────────
    void resetAll();          // back to defaults
    void clearAllData();      // wipe cookies + cache + history (irreversible)

    // ── Metadata ────────────────────────────────────────────────────────
    static QString filePath();
    qint64 totalFileSize() const;

    // ── Defaults (called on first run) ──────────────────────────────────
    static void installDefaults();

private:
    SettingsStore();
    ~SettingsStore() = default;
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    void load();
    void save() const;
    void notify(const QString& key);

    QHash<QString, QVariant> values_;
    QHash<QString, QVector<ChangeCallback>> keyListeners_;
    QVector<ChangeCallback> globalListeners_;
};
