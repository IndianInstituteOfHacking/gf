// ============================================================================
//  settingspage.cpp — Full-featured settings page.
// ============================================================================

#include "settingspage.h"
#include "settingsstore.h"

#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDateTime>
#include <QUrl>
#include <QFileInfo>

namespace SettingsPage {

// ────────────────────────────────────────────────────────────────────────────
static QString esc(const QString& s) {
    QString out;
    out.reserve(s.size() + 16);
    for (QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;");  break;
        case '>': out += QStringLiteral("&gt;");  break;
        case '"': out += QStringLiteral("&quot;");break;
        case '\'':out += QStringLiteral("&#39;"); break;
        default:  out += c; break;
        }
    }
    return out;
}

// ────────────────────────────────────────────────────────────────────────────
QString build() {
    auto& S = SettingsStore::instance();

    QJsonObject vals;
    vals["search.engine"]       = S.getString("search.engine", "DuckDuckGo");
    vals["search.newTab"]       = S.getBool  ("search.newTab", false);
    vals["privacy.adblock"]     = S.getBool  ("privacy.adblock", true);
    vals["privacy.dnt"]         = S.getBool  ("privacy.dnt", false);
    vals["privacy.block3pc"]    = S.getBool  ("privacy.block3pc", false);
    vals["privacy.blockImages"] = S.getBool  ("privacy.blockImages", false);
    vals["privacy.httpsOnly"]   = S.getBool  ("privacy.httpsOnly", true);
    vals["privacy.fingerprint"] = S.getBool  ("privacy.fingerprint", true);
    vals["appearance.bookmarkBar"] = S.getBool("appearance.bookmarkBar", false);
    vals["appearance.compactTabs"] = S.getBool("appearance.compactTabs", false);
    vals["appearance.animations"]  = S.getBool("appearance.animations", true);
    vals["appearance.wallpaper"]   = S.getString("appearance.wallpaper", "random");
    vals["downloads.folder"]       = S.getString("downloads.folder",
                                                  QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    vals["downloads.askEach"]      = S.getBool  ("downloads.askEach", false);
    vals["startup.homepage"]       = S.getString("startup.homepage", "rootbrowser://home");
    vals["behavior.smoothScroll"]  = S.getBool  ("behavior.smoothScroll", true);
    vals["behavior.javascript"]    = S.getBool  ("behavior.javascript", true);
    vals["behavior.loadImages"]    = S.getBool  ("behavior.loadImages", true);
    vals["behavior.fullscreenNotif"]= S.getBool ("behavior.fullscreenNotif", true);
    vals["history.record"]         = S.getBool  ("history.record", true);
    vals["history.maxEntries"]     = S.getInt   ("history.maxEntries", 5000);
    vals["data.clearOnExit"]       = S.getBool  ("data.clearOnExit", false);

    QJsonObject stats;
    stats["totalSize"] = double(S.totalFileSize());
    stats["settingsFile"] = SettingsStore::filePath();

    QJsonObject root;
    root["values"] = vals;
    root["stats"]  = stats;

    const QString dataJson =
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));

    // ── CSS (as QByteArray)
    static const QByteArray cssBytes = R"CSS(
        * { box-sizing: border-box; }
        html, body {
            margin: 0; padding: 0; min-height: 100%;
            background: #0c0c0e; color: #e6e8ec;
            font-family: 'Segoe UI', 'Inter', Ubuntu, 'Noto Sans',
                         Cantarell, 'DejaVu Sans', sans-serif;
            font-size: 13px;
        }
        .page { max-width: 780px; margin: 0 auto; padding: 40px 28px 80px; }

        .header { display: flex; align-items: center; gap: 14px; margin-bottom: 28px; }
        .icon {
            width: 44px; height: 44px; border-radius: 12px;
            background: #17181b; border: 1px solid #23262c;
            display: flex; align-items: center; justify-content: center;
            flex-shrink: 0;
        }
        .icon svg { width: 22px; height: 22px; color: #b4b8c2; }
        .h1 { margin: 0; font-size: 24px; font-weight: 500;
              letter-spacing: -0.3px; color: #f4f5f7; }
        .subtitle { color: #6b7280; font-size: 12px; margin-top: 3px; }

        .section {
            background: #141519; border: 1px solid #23262c;
            border-radius: 14px; margin-bottom: 16px; overflow: hidden;
        }
        .sectionHead {
            display: flex; align-items: center; gap: 10px;
            padding: 14px 20px;
            border-bottom: 1px solid #23262c;
            background: #17181b;
        }
        .sectionHead svg { width: 16px; height: 16px; color: #8a8f9c; }
        .sectionTitle { font-size: 13px; font-weight: 600; color: #e6e8ec; }
        .sectionBody { padding: 6px 0; }

        .row {
            display: flex; align-items: center;
            padding: 12px 20px; gap: 14px;
            min-height: 48px;
            transition: background 0.1s;
        }
        .row:hover { background: rgba(255,255,255,0.02); }
        .rowText { flex: 1; min-width: 0; }
        .rowLabel { color: #e6e8ec; font-size: 13px; margin-bottom: 3px; }
        .rowDesc { color: #6b7280; font-size: 11px; line-height: 1.4; }

        .switch {
            position: relative;
            width: 38px; height: 22px;
            background: #2c2e34;
            border-radius: 11px;
            cursor: pointer;
            transition: background 0.2s;
            flex-shrink: 0;
        }
        .switch::after {
            content: '';
            position: absolute;
            top: 3px; left: 3px;
            width: 16px; height: 16px;
            background: #b4b8c2;
            border-radius: 50%;
            transition: all 0.2s cubic-bezier(0.4, 0.0, 0.2, 1);
        }
        .switch.on { background: #5d9df1; }
        .switch.on::after { left: 19px; background: #ffffff; }

        .select, .input {
            background: #1c1d21; color: #e6e8ec;
            border: 1px solid #2c2e34; border-radius: 8px;
            padding: 7px 12px; font-size: 12px;
            outline: none; min-width: 180px;
            transition: border-color 0.15s, background 0.15s;
            font-family: inherit;
        }
        .select:hover, .input:hover { background: #232429; }
        .select:focus, .input:focus { border-color: #5d9df1; }
        .input { min-width: 260px; }
        .input::placeholder { color: #5c606b; }

        .btn {
            background: #1c1d21; color: #d7d9de;
            border: 1px solid #2c2e34; border-radius: 8px;
            padding: 7px 14px; font-size: 12px;
            cursor: pointer; transition: all 0.12s;
            font-family: inherit;
            display: inline-flex; align-items: center; gap: 6px;
        }
        .btn:hover { background: #23262c; border-color: #3a3d45; }
        .btn.danger { color: #e08880; border-color: #4a2020; }
        .btn.danger:hover {
            background: #4a2020; border-color: #7a2d2d; color: #ffb3b3;
        }
        .btn svg { width: 13px; height: 13px; }

        .resetBar {
            display: flex; gap: 10px; justify-content: flex-end;
            padding: 20px 0 0;
            border-top: 1px solid #1a1c22;
            margin-top: 24px;
        }

        .toast {
            position: fixed; bottom: 24px; left: 50%;
            transform: translateX(-50%) translateY(80px);
            background: #23262c; color: #f1f2f5;
            padding: 10px 20px; border-radius: 10px;
            border: 1px solid #3a3d45;
            font-size: 12px;
            box-shadow: 0 8px 24px rgba(0,0,0,0.4);
            opacity: 0;
            transition: all 0.25s cubic-bezier(0.4, 0.0, 0.2, 1);
            z-index: 100;
            pointer-events: none;
        }
        .toast.show {
            opacity: 1;
            transform: translateX(-50%) translateY(0);
        }
    )CSS";
    const QString css = QString::fromUtf8(cssBytes);

    // ── JS (as QByteArray)
    static const QByteArray jsBytes = R"JS(
const DATA = __DATA_PLACEHOLDER__;
const VALS = DATA.values || {};
const STATS = DATA.stats || {};

const toastEl = document.getElementById('toast');
let toastTimer = null;
function showToast(msg) {
    toastEl.textContent = msg;
    toastEl.classList.add('show');
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove('show'), 1800);
}

function setSetting(key, type, value) {
    const encoded = encodeURIComponent(String(value));
    window.location.href = 'rootbrowser-settings-set:' +
                           encodeURIComponent(key) + ':' +
                           type + ':' + encoded;
}

function initControls() {
    document.querySelectorAll('.switch').forEach(el => {
        const key = el.getAttribute('data-key');
        const val = VALS[key] === true;
        if (val) el.classList.add('on');
        el.addEventListener('click', () => {
            const nowOn = !el.classList.contains('on');
            el.classList.toggle('on', nowOn);
            setSetting(key, 'bool', nowOn ? 'true' : 'false');
            showToast(key.split('.').pop() + ': ' + (nowOn ? 'on' : 'off'));
        });
    });
    document.querySelectorAll('select[data-key]').forEach(el => {
        const key = el.getAttribute('data-key');
        const val = VALS[key];
        if (val !== undefined) el.value = String(val);
        el.addEventListener('change', () => {
            const type = el.getAttribute('data-type');
            setSetting(key, type, el.value);
            showToast('Saved');
        });
    });
    document.querySelectorAll('input[data-key]').forEach(el => {
        const key = el.getAttribute('data-key');
        const val = VALS[key];
        if (val !== undefined) el.value = String(val);
        let timer = null;
        el.addEventListener('input', () => {
            if (timer) clearTimeout(timer);
            timer = setTimeout(() => {
                const type = el.getAttribute('data-type');
                setSetting(key, type, el.value);
                showToast('Saved');
            }, 500);
        });
    });
}

document.getElementById('dlFolderBtn').addEventListener('click', () => {
    window.location.href = 'rootbrowser-settings-action:pick-download-folder';
});
document.getElementById('clearDataBtn').addEventListener('click', () => {
    if (!confirm('Clear all browsing data?')) return;
    window.location.href = 'rootbrowser-settings-action:clear-data';
});
document.getElementById('resetBtn').addEventListener('click', () => {
    if (!confirm('Reset all settings to defaults?')) return;
    window.location.href = 'rootbrowser-settings-action:reset';
});
document.getElementById('openFolderBtn').addEventListener('click', () => {
    window.location.href = 'rootbrowser-settings-action:open-folder';
});

function updateStats() {
    const size = STATS.totalSize || 0;
    const el = document.getElementById('dataSize');
    if (el) {
        const u = ['B','KB','MB','GB'];
        let v = size, i = 0;
        while (v >= 1024 && i < 3) { v /= 1024; i++; }
        el.textContent = 'Storage used: ' + v.toFixed(i === 0 ? 0 : 1) + ' ' + u[i];
    }
    const folderEl = document.getElementById('dlFolder');
    if (folderEl && VALS['downloads.folder']) {
        folderEl.textContent = VALS['downloads.folder'];
    }
}

initControls();
updateStats();
    )JS";
    QString js = QString::fromUtf8(jsBytes);
    js.replace("__DATA_PLACEHOLDER__", dataJson);

    // ── HTML
    const QString htmlTemplate = QStringLiteral(R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings — RootBrowser</title>
<style>%1</style>
</head><body>
<div class="page">
    <div class="header">
        <div class="icon">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="3"/>
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>
            </svg>
        </div>
        <div>
            <h1 class="h1">Settings</h1>
            <div class="subtitle">Changes are saved automatically</div>
        </div>
    </div>

    <!-- Search -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="11" cy="11" r="7"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>
            </svg>
            <span class="sectionTitle">Search</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Default search engine</div>
                    <div class="rowDesc">Used when you type a query in the address bar</div>
                </div>
                <select class="select" data-key="search.engine" data-type="string">
                    <option value="Google">Google</option>
                    <option value="DuckDuckGo">DuckDuckGo</option>
                    <option value="Bing">Bing</option>
                    <option value="Brave">Brave</option>
                </select>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Open results in new tab</div>
                    <div class="rowDesc">Search results open in a new tab</div>
                </div>
                <div class="switch" data-key="search.newTab" data-type="bool"></div>
            </div>
        </div>
    </div>

    <!-- Privacy -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
            </svg>
            <span class="sectionTitle">Privacy</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Block ads &amp; trackers</div>
                    <div class="rowDesc">Uses RootBrowser's built-in filter list</div>
                </div>
                <div class="switch" data-key="privacy.adblock" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Send Do Not Track header</div>
                    <div class="rowDesc">Ask websites not to track you</div>
                </div>
                <div class="switch" data-key="privacy.dnt" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Block third-party cookies</div>
                    <div class="rowDesc">Prevents cross-site tracking cookies</div>
                </div>
                <div class="switch" data-key="privacy.block3pc" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Block images</div>
                    <div class="rowDesc">Disables images on all websites</div>
                </div>
                <div class="switch" data-key="privacy.blockImages" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">HTTPS-Only mode</div>
                    <div class="rowDesc">Auto-upgrade HTTP sites to HTTPS</div>
                </div>
                <div class="switch" data-key="privacy.httpsOnly" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Fingerprint protection</div>
                    <div class="rowDesc">Randomize canvas, WebGL, fonts</div>
                </div>
                <div class="switch" data-key="privacy.fingerprint" data-type="bool"></div>
            </div>
        </div>
    </div>

    <!-- Appearance -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="10"/>
                <circle cx="12" cy="12" r="4" fill="currentColor"/>
            </svg>
            <span class="sectionTitle">Appearance</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Show bookmarks bar</div>
                    <div class="rowDesc">Under the address bar · Ctrl+Shift+B</div>
                </div>
                <div class="switch" data-key="appearance.bookmarkBar" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Compact tabs</div>
                    <div class="rowDesc">Reduced tab height</div>
                </div>
                <div class="switch" data-key="appearance.compactTabs" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Enable animations</div>
                    <div class="rowDesc">Smooth transitions</div>
                </div>
                <div class="switch" data-key="appearance.animations" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Wallpaper</div>
                    <div class="rowDesc">Choose home page background</div>
                </div>
                <select class="select" data-key="appearance.wallpaper" data-type="string">
                    <option value="sunset">Sunset</option>
                    <option value="night">Night</option>
                    <option value="random">Random (rotate)</option>
                </select>
            </div>
        </div>
    </div>

    <!-- Downloads -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
                <polyline points="7 10 12 15 17 10"/>
                <line x1="12" y1="15" x2="12" y2="3"/>
            </svg>
            <span class="sectionTitle">Downloads</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Download folder</div>
                    <div class="rowDesc" id="dlFolder">/home/user/Downloads</div>
                </div>
                <button class="btn" id="dlFolderBtn">Change…</button>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Ask before saving</div>
                    <div class="rowDesc">Prompt for location on every download</div>
                </div>
                <div class="switch" data-key="downloads.askEach" data-type="bool"></div>
            </div>
        </div>
    </div>

    <!-- Startup -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M12 2L2 7l10 5 10-5-10-5z"/>
                <polyline points="2 17 12 22 22 17"/>
                <polyline points="2 12 12 17 22 12"/>
            </svg>
            <span class="sectionTitle">On Startup</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Homepage</div>
                    <div class="rowDesc">Page shown when you click the home button</div>
                </div>
                <input class="input" type="text" data-key="startup.homepage"
                       data-type="string" placeholder="rootbrowser://home">
            </div>
        </div>
    </div>

    <!-- Behavior -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
            </svg>
            <span class="sectionTitle">Behavior</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Enable JavaScript</div>
                    <div class="rowDesc">Disabling may break websites</div>
                </div>
                <div class="switch" data-key="behavior.javascript" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Load images automatically</div>
                    <div class="rowDesc">Turn off to save bandwidth</div>
                </div>
                <div class="switch" data-key="behavior.loadImages" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Smooth scrolling</div>
                    <div class="rowDesc">Animated scrolling</div>
                </div>
                <div class="switch" data-key="behavior.smoothScroll" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Allow fullscreen requests</div>
                    <div class="rowDesc">Websites can enter fullscreen</div>
                </div>
                <div class="switch" data-key="behavior.fullscreenNotif" data-type="bool"></div>
            </div>
        </div>
    </div>

    <!-- History -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="10"/>
                <polyline points="12 6 12 12 16 14"/>
            </svg>
            <span class="sectionTitle">History</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Record browsing history</div>
                    <div class="rowDesc">Save visited pages</div>
                </div>
                <div class="switch" data-key="history.record" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Maximum history entries</div>
                    <div class="rowDesc">Oldest entries removed when reached</div>
                </div>
                <select class="select" data-key="history.maxEntries" data-type="int">
                    <option value="100">100</option>
                    <option value="1000">1,000</option>
                    <option value="5000">5,000</option>
                    <option value="10000">10,000</option>
                </select>
            </div>
        </div>
    </div>

    <!-- Data -->
    <div class="section">
        <div class="sectionHead">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <ellipse cx="12" cy="5" rx="9" ry="3"/>
                <path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/>
                <path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/>
            </svg>
            <span class="sectionTitle">Data &amp; Storage</span>
        </div>
        <div class="sectionBody">
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Clear browsing data on exit</div>
                    <div class="rowDesc">Wipes cookies, cache, and history</div>
                </div>
                <div class="switch" data-key="data.clearOnExit" data-type="bool"></div>
            </div>
            <div class="row">
                <div class="rowText">
                    <div class="rowLabel">Clear data manually</div>
                    <div class="rowDesc" id="dataSize">Calculating…</div>
                </div>
                <button class="btn danger" id="clearDataBtn">Clear now</button>
            </div>
        </div>
    </div>

    <div class="resetBar">
        <button class="btn" id="openFolderBtn">Open data folder</button>
        <button class="btn danger" id="resetBtn">Reset all settings</button>
    </div>
</div>

<div class="toast" id="toast"></div>
<script>%2</script>
</body></html>)HTML")
        .arg(css, js);

    return htmlTemplate;
}

} // namespace SettingsPage
