#ifndef ADAPTIXCLIENT_EMBEDDABLEBROWSERWIDGET_H
#define ADAPTIXCLIENT_EMBEDDABLEBROWSERWIDGET_H

#include <main.h>
#include <UI/Widgets/AbstractDock.h>

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QTabBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QComboBox>
#include <QListWidget>
#include <QScrollArea>
#include <QSplitter>
#include <QNetworkProxy>
#include <QUrl>
#include <QTimer>

namespace AdaptixBrowserFloatingDetail {
class BrowserFloatingHost;
}

#include <oclero/qlementine/widgets/LineEdit.hpp>
#include <oclero/qlementine/widgets/PopoverButton.hpp>
#include <oclero/qlementine/widgets/Popover.hpp>

class AdaptixWidget;

class EmbeddableBrowserWidget;

enum class BrowserChromeMode {
    Full,
    Chromeless
};

struct EmbeddableBrowserOptions {
    BrowserChromeMode mode = BrowserChromeMode::Full;
    QString           title;
    QString           initialUrl;
    QString           logicalId;
    QString           iconPath;

    static EmbeddableBrowserOptions fullMode(const QString& title = QString(), const QString& initialUrl = QString());
    static EmbeddableBrowserOptions chromelessMode(const QString& logicalId, const QString& title,
                                                   const QString& initialUrl = QString(), const QString& iconPath = QString());
};

class BrowserPage : public QWebEnginePage
{
    Q_OBJECT
public:
    explicit BrowserPage(QWebEngineProfile* profile, QWebEngineView* view,
                        QWebEnginePage* devToolsPage = nullptr,
                        EmbeddableBrowserWidget* browserWidget = nullptr,
                        QObject* parent = nullptr);

protected:
    QWebEnginePage* createWindow(QWebEnginePage::WebWindowType type) override;
    bool acceptNavigationRequest(const QUrl& url, QWebEnginePage::NavigationType type, bool isMainFrame) override;

private:
    QWebEngineView* m_view = nullptr;
    QWebEnginePage* m_devToolsPage = nullptr;
    EmbeddableBrowserWidget* m_browserWidget = nullptr;
};

class EmbeddableBrowserWidget : public DockTab
{
Q_OBJECT
    friend class BrowserPage;
    friend class AdaptixBrowserFloatingDetail::BrowserFloatingHost;

    AdaptixWidget*      adaptixWidget = nullptr;
    BrowserChromeMode   m_chromeMode  = BrowserChromeMode::Full;
    QString             m_logicalId;

    bool isFullBrowserChrome() const { return m_chromeMode == BrowserChromeMode::Full; }

    QTabBar* browserTabBar = nullptr;
    QStackedWidget* browserTabStack = nullptr;
    QWidget* browserTabBarRow = nullptr;
    QWebEngineProfile* browserSharedProfile = nullptr;
    QWebEngineView* webView = nullptr;
    oclero::qlementine::LineEdit* urlBar = nullptr;
    QToolButton* backButton = nullptr;
    QToolButton* forwardButton = nullptr;
    QTimer* m_backLongPressTimer = nullptr;
    bool m_backIgnoreNextButtonRelease = false;
    QToolButton* reloadButton = nullptr;
    QToolButton* homeButton = nullptr;
    QToolButton* bookmarkUrlBtn = nullptr;
    QToolButton* proxyButton = nullptr;
    QToolButton* separateWindowButton = nullptr;
    QMainWindow* m_browserFloatingWindow = nullptr;
    oclero::qlementine::Popover* proxyPopover = nullptr;
    QComboBox* proxyCombo = nullptr;
    QComboBox* proxyTunnelCombo = nullptr;
    oclero::qlementine::LineEdit* proxyHostEdit = nullptr;
    oclero::qlementine::LineEdit* proxyPortEdit = nullptr;
    QWidget* proxyHostPortRow = nullptr;
    QWidget* proxyPanel = nullptr;

    QListWidget* bookmarksList = nullptr;
    QWidget* bookmarksBarContainer = nullptr;
    QWidget* bookmarksSection = nullptr;
    QWidget* bookmarksButtonsContainer = nullptr;
    QScrollArea* bookmarksScrollArea = nullptr;
    oclero::qlementine::PopoverButton* allBookmarksButton = nullptr;

