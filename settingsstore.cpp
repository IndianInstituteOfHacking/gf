// ============================================================================
//  settingsstore.cpp
// ============================================================================

#include "settingsstore.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QDebug>

// ────────────────────────────────────────────────────────────────────────────
SettingsStore& SettingsStore::instance() {
    static SettingsStore s;
    return s;
}

SettingsStore::SettingsStore() {
    installDefaults();
    load();
}

QString SettingsStore::filePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/settings.json";
}

// ────────────────────────────────────────────────────────────────────────────
void SettingsStore::installDefaults() {
    // Called on every startup; only applies to keys not present on disk.
    // We define defaults via the getX() fallbacks — nothing to do here.
}

// ────────────────────────────────────────────────────────────────────────────
void SettingsStore::load() {
    values_.clear();

    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) return;

    const QJsonObject root = doc.object();
    const QJsonObject v = root.value("values").toObject();

    for (auto it = v.begin(); it != v.end(); ++it) {
        values_.insert(it.key(), it.value().toVariant());
    }
}

void SettingsStore::save() const {
    QJsonObject v;
    for (auto it = values_.constBegin(); it != values_.constEnd(); ++it) {
        v.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }

    QJsonObject root;
    root["version"] = 1;
    root["values"]  = v;

    const QString path = filePath();
    const QString tmp  = path + ".tmp";

    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.flush();
    f.close();

    QFile::remove(path);
    QFile::rename(tmp, path);
}

// ────────────────────────────────────────────────────────────────────────────
//  Getters
// ────────────────────────────────────────────────────────────────────────────
bool SettingsStore::getBool(const QString& key, bool def) const {
    if (!values_.contains(key)) return def;
    const QVariant v = values_.value(key);
    if (v.typeId() == QMetaType::Bool) return v.toBool();
    // Fallback conversions
    const QString s = v.toString().toLower();
    return (s == "true" || s == "1" || s == "yes" || s == "on");
}

int SettingsStore::getInt(const QString& key, int def) const {
    if (!values_.contains(key)) return def;
    return values_.value(key).toInt();
}

QString SettingsStore::getString(const QString& key, const QString& def) const {
    if (!values_.contains(key)) return def;
    return values_.value(key).toString();
}

double SettingsStore::getDouble(const QString& key, double def) const {
    if (!values_.contains(key)) return def;
    return values_.value(key).toDouble();
}

// ────────────────────────────────────────────────────────────────────────────
//  Setters — notify listeners on change
// ────────────────────────────────────────────────────────────────────────────
void SettingsStore::setBool(const QString& key, bool value) {
    if (getBool(key) == value && values_.contains(key)) return;
    values_.insert(key, value);
    save();
    notify(key);
}

void SettingsStore::setInt(const QString& key, int value) {
    if (getInt(key) == value && values_.contains(key)) return;
    values_.insert(key, value);
    save();
    notify(key);
}

void SettingsStore::setString(const QString& key, const QString& value) {
    if (getString(key) == value && values_.contains(key)) return;
    values_.insert(key, value);
    save();
    notify(key);
}

void SettingsStore::setDouble(const QString& key, double value) {
    if (getDouble(key) == value && values_.contains(key)) return;
    values_.insert(key, value);
    save();
    notify(key);
}

// ────────────────────────────────────────────────────────────────────────────
//  Listeners
// ────────────────────────────────────────────────────────────────────────────
void SettingsStore::onChange(const QString& key, ChangeCallback cb) {
    keyListeners_[key].append(std::move(cb));
}

void SettingsStore::onAnyChange(ChangeCallback cb) {
    globalListeners_.append(std::move(cb));
}

void SettingsStore::notify(const QString& key) {
    const QVariant v = values_.value(key);

    if (keyListeners_.contains(key)) {
        for (auto& cb : keyListeners_[key]) {
            if (cb) cb(key, v);
        }
    }
    for (auto& cb : globalListeners_) {
        if (cb) cb(key, v);
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  Bulk
// ────────────────────────────────────────────────────────────────────────────
void SettingsStore::resetAll() {
    values_.clear();
    save();
    // Notify every known key
    for (auto it = keyListeners_.constBegin(); it != keyListeners_.constEnd(); ++it) {
        const QVariant v;
        for (auto& cb : it.value()) if (cb) cb(it.key(), v);
    }
    for (auto& cb : globalListeners_) if (cb) cb(QString(), QVariant());
}

void SettingsStore::clearAllData() {
    // Wipes app data (cookies, cache, history, downloads.json, bookmarks.json)
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    auto removeDir = [](const QString& path) {
        if (path.isEmpty()) return;
        QDir d(path);
        if (d.exists()) d.removeRecursively();
    };

    // Remove specific files (keep settings.json)
    QDir dataDirObj(dataDir);
    if (dataDirObj.exists()) {
        for (const QString& f : dataDirObj.entryList(QDir::Files)) {
            if (f == "settings.json") continue;   // preserve
            dataDirObj.remove(f);
        }
    }

    // Wipe cache dir
    removeDir(cacheDir);
}

qint64 SettingsStore::totalFileSize() const {
    qint64 total = 0;

    auto dirSize = [](const QString& path) -> qint64 {
        qint64 sum = 0;
        QDir d(path);
        if (!d.exists()) return 0;
        for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                                   QDir::Size))
        {
            if (fi.isFile()) sum += fi.size();
            else if (fi.isDir()) {
                QDir sub(fi.absoluteFilePath());
                for (const QFileInfo& si : sub.entryInfoList(QDir::Files))
                    sum += si.size();
            }
        }
        return sum;
    };

    total += dirSize(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    total += dirSize(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    return total;
}