    QWebEngineView* devToolsView = nullptr;
    QToolButton* devToolsButton = nullptr;
    QSplitter* verticalSplitter = nullptr;
    bool devToolsVisible = false;

    QNetworkProxy::ProxyType currentProxyType = QNetworkProxy::NoProxy;
    QString currentProxyHost;
    quint16 currentProxyPort = 0;

    bool m_ownsProfile = false;
    bool m_proxyApplied = false;
    static QNetworkProxy s_previousApplicationProxy;
    static int s_activeProxyBrowserCount;

    void createFullUI();
    void createChromelessUI();
    void applyProxy();
    void clearProxy();
    QUrl urlFromUserInput(const QString& input) const;
    static bool isAllowedUrlScheme(const QUrl& url);
    void loadBookmarks();
    void saveBookmarks();
    void loadHomePage();
    void reloadHomePageIfVisible();
    QString buildBookmarksHomeHtml() const;
    bool isInternalHomeUrl(const QUrl& url) const;
    void refreshBookmarksBar();
    void trimBookmarksToFit();
    void updateBookmarkStarButton();
    void updateDevToolsButtonIcon();
    void updateProxyButtonCheckedState();
    void updateSeparateWindowButtonCheckedState();
    void clearFloatingWindowReference();
    void editBookmarkItem(QListWidgetItem* item);
    void deleteBookmarkListItem(QListWidgetItem* item, bool askConfirm);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void refreshProxyTunnelCombo();

    QWebEngineView* currentWebView() const;
    QWebEngineView* viewAtTabIndex(int index) const;
    int tabIndexForView(QWebEngineView* view) const;
    void connectViewSignals(QWebEngineView* view);
    void disconnectViewSignals(QWebEngineView* view);
    QWebEnginePage* createNewTabPage();
    void updateTabTitleForView(QWebEngineView* view);
    void syncDevToolsToCurrentTab();

public:
    void updateNavigationActions();
    void openBookmarkContextMenuAt(const QPoint& globalPos, QListWidgetItem* item);
    explicit EmbeddableBrowserWidget(AdaptixWidget* w, const QString& title = "Browser", const QString& initialUrl = QString());
    explicit EmbeddableBrowserWidget(AdaptixWidget* w, const EmbeddableBrowserOptions& options);
    ~EmbeddableBrowserWidget() override;

    BrowserChromeMode chromeMode() const { return m_chromeMode; }
    QString           logicalId() const { return m_logicalId; }
    QWebEngineView*   primaryWebView() const;

    void loadUrl(const QUrl& url);
    void loadUrl(const QString& url);

    void setProxy(QNetworkProxy::ProxyType type, const QString& host, quint16 port);

    static EmbeddableBrowserWidget* create(AdaptixWidget* w, const QString& title, const QString& url, const QString& proxyHost = QString(), quint16 proxyPort = 0);

    bool isBrowserDevToolsOpen() const { return isFullBrowserChrome() && devToolsVisible; }

    void openBrowserInSeparateWindow();

private Q_SLOTS:
    void onUrlChanged(const QUrl& url);
    void onLoadStarted();
    void onLoadFinished(bool ok);
    void onNavigate();
    void onProxyApply();
    void onProxyTypeChanged(int index);
    void onProxyTunnelPicked(int index);
    void onAddBookmark();
    void onRemoveBookmark();
    void onToggleBookmark();
    void onBookmarkClicked(QListWidgetItem* item);
    void onAllBookmarksListItemPressed(QListWidgetItem* item);
    void onDevToolsToggle();
    void showBookmarkContextMenu(const QPoint& globalPos, QListWidgetItem* listItem);
    void onBackLongPressTimeout();
    void promptAddBookmarkFromHome();
    void handleHomeBookmarkEdit(int row);
    void handleHomeBookmarkDelete(int row);
    void onBrowserTabChanged(int index);
    void onTabCloseRequested(int index);
    void onBrowserTabMoved(int from, int to);
    void onNewTab();
    void onPageTitleChanged(const QString& title);
};

#endif
