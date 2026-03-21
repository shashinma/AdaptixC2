#include <UI/Widgets/EmbeddableBrowserWidget.h>
#include <UI/Widgets/AdaptixWidget.h>
#include <Client/AuthProfile.h>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabBar>
#include <QStackedWidget>
#include <QWebEngineProfile>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>
#include <QNetworkProxy>
#include <QUrl>
#include <QUrlQuery>
#include <QStyle>
#include <QSettings>
#include <QSplitter>
#include <QListWidget>
#include <QScrollArea>
#include <QFrame>

#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineHistory>
#include <QResizeEvent>
#include <QTimer>
#include <QMouseEvent>
#include <QPushButton>
#include <QAction>
#include <QClipboard>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontMetrics>

#include <oclero/qlementine/widgets/Popover.hpp>
#include <oclero/qlementine/widgets/Label.hpp>
#include <oclero/qlementine/widgets/Menu.hpp>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QReadLocker>
#include <QSignalBlocker>
#include <QFile>
#include <QIODevice>
#include <QMainWindow>
#include <QCloseEvent>
#include <QPointer>
#include <functional>

namespace AdaptixBrowserFloatingDetail {

class BrowserFloatingHost final : public QMainWindow
{
public:
    explicit BrowserFloatingHost(EmbeddableBrowserWidget* browser, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QPointer<EmbeddableBrowserWidget> m_browser;
};

}

namespace {

QString resourcePngAsDataUri(const QString& qrcPath)
{
    QFile f(qrcPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(f.readAll().toBase64());
}

class EmbeddableBrowserUrlRowFocusFilter final : public QObject
{
public:
    EmbeddableBrowserUrlRowFocusFilter(QWidget* row, QLineEdit* edit, QObject* parent = nullptr)
        : QObject(parent)
        , m_row(row)
        , m_edit(edit)
    {
        m_edit->installEventFilter(this);
        applyStyle(m_edit->hasFocus());
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != m_edit)
            return false;
        switch (event->type()) {
        case QEvent::FocusIn:
            applyStyle(true);
            break;
        case QEvent::FocusOut:
            applyStyle(false);
            break;
        default:
            return false;
        }
        return false;
    }

private:
    void applyStyle(bool focused)
    {
        if (!m_row)
            return;
        if (focused) {
            m_row->setStyleSheet(QStringLiteral(
                "QWidget#EmbeddableBrowserUrlRow {"
                " background-color: palette(base);"
                " border: 2px solid palette(highlight);"
                " border-radius: 4px;"
                "}"));
        } else {
            m_row->setStyleSheet(QStringLiteral(
                "QWidget#EmbeddableBrowserUrlRow {"
                " background-color: palette(base);"
                " border: 1px solid palette(mid);"
                " border-radius: 4px;"
                "}"));
        }
    }

    QWidget* m_row = nullptr;
    QLineEdit* m_edit = nullptr;
};

class BookmarksListViewportMenuFilter final : public QObject
{
public:
    BookmarksListViewportMenuFilter(QListWidget* list, std::function<void(const QPoint&, QListWidgetItem*)> onMenu,
                                    QObject* parent = nullptr)
        : QObject(parent)
        , m_list(list)
        , m_onMenu(std::move(onMenu))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!m_list || watched != m_list->viewport() || !m_onMenu)
            return false;

        auto resolveItem = [this](const QPoint& viewportPos) -> QListWidgetItem* {
            QListWidgetItem* item = m_list->itemAt(viewportPos);
            if (!item) {
                const QModelIndex ix = m_list->indexAt(viewportPos);
                if (ix.isValid())
                    item = m_list->itemFromIndex(ix);
            }
            return item;
        };

        switch (event->type()) {
        case QEvent::ContextMenu: {
            auto* ce = static_cast<QContextMenuEvent*>(event);
            const QPoint vpPos = m_list->viewport()->mapFromGlobal(ce->globalPos());
            QListWidgetItem* item = resolveItem(vpPos);
            if (!item)
                return false;
            m_onMenu(ce->globalPos(), item);
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::RightButton)
                return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint vpPos = me->position().toPoint();
#else
            const QPoint vpPos = me->pos();
#endif
            QListWidgetItem* item = resolveItem(vpPos);
            if (!item)
                return false;
            const QPoint globalPos = m_list->viewport()->mapToGlobal(vpPos);
            m_onMenu(globalPos, item);
            return true;
        }
        default:
            return false;
        }
    }

private:
    QListWidget* m_list = nullptr;
    std::function<void(const QPoint&, QListWidgetItem*)> m_onMenu;
};

}

class NoScrollClipArea : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }

    void contextMenuEvent(QContextMenuEvent* e) override
    {
        QWidget* w = widget();
        if (!w) {
            QScrollArea::contextMenuEvent(e);
            return;
        }
        const QPoint global = e->globalPos();
        const QPoint inWidget = w->mapFromGlobal(global);
        if (!w->rect().contains(inWidget)) {
            QScrollArea::contextMenuEvent(e);
            return;
        }
        QWidget* target = w->childAt(inWidget);
        if (target) {
            const QPoint inTarget = target->mapFrom(w, inWidget);
            QContextMenuEvent childEv(e->reason(), inTarget, global, e->modifiers());
            QCoreApplication::sendEvent(target, &childEv);
        } else {
            QContextMenuEvent childEv(e->reason(), inWidget, global, e->modifiers());
            QCoreApplication::sendEvent(w, &childEv);
        }
        e->accept();
    }
};

static const QString kBrowserProxyTunnelCustomMarker = QStringLiteral("__adaptix_proxy_tunnel_custom__");

static QString normalizeTunnelBindHostForClient(const QString& iface)
{
    const QString s = iface.trimmed();
    if (s.isEmpty() || s.compare(QStringLiteral("0.0.0.0"), Qt::CaseInsensitive) == 0
        || s == QStringLiteral("::"))
        return QStringLiteral("127.0.0.1");
    if (s.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("127.0.0.1");
    return s;
}

static bool tunnelTypeIsSocks5ForBrowser(const QString& type)
{
    const QString t = type.trimmed().toLower();
    if (t.contains(QLatin1String("socks4")))
        return false;
    return t.contains(QLatin1String("socks5"));
}

static QVector<TunnelData> tunnelsForBrowserProxyPicker(AdaptixWidget* w)
{
    if (!w)
        return {};
    QReadLocker locker(&w->TunnelsLock);
    return w->Tunnels;
}

static TunnelData tunnelByIdForBrowser(AdaptixWidget* w, const QString& id)
{
    if (!w || id.isEmpty())
        return {};
    QReadLocker locker(&w->TunnelsLock);
    for (const TunnelData& x : w->Tunnels) {
        if (x.TunnelId == id)
            return x;
    }
    return {};
}

EmbeddableBrowserWidget::EmbeddableBrowserWidget(AdaptixWidget* w, const QString& title, const QString& initialUrl)
    : DockTab(title, w->GetProfile()->GetProject(), ":/icons/globe_64dp")
    , adaptixWidget(w)
{
    setAutoBlinkEnabled(false);
    createUI();

    connect(reloadButton, &QToolButton::clicked, this, [this]() {
        QWebEngineView* v = currentWebView();
        if (v && isInternalHomeUrl(v->url()))
            loadHomePage();
        else if (v)
            v->reload();
    });
    connect(homeButton, &QToolButton::clicked, this, &EmbeddableBrowserWidget::loadHomePage);
    m_backLongPressTimer = new QTimer(this);
    m_backLongPressTimer->setSingleShot(true);
    m_backLongPressTimer->setInterval(550);
    connect(m_backLongPressTimer, &QTimer::timeout, this, &EmbeddableBrowserWidget::onBackLongPressTimeout);
    backButton->installEventFilter(this);
    connect(forwardButton, &QToolButton::clicked, this, [this]() {
        QWebEngineView* v = currentWebView();
        if (v && v->page())
            v->page()->triggerAction(QWebEnginePage::Forward);
    });
    connect(urlBar, &QLineEdit::returnPressed, this, &EmbeddableBrowserWidget::onNavigate);
    connect(proxyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EmbeddableBrowserWidget::onProxyTypeChanged);
    connect(bookmarksList, &QListWidget::itemPressed, this, &EmbeddableBrowserWidget::onAllBookmarksListItemPressed);
    connect(devToolsButton, &QToolButton::clicked, this, &EmbeddableBrowserWidget::onDevToolsToggle);

    loadBookmarks();
    updateNavigationActions();

    dockWidget->setWidget(this);

    const QString init = initialUrl.trimmed();
    if (!init.isEmpty() && init.compare(QStringLiteral("about:blank"), Qt::CaseInsensitive) != 0) {
        loadUrl(initialUrl);
    } else {
        loadHomePage();
    }
}

EmbeddableBrowserWidget::~EmbeddableBrowserWidget()
{
    if (m_browserFloatingWindow) {
        QPointer<QMainWindow> win(m_browserFloatingWindow);
        m_browserFloatingWindow = nullptr;
        if (win) {
            win->takeCentralWidget();
            win->deleteLater();
        }
    }
}

BrowserPage::BrowserPage(QWebEngineProfile* profile, QWebEngineView* view,
                         QWebEnginePage* devToolsPage,
                         EmbeddableBrowserWidget* browserWidget, QObject* parent)
    : QWebEnginePage(profile, parent)
    , m_view(view)
    , m_devToolsPage(devToolsPage)
    , m_browserWidget(browserWidget)
{
}

QWebEnginePage* BrowserPage::createWindow(QWebEnginePage::WebWindowType type)
{
    Q_UNUSED(type);
    if (!m_browserWidget)
        return nullptr;
    QWebEnginePage* page = m_browserWidget->createNewTabPage();
    if (m_devToolsPage && m_browserWidget->isBrowserDevToolsOpen())
        page->setDevToolsPage(m_devToolsPage);
    return page;
}

bool BrowserPage::acceptNavigationRequest(const QUrl& url, QWebEnginePage::NavigationType type, bool isMainFrame)
{
    Q_UNUSED(type);
    if (!isMainFrame || url.scheme() != QStringLiteral("adaptix-browser"))
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);

    const QString host = url.host();
    if (host.compare(QStringLiteral("add"), Qt::CaseInsensitive) == 0) {
        if (m_browserWidget)
            QMetaObject::invokeMethod(m_browserWidget, "promptAddBookmarkFromHome", Qt::QueuedConnection);
        return false;
    }
    if (host.compare(QStringLiteral("edit"), Qt::CaseInsensitive) == 0) {
        bool ok = false;
        const int row = QUrlQuery(url).queryItemValue(QStringLiteral("i")).toInt(&ok);
        if (m_browserWidget && ok)
            QMetaObject::invokeMethod(m_browserWidget, "handleHomeBookmarkEdit", Qt::QueuedConnection, Q_ARG(int, row));
        return false;
    }
    if (host.compare(QStringLiteral("delete"), Qt::CaseInsensitive) == 0) {
        bool ok = false;
        const int row = QUrlQuery(url).queryItemValue(QStringLiteral("i")).toInt(&ok);
        if (m_browserWidget && ok)
            QMetaObject::invokeMethod(m_browserWidget, "handleHomeBookmarkDelete", Qt::QueuedConnection, Q_ARG(int, row));
        return false;
    }

    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

void EmbeddableBrowserWidget::createUI()
{
    devToolsView = new QWebEngineView(this);

    auto* urlBarRow = new QWidget(this);
    urlBarRow->setObjectName(QStringLiteral("EmbeddableBrowserUrlRow"));
    auto* urlRowLay = new QHBoxLayout(urlBarRow);
    urlRowLay->setContentsMargins(0, 0, 0, 0);
    urlRowLay->setSpacing(4);
    urlRowLay->setAlignment(Qt::AlignVCenter);

    urlBar = new oclero::qlementine::LineEdit(urlBarRow);
    urlBar->setPlaceholderText(tr("Enter URL (e.g. https://example.com)"));
    urlBar->setIcon(QIcon(":/icons/globe_64dp"));

    bookmarkUrlBtn = new QToolButton(urlBarRow);
    bookmarkUrlBtn->setToolTip(tr("Add/remove bookmark"));
    bookmarkUrlBtn->setAutoRaise(true);
    bookmarkUrlBtn->setIconSize(QSize(16, 16));
    bookmarkUrlBtn->setFixedSize(28, 28);
    connect(bookmarkUrlBtn, &QToolButton::clicked, this, &EmbeddableBrowserWidget::onToggleBookmark);
    updateBookmarkStarButton();

    auto* clearUrlBtn = new QToolButton(urlBarRow);
    clearUrlBtn->setIcon(style()->standardIcon(QStyle::SP_LineEditClearButton));
    clearUrlBtn->setToolTip(tr("Clear"));
    clearUrlBtn->setAutoRaise(true);
    clearUrlBtn->setIconSize(QSize(16, 16));
    clearUrlBtn->setFixedSize(28, 28);
    connect(clearUrlBtn, &QToolButton::clicked, urlBar, &QLineEdit::clear);

    urlRowLay->addWidget(urlBar, 1, Qt::AlignVCenter);
    urlRowLay->addWidget(bookmarkUrlBtn, 0, Qt::AlignVCenter);
    urlRowLay->addWidget(clearUrlBtn, 0, Qt::AlignVCenter);

    int urlInnerPadV = 2;
    int urlInnerPadH = 4;
    if (auto* qst = qobject_cast<oclero::qlementine::QlementineStyle*>(QApplication::style())) {
        const int ch = qst->theme().controlHeightLarge;
        urlBarRow->setMinimumHeight(ch + 2 * urlInnerPadV);
    } else {
        urlBarRow->setMinimumHeight(34);
    }
    urlRowLay->setContentsMargins(urlInnerPadH, urlInnerPadV, urlInnerPadH, urlInnerPadV);
    urlBar->setFrame(false);
    urlBarRow->setAttribute(Qt::WA_StyledBackground, true);
    new EmbeddableBrowserUrlRowFocusFilter(urlBarRow, urlBar, urlBar);
    urlBar->setStyleSheet(QStringLiteral(
        "QLineEdit { background: transparent; border: none; padding: 2px 4px; }"
        "QLineEdit:focus { border: none; outline: none; }"));

    backButton = new QToolButton(this);
    backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    backButton->setIconSize(QSize(18, 18));
    backButton->setToolTip(tr("Back — long press to show history"));
    backButton->setAutoRaise(true);
    backButton->setEnabled(false);

    forwardButton = new QToolButton(this);
    forwardButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    forwardButton->setIconSize(QSize(18, 18));
    forwardButton->setToolTip("Forward");
    forwardButton->setAutoRaise(true);
    forwardButton->setEnabled(false);

    reloadButton = new QToolButton(this);
    reloadButton->setIcon(QIcon(":/icons/reload"));
    reloadButton->setIconSize(QSize(18, 18));
    reloadButton->setToolTip("Reload");
    reloadButton->setAutoRaise(true);

    homeButton = new QToolButton(this);
    homeButton->setIcon(QIcon(QStringLiteral(":/icons/home_64dp")));
    homeButton->setIconSize(QSize(18, 18));
    homeButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    homeButton->setToolTip(tr("Start page with bookmarks"));
    homeButton->setAutoRaise(true);

    proxyCombo = new QComboBox(this);
    proxyCombo->addItem("No proxy", QNetworkProxy::NoProxy);
    proxyCombo->addItem("SOCKS5", QNetworkProxy::Socks5Proxy);
    proxyCombo->addItem("HTTP", QNetworkProxy::HttpProxy);
    proxyCombo->setMinimumWidth(100);
    if (auto* qst = qobject_cast<oclero::qlementine::QlementineStyle*>(QApplication::style()))
        proxyCombo->setMinimumHeight(qst->theme().controlHeightLarge);

    proxyHostEdit = new oclero::qlementine::LineEdit(this);
    proxyHostEdit->setPlaceholderText(tr("host"));

    proxyPortEdit = new oclero::qlementine::LineEdit(this);
    proxyPortEdit->setPlaceholderText(tr("port"));
    proxyPortEdit->setMaximumWidth(70);

    auto proxyApplyBtn = new QPushButton("Apply", this);
    proxyApplyBtn->setToolTip("Apply proxy and reload");
    connect(proxyApplyBtn, &QPushButton::clicked, this, [this]() {
        onProxyApply();
        proxyPopover->setOpened(false);
    });

    auto proxyContentLayout = new QVBoxLayout();
    proxyContentLayout->setSpacing(8);
    auto* proxyLabel = new oclero::qlementine::Label(tr("Proxy"), oclero::qlementine::TextRole::H5, this);
    proxyContentLayout->addWidget(proxyLabel);
    proxyTunnelCombo = new QComboBox(this);
    proxyTunnelCombo->setMinimumWidth(100);
    proxyTunnelCombo->setToolTip(tr("Active SOCKS5 tunnels from the Tunnels tab. Chooses local listen address for the browser."));
    if (auto* qstTun = qobject_cast<oclero::qlementine::QlementineStyle*>(QApplication::style()))
        proxyTunnelCombo->setMinimumHeight(qstTun->theme().controlHeightLarge);
    proxyContentLayout->addWidget(proxyTunnelCombo);
    proxyContentLayout->addWidget(proxyCombo);
    proxyHostPortRow = new QWidget(this);
    auto* hostPortLayout = new QHBoxLayout(proxyHostPortRow);
    hostPortLayout->setContentsMargins(0, 0, 0, 0);
    hostPortLayout->setSpacing(8);
    hostPortLayout->addWidget(proxyHostEdit, 1);
    hostPortLayout->addWidget(proxyPortEdit);
    proxyContentLayout->addWidget(proxyHostPortRow);
    proxyContentLayout->addWidget(proxyApplyBtn);

    auto proxyContent = new QWidget(this);
    proxyContent->setLayout(proxyContentLayout);
    proxyContent->setMinimumWidth(200);

    proxyButton = new QToolButton(this);
    proxyButton->setIcon(QIcon(":/icons/vpn"));
    proxyButton->setIconSize(QSize(18, 18));
    proxyButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    proxyButton->setToolTip(tr("Proxy settings"));
    proxyButton->setAutoRaise(true);
    proxyButton->setCheckable(true);

    proxyPopover = new oclero::qlementine::Popover(this);
    proxyPopover->setAnchorWidget(proxyButton);
    proxyPopover->setContentWidget(proxyContent);
    proxyPopover->setPreferredPosition(oclero::qlementine::Popover::Position::Bottom);
    proxyPopover->setPreferredAlignment(oclero::qlementine::Popover::Alignment::End);
    connect(proxyButton, &QToolButton::clicked, this, [this]() {
        proxyPopover->setOpened(!proxyPopover->isOpened());
        updateProxyButtonCheckedState();
    });

    separateWindowButton = new QToolButton(this);
    separateWindowButton->setIcon(QIcon(QStringLiteral(":/icons/external_window_64dp")));
    separateWindowButton->setIconSize(QSize(18, 18));
    separateWindowButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    separateWindowButton->setToolTip(tr("Open browser in a separate window; click again to return to the tab"));
    separateWindowButton->setAutoRaise(true);
    separateWindowButton->setCheckable(true);
    connect(separateWindowButton, &QToolButton::clicked, this, [this]() {
        openBrowserInSeparateWindow();
        updateSeparateWindowButtonCheckedState();
    });
    connect(proxyPopover, &oclero::qlementine::Popover::aboutToOpen, this, &EmbeddableBrowserWidget::refreshProxyTunnelCombo);
    connect(proxyTunnelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EmbeddableBrowserWidget::onProxyTunnelPicked);
    connect(proxyTunnelCombo, QOverload<int>::of(&QComboBox::activated), this, &EmbeddableBrowserWidget::onProxyTunnelPicked);
    refreshProxyTunnelCombo();

    onProxyTypeChanged(proxyCombo->currentIndex());

    auto navLayout = new QHBoxLayout();
    navLayout->setContentsMargins(4, 2, 4, 2);
    navLayout->setSpacing(2);
    const Qt::Alignment navAlign = Qt::AlignVCenter;
    navLayout->addWidget(backButton, 0, navAlign);
    navLayout->addWidget(forwardButton, 0, navAlign);
    navLayout->addWidget(reloadButton, 0, navAlign);
    navLayout->addWidget(homeButton, 0, navAlign);

    devToolsButton = new QToolButton(this);
    devToolsButton->setIcon(QIcon(QStringLiteral(":/icons/dev_mode_64dp")));
    devToolsButton->setIconSize(QSize(18, 18));
    devToolsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    devToolsButton->setToolTip(tr("Toggle Developer Tools"));
    devToolsButton->setAutoRaise(true);
    devToolsButton->setCheckable(true);
    navLayout->addWidget(devToolsButton, 0, navAlign);

    navLayout->addWidget(urlBarRow, 1, navAlign);
    navLayout->addWidget(proxyButton, 0, navAlign);
    navLayout->addWidget(separateWindowButton, 0, navAlign);

    proxyPanel = new QWidget(this);
    proxyPanel->setFixedHeight(42);
    auto proxyLayout = new QHBoxLayout(proxyPanel);
    proxyLayout->setContentsMargins(0, 0, 0, 0);
    proxyLayout->addLayout(navLayout);

    bookmarksList = new QListWidget(this);
    bookmarksList->setMinimumWidth(200);
    bookmarksList->setMinimumHeight(150);
    bookmarksList->setContextMenuPolicy(Qt::DefaultContextMenu);
    if (QWidget* vp = bookmarksList->viewport()) {
        vp->setContextMenuPolicy(Qt::DefaultContextMenu);
        auto* menuFilter = new BookmarksListViewportMenuFilter(
            bookmarksList,
            [this](const QPoint& globalPos, QListWidgetItem* item) { openBookmarkContextMenuAt(globalPos, item); },
            vp);
        vp->installEventFilter(menuFilter);
    }

    auto bookmarksPopoverLayout = new QVBoxLayout();
    bookmarksPopoverLayout->setContentsMargins(0, 0, 0, 0);
    bookmarksPopoverLayout->setSpacing(0);
    bookmarksPopoverLayout->addWidget(bookmarksList, 1);

    auto bookmarksPopoverContent = new QWidget(this);
    bookmarksPopoverContent->setLayout(bookmarksPopoverLayout);
    bookmarksPopoverContent->setMinimumSize(220, 250);

    allBookmarksButton = new oclero::qlementine::PopoverButton(tr("All bookmarks"), QIcon(":/icons/folder"), this);
    allBookmarksButton->setToolTip(tr("All bookmarks"));
    allBookmarksButton->setFlat(true);
    allBookmarksButton->setFixedHeight(24);
    allBookmarksButton->setShowArrowIndicator(false);
    allBookmarksButton->setIconSize(QSize(18, 18));
    {
        const QString label = tr("All bookmarks");
        const QFontMetrics fm(allBookmarksButton->font());
        const int textW = fm.horizontalAdvance(label);
        allBookmarksButton->setMinimumWidth(textW + allBookmarksButton->iconSize().width() + 40);
    }
    auto* bookmarksPopover = allBookmarksButton->popover();
    bookmarksPopover->setPreferredPosition(oclero::qlementine::Popover::Position::Bottom);
    bookmarksPopover->setPreferredAlignment(oclero::qlementine::Popover::Alignment::End);
    allBookmarksButton->setPopoverContentWidget(bookmarksPopoverContent);

    bookmarksButtonsContainer = new QWidget(this);
    auto bookmarksLayout = new QHBoxLayout(bookmarksButtonsContainer);
    bookmarksLayout->setContentsMargins(4, 0, 4, 0);
    bookmarksLayout->setSpacing(4);
    bookmarksLayout->setAlignment(Qt::AlignVCenter);

    bookmarksScrollArea = new NoScrollClipArea(this);
    bookmarksScrollArea->setWidget(bookmarksButtonsContainer);
    bookmarksScrollArea->setWidgetResizable(true);
    bookmarksScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bookmarksScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bookmarksScrollArea->setFrameShape(QFrame::NoFrame);
    bookmarksScrollArea->setFixedHeight(24);
    bookmarksScrollArea->setContextMenuPolicy(Qt::DefaultContextMenu);
    bookmarksScrollArea->viewport()->setContextMenuPolicy(Qt::NoContextMenu);
    bookmarksScrollArea->installEventFilter(this);

    auto sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedSize(1, 20);

    bookmarksBarContainer = new QWidget(this);
    bookmarksBarContainer->setFixedHeight(28);
    auto barLayout = new QHBoxLayout(bookmarksBarContainer);
    barLayout->setContentsMargins(2, 2, 2, 2);
    barLayout->setSpacing(4);
    barLayout->setAlignment(Qt::AlignVCenter);
    barLayout->addWidget(bookmarksScrollArea, 1, Qt::AlignVCenter);
    barLayout->addWidget(sep2, 0, Qt::AlignVCenter);
    barLayout->addWidget(allBookmarksButton, 0, Qt::AlignVCenter);

    bookmarksSection = new QWidget(this);
    auto sectionLayout = new QVBoxLayout(bookmarksSection);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(0);
    sectionLayout->addWidget(bookmarksBarContainer);
    sectionLayout->addSpacing(4);

    QSettings settings("Adaptix", "Browser");
    bookmarksSection->setVisible(settings.value("bookmarksBarVisible", true).toBool());
    const bool tabsBarVisible = settings.value("browserTabsBarVisible", true).toBool();

    auto bookmarksToggleBtn = new QToolButton(this);
    bookmarksToggleBtn->setIcon(QIcon(bookmarksSection->isVisible() ? ":/icons/bookmark_filled_64dp"
                                                                   : ":/icons/bookmark_64dp"));
    bookmarksToggleBtn->setIconSize(QSize(18, 18));
    bookmarksToggleBtn->setToolTip(tr("Show/hide bookmarks bar"));
    bookmarksToggleBtn->setAutoRaise(true);
    connect(bookmarksToggleBtn, &QToolButton::clicked, this, [this, bookmarksToggleBtn]() {
        bookmarksSection->setVisible(!bookmarksSection->isVisible());
        QSettings("Adaptix", "Browser").setValue("bookmarksBarVisible", bookmarksSection->isVisible());
        bookmarksToggleBtn->setIcon(QIcon(bookmarksSection->isVisible() ? ":/icons/bookmark_filled_64dp"
                                                                        : ":/icons/bookmark_64dp"));
    });
    navLayout->addWidget(bookmarksToggleBtn, 0, navAlign);

    auto* tabsBarToggleBtn = new QToolButton(this);
    tabsBarToggleBtn->setIcon(QIcon(tabsBarVisible ? ":/icons/tab_browser_filled_64dp"
                                                  : ":/icons/tab_browser_64dp"));
    tabsBarToggleBtn->setIconSize(QSize(18, 18));
    tabsBarToggleBtn->setToolTip(tr("Show/hide tab bar"));
    tabsBarToggleBtn->setAutoRaise(true);
    connect(tabsBarToggleBtn, &QToolButton::clicked, this, [this, tabsBarToggleBtn]() {
        if (!browserTabBarRow)
            return;
        browserTabBarRow->setVisible(!browserTabBarRow->isVisible());
        QSettings("Adaptix", "Browser").setValue("browserTabsBarVisible", browserTabBarRow->isVisible());
        tabsBarToggleBtn->setIcon(QIcon(browserTabBarRow->isVisible() ? ":/icons/tab_browser_filled_64dp"
                                                                       : ":/icons/tab_browser_64dp"));
    });
    navLayout->addWidget(tabsBarToggleBtn, 0, navAlign);

    auto* browserTabsHost = new QWidget(this);
    auto* tabsHostLay = new QVBoxLayout(browserTabsHost);
    tabsHostLay->setContentsMargins(0, 0, 0, 0);
    tabsHostLay->setSpacing(0);

    browserTabBarRow = new QWidget(browserTabsHost);
    auto* tabBarRowLay = new QHBoxLayout(browserTabBarRow);
    tabBarRowLay->setContentsMargins(0, 0, 4, 0);
    tabBarRowLay->setSpacing(4);

    browserTabBar = new QTabBar(browserTabBarRow);
    browserTabBar->setDocumentMode(true);
    browserTabBar->setTabsClosable(true);
    browserTabBar->setMovable(true);
    browserTabBar->setUsesScrollButtons(true);
    connect(browserTabBar, &QTabBar::currentChanged, this, &EmbeddableBrowserWidget::onBrowserTabChanged);
    connect(browserTabBar, &QTabBar::tabCloseRequested, this, &EmbeddableBrowserWidget::onTabCloseRequested);
    connect(browserTabBar, &QTabBar::tabMoved, this, &EmbeddableBrowserWidget::onBrowserTabMoved);
    tabBarRowLay->addWidget(browserTabBar, 1);

    auto* newTabBtn = new QToolButton(browserTabBarRow);
    newTabBtn->setText(QStringLiteral("+"));
    newTabBtn->setToolTip(tr("New tab (Ctrl+T)"));
    newTabBtn->setAutoRaise(true);
    newTabBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    newTabBtn->setFocusPolicy(Qt::NoFocus);
    if (auto* qst = qobject_cast<oclero::qlementine::QlementineStyle*>(QApplication::style())) {
        const int h = qMax(28, qst->theme().controlHeightLarge);
        newTabBtn->setFixedSize(h + 4, h);
    } else {
        newTabBtn->setFixedSize(32, 28);
    }
    {
        QFont f = newTabBtn->font();
        f.setPointSizeF(f.pointSizeF() + 3.);
        f.setBold(true);
        newTabBtn->setFont(f);
    }
    connect(newTabBtn, &QToolButton::clicked, this, &EmbeddableBrowserWidget::onNewTab);
    tabBarRowLay->addWidget(newTabBtn, 0, Qt::AlignVCenter);
    tabsHostLay->addWidget(browserTabBarRow);
    browserTabBarRow->setVisible(tabsBarVisible);

    auto* newTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
    newTabShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(newTabShortcut, &QShortcut::activated, this, &EmbeddableBrowserWidget::onNewTab);

    browserTabStack = new QStackedWidget(browserTabsHost);
    tabsHostLay->addWidget(browserTabStack, 1);

    auto* firstTabPage = new QWidget(browserTabStack);
    auto* firstLay = new QVBoxLayout(firstTabPage);
    firstLay->setContentsMargins(0, 0, 0, 0);
    firstLay->setSpacing(0);
    webView = new QWebEngineView(firstTabPage);
    webView->setContextMenuPolicy(Qt::DefaultContextMenu);
    firstLay->addWidget(webView);
    browserTabStack->addWidget(firstTabPage);
    browserTabBar->addTab(tr("New tab"));

    webView->setPage(new BrowserPage(webView->page()->profile(), webView, devToolsView->page(), this, webView));
    browserSharedProfile = webView->page()->profile();
    connectViewSignals(webView);

    verticalSplitter = new QSplitter(Qt::Vertical, this);
    verticalSplitter->addWidget(browserTabsHost);
    verticalSplitter->addWidget(devToolsView);
    verticalSplitter->setStretchFactor(0, 1);
    verticalSplitter->setStretchFactor(1, 0);
    verticalSplitter->setSizes({10000, 0});
    devToolsView->setVisible(false);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(proxyPanel);
    mainLayout->addWidget(bookmarksSection);
    mainLayout->addWidget(verticalSplitter, 1);

    updateSeparateWindowButtonCheckedState();
}

QWebEngineView* EmbeddableBrowserWidget::currentWebView() const
{
    if (!browserTabStack)
        return webView;
    QWidget* page = browserTabStack->currentWidget();
    if (!page)
        return nullptr;
    return page->findChild<QWebEngineView*>(QString(), Qt::FindDirectChildrenOnly);
}

QWebEngineView* EmbeddableBrowserWidget::viewAtTabIndex(int index) const
{
    if (!browserTabStack || index < 0 || index >= browserTabStack->count())
        return nullptr;
    QWidget* page = browserTabStack->widget(index);
    if (!page)
        return nullptr;
    return page->findChild<QWebEngineView*>(QString(), Qt::FindDirectChildrenOnly);
}

int EmbeddableBrowserWidget::tabIndexForView(QWebEngineView* view) const
{
    if (!view || !browserTabStack)
        return -1;
    QWidget* tabPage = view->parentWidget();
    return browserTabStack->indexOf(tabPage);
}

void EmbeddableBrowserWidget::connectViewSignals(QWebEngineView* view)
{
    if (!view)
        return;
    connect(view, &QWebEngineView::urlChanged, this, &EmbeddableBrowserWidget::onUrlChanged);
    connect(view, &QWebEngineView::loadStarted, this, &EmbeddableBrowserWidget::onLoadStarted);
    connect(view, &QWebEngineView::loadFinished, this, &EmbeddableBrowserWidget::onLoadFinished);
    connect(view, &QWebEngineView::titleChanged, this, &EmbeddableBrowserWidget::onPageTitleChanged);
}

void EmbeddableBrowserWidget::disconnectViewSignals(QWebEngineView* view)
{
    if (!view)
        return;
    disconnect(view, nullptr, this, nullptr);
}

void EmbeddableBrowserWidget::updateTabTitleForView(QWebEngineView* view)
{
    if (!view || !browserTabBar)
        return;
    const int idx = tabIndexForView(view);
    if (idx < 0)
        return;
    QString t = view->title().trimmed();
    if (t.isEmpty()) {
        const QString u = view->url().toString();
        t = u.isEmpty() ? tr("New tab") : u;
    }
    if (t.length() > 28)
        t = t.left(25) + QChar(0x2026);
    browserTabBar->setTabText(idx, t);
    browserTabBar->setTabToolTip(idx, view->url().toString());
}

QWebEnginePage* EmbeddableBrowserWidget::createNewTabPage()
{
    if (!browserTabStack || !browserTabBar || !browserSharedProfile)
        return nullptr;
    auto* tabPage = new QWidget(browserTabStack);
    auto* layout = new QVBoxLayout(tabPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* view = new QWebEngineView(tabPage);
    view->setContextMenuPolicy(Qt::DefaultContextMenu);
    layout->addWidget(view);
    auto* page = new BrowserPage(browserSharedProfile, view, devToolsView ? devToolsView->page() : nullptr, this, view);
    view->setPage(page);
    if (devToolsVisible && devToolsView && devToolsView->page())
        page->setDevToolsPage(devToolsView->page());
    browserTabStack->addWidget(tabPage);
    browserTabBar->addTab(tr("New tab"));
    browserTabBar->setCurrentIndex(browserTabBar->count() - 1);
    webView = view;
    connectViewSignals(view);
    return page;
}

void EmbeddableBrowserWidget::syncDevToolsToCurrentTab()
{
    if (!devToolsView || !devToolsView->page() || !browserTabStack)
        return;
    for (int i = 0; i < browserTabStack->count(); ++i) {
        if (QWebEngineView* v = viewAtTabIndex(i)) {
            if (v->page())
                v->page()->setDevToolsPage(nullptr);
        }
    }
    if (devToolsVisible) {
        QWebEngineView* v = currentWebView();
        if (v && v->page())
            v->page()->setDevToolsPage(devToolsView->page());
    }
}

void EmbeddableBrowserWidget::onBrowserTabChanged(int index)
{
    if (!browserTabStack || index < 0 || index >= browserTabStack->count())
        return;
    browserTabStack->setCurrentIndex(index);
    webView = currentWebView();
    syncDevToolsToCurrentTab();
    if (webView) {
        const QUrl u = webView->url();
        const QString barText = isInternalHomeUrl(u) ? QStringLiteral("adaptix:home") : u.toString();
        QSignalBlocker b(urlBar);
        urlBar->setText(barText);
    }
    updateNavigationActions();
}

void EmbeddableBrowserWidget::onTabCloseRequested(int index)
{
    if (!browserTabBar || !browserTabStack || index < 0 || index >= browserTabBar->count())
        return;
    if (browserTabBar->count() <= 1) {
        browserTabBar->setCurrentIndex(index);
        browserTabStack->setCurrentIndex(index);
        webView = viewAtTabIndex(index);
        loadHomePage();
        return;
    }
    QWidget* tabPage = browserTabStack->widget(index);
    if (QWebEngineView* v = viewAtTabIndex(index))
        disconnectViewSignals(v);
    browserTabBar->removeTab(index);
    browserTabStack->removeWidget(tabPage);
    tabPage->deleteLater();
    webView = currentWebView();
    syncDevToolsToCurrentTab();
    updateNavigationActions();
}

void EmbeddableBrowserWidget::onBrowserTabMoved(int from, int to)
{
    if (!browserTabStack || from == to || from < 0 || to < 0)
        return;
    if (from >= browserTabStack->count() || to >= browserTabStack->count())
        return;
    QWidget* w = browserTabStack->widget(from);
    browserTabStack->removeWidget(w);
    browserTabStack->insertWidget(to, w);
}

void EmbeddableBrowserWidget::onNewTab()
{
    createNewTabPage();
    loadHomePage();
}

void EmbeddableBrowserWidget::onPageTitleChanged(const QString&)
{
    auto* v = qobject_cast<QWebEngineView*>(sender());
    if (v)
        updateTabTitleForView(v);
}

void EmbeddableBrowserWidget::applyProxy()
{
    if (currentProxyType == QNetworkProxy::NoProxy || currentProxyHost.isEmpty()) {
        clearProxy();
        updateProxyButtonCheckedState();
        return;
    }

    QNetworkProxy proxy;
    proxy.setType(currentProxyType);
    proxy.setHostName(currentProxyHost);
    proxy.setPort(currentProxyPort);
    QNetworkProxy::setApplicationProxy(proxy);
    updateProxyButtonCheckedState();
}

void EmbeddableBrowserWidget::clearProxy()
{
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
}

QUrl EmbeddableBrowserWidget::urlFromUserInput(const QString& input) const
{
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return QUrl();

    if (!trimmed.contains(".") && !trimmed.startsWith("http") && !trimmed.startsWith("file")) {
        return QUrl("https://www.google.com/search?q=" + QUrl::toPercentEncoding(trimmed));
    }

    QUrl url(trimmed);
    if (url.scheme().isEmpty())
        url.setScheme("https");
    return url;
}

void EmbeddableBrowserWidget::loadUrl(const QUrl& url)
{
    if (!url.isValid() || url.isEmpty())
        return;

    applyProxy();
    if (QWebEngineView* v = currentWebView())
        v->setUrl(url);
}

void EmbeddableBrowserWidget::loadUrl(const QString& url)
{
    loadUrl(urlFromUserInput(url));
}

void EmbeddableBrowserWidget::setProxy(QNetworkProxy::ProxyType type, const QString& host, quint16 port)
{
    currentProxyType = type;
    currentProxyHost = host.trimmed();
    currentProxyPort = port;

    int idx = proxyCombo->findData(type);
    if (idx >= 0)
        proxyCombo->setCurrentIndex(idx);
    proxyHostEdit->setText(currentProxyHost);
    proxyPortEdit->setText(currentProxyPort > 0 ? QString::number(currentProxyPort) : QString());
    updateProxyButtonCheckedState();
}

EmbeddableBrowserWidget* EmbeddableBrowserWidget::create(AdaptixWidget* w, const QString& title, const QString& url, const QString& proxyHost, quint16 proxyPort)
{
    auto* browser = new EmbeddableBrowserWidget(w, title, url);
    if (!proxyHost.isEmpty() && proxyPort > 0) {
        browser->setProxy(QNetworkProxy::Socks5Proxy, proxyHost, proxyPort);
    }
    return browser;
}

void EmbeddableBrowserWidget::onUrlChanged(const QUrl& url)
{
    auto* v = qobject_cast<QWebEngineView*>(sender());
    if (!v)
        return;
    if (v == currentWebView()) {
        const QString barText = isInternalHomeUrl(url) ? QStringLiteral("adaptix:home") : url.toString();
        if (barText != urlBar->text()) {
            urlBar->setText(barText);
            QTimer::singleShot(0, urlBar, [this]() {
                urlBar->setCursorPosition(0);
                urlBar->deselect();
            });
        }
        updateNavigationActions();
    }
    updateTabTitleForView(v);
}

void EmbeddableBrowserWidget::onLoadStarted()
{
    reloadButton->setEnabled(false);
}

void EmbeddableBrowserWidget::onLoadFinished(bool ok)
{
    Q_UNUSED(ok);
    auto* v = qobject_cast<QWebEngineView*>(sender());
    if (!v || v != currentWebView())
        return;
    reloadButton->setEnabled(true);
    updateNavigationActions();
}

void EmbeddableBrowserWidget::updateNavigationActions()
{
    QWebEngineView* v = currentWebView();
    if (!v || !v->page())
        return;
    backButton->setDefaultAction(nullptr);
    forwardButton->setDefaultAction(nullptr);
    auto* backAction = v->page()->action(QWebEnginePage::Back);
    auto* forwardAction = v->page()->action(QWebEnginePage::Forward);
    backButton->setEnabled(backAction->isEnabled());
    forwardButton->setEnabled(forwardAction->isEnabled());
    backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    forwardButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    updateBookmarkStarButton();
}

void EmbeddableBrowserWidget::onNavigate()
{
    const QString typed = urlBar->text().trimmed();
    if (typed.compare(QStringLiteral("adaptix:home"), Qt::CaseInsensitive) == 0) {
        loadHomePage();
        return;
    }
    QUrl url = urlFromUserInput(typed);
    if (url.isValid())
        loadUrl(url);
}

void EmbeddableBrowserWidget::onProxyApply()
{
    currentProxyType = static_cast<QNetworkProxy::ProxyType>(proxyCombo->currentData().toInt());
    currentProxyHost = proxyHostEdit->text().trimmed();
    currentProxyPort = static_cast<quint16>(proxyPortEdit->text().toUInt());

    applyProxy();
    if (QWebEngineView* v = currentWebView())
        v->reload();
}

void EmbeddableBrowserWidget::onProxyTypeChanged(int index)
{
    Q_UNUSED(index);
    const bool hasProxy = proxyCombo->currentData().toInt() != QNetworkProxy::NoProxy;
    if (proxyHostPortRow)
        proxyHostPortRow->setVisible(hasProxy);
    proxyHostEdit->setEnabled(hasProxy);
    proxyPortEdit->setEnabled(hasProxy);

    if (proxyPopover && proxyPopover->isOpened()) {
        if (QWidget* cw = proxyPopover->contentWidget())
            cw->updateGeometry();
        QTimer::singleShot(0, this, [this]() {
            if (proxyPopover && proxyPopover->isOpened())
                proxyPopover->relayoutToContent();
        });
    }
}

void EmbeddableBrowserWidget::refreshProxyTunnelCombo()
{
    if (!proxyTunnelCombo || !adaptixWidget)
        return;

    const QVector<TunnelData> tunnelSource = tunnelsForBrowserProxyPicker(adaptixWidget);

    QString preferredTunnelId;
    const int proxyType = proxyCombo ? proxyCombo->currentData().toInt() : static_cast<int>(QNetworkProxy::NoProxy);
    if (proxyType == static_cast<int>(QNetworkProxy::Socks5Proxy) && proxyHostEdit && proxyPortEdit) {
        const QString host = proxyHostEdit->text().trimmed();
        bool ok = false;
        const quint16 port = proxyPortEdit->text().toUShort(&ok);
        if (ok && port != 0) {
            for (const TunnelData& t : tunnelSource) {
                if (!tunnelTypeIsSocks5ForBrowser(t.Type))
                    continue;
                bool pok = false;
                const quint16 tp = t.Port.toUShort(&pok);
                if (!pok || tp != port)
                    continue;
                if (normalizeTunnelBindHostForClient(t.Interface) == host) {
                    preferredTunnelId = t.TunnelId;
                    break;
                }
            }
        }
    }

    const QSignalBlocker blocker(proxyTunnelCombo);
    proxyTunnelCombo->clear();
    proxyTunnelCombo->addItem(tr("Custom (manual host/port)"), kBrowserProxyTunnelCustomMarker);

    for (const TunnelData& t : tunnelSource) {
        if (!tunnelTypeIsSocks5ForBrowser(t.Type))
            continue;
        bool pok = false;
        const quint16 tp = t.Port.toUShort(&pok);
        if (!pok || tp == 0)
            continue;
        const QString iface = t.Interface.trimmed();
        const QString hostPart = iface.isEmpty() ? QStringLiteral("0.0.0.0") : iface;
        const QString label = QStringLiteral("%1:%2 — %3").arg(
            hostPart, t.Port, t.Computer.isEmpty() ? t.TunnelId.left(8) : t.Computer);
        proxyTunnelCombo->addItem(label, t.TunnelId);
    }

    int idx = 0;
    if (!preferredTunnelId.isEmpty()) {
        for (int i = 0; i < proxyTunnelCombo->count(); ++i) {
            if (proxyTunnelCombo->itemData(i).toString() == preferredTunnelId) {
                idx = i;
                break;
            }
        }
    }
    proxyTunnelCombo->setCurrentIndex(idx);
}

void EmbeddableBrowserWidget::onProxyTunnelPicked(int index)
{
    if (!proxyTunnelCombo || index < 0 || !adaptixWidget)
        return;
    const QString tunnelId = proxyTunnelCombo->itemData(index).toString();
    if (tunnelId == kBrowserProxyTunnelCustomMarker || tunnelId.isEmpty()) {
        if (proxyHostEdit)
            proxyHostEdit->clear();
        if (proxyPortEdit)
            proxyPortEdit->clear();
        onProxyTypeChanged(proxyCombo->currentIndex());
        return;
    }

    const TunnelData t = tunnelByIdForBrowser(adaptixWidget, tunnelId);
    if (t.TunnelId.isEmpty())
        return;

    bool pok = false;
    const quint16 port = t.Port.toUShort(&pok);
    if (!pok || port == 0)
        return;

    {
        QSignalBlocker b(proxyCombo);
        const int s5 = proxyCombo->findData(static_cast<int>(QNetworkProxy::Socks5Proxy));
        if (s5 >= 0)
            proxyCombo->setCurrentIndex(s5);
    }

    proxyHostEdit->setText(normalizeTunnelBindHostForClient(t.Interface));
    proxyPortEdit->setText(QString::number(port));

    onProxyTypeChanged(proxyCombo->currentIndex());
}

void EmbeddableBrowserWidget::loadBookmarks()
{
    bookmarksList->clear();
    QSettings settings("Adaptix", "Browser");
    int size = settings.beginReadArray("bookmarks");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString title = settings.value("title").toString();
        QString url = settings.value("url").toString();
        if (!url.isEmpty()) {
            auto* item = new QListWidgetItem(title.isEmpty() ? url : title);
            item->setData(Qt::UserRole, url);
            item->setToolTip(url);
            bookmarksList->addItem(item);
        }
    }
    settings.endArray();
    refreshBookmarksBar();
}

void EmbeddableBrowserWidget::refreshBookmarksBar()
{
    QLayoutItem* item;
    while ((item = bookmarksButtonsContainer->layout()->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    auto* layout = static_cast<QHBoxLayout*>(bookmarksButtonsContainer->layout());
    for (int i = 0; i < bookmarksList->count(); ++i) {
        if (i > 0) {
            auto* sep = new QFrame(bookmarksButtonsContainer);
            sep->setFrameShape(QFrame::VLine);
            sep->setFixedSize(1, 16);
            sep->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(sep, &QFrame::customContextMenuRequested, this, [this, sep, i](const QPoint& pos) {
                if (i - 1 >= 0 && i - 1 < bookmarksList->count())
                    showBookmarkContextMenu(sep->mapToGlobal(pos), bookmarksList->item(i - 1));
            });
            layout->addWidget(sep, 0, Qt::AlignVCenter);
        }
        auto* listItem = bookmarksList->item(i);
        QString title = listItem->text();
        QString url = listItem->data(Qt::UserRole).toString();

        auto* btn = new QToolButton(bookmarksButtonsContainer);
        btn->setText(title);
        btn->setToolTip(url);
        btn->setAutoRaise(true);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setFixedHeight(22);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QToolButton::clicked, this, [this, url]() { loadUrl(url); });
        connect(btn, &QToolButton::customContextMenuRequested, this, [this, btn, listItem](const QPoint& pos) {
            showBookmarkContextMenu(btn->mapToGlobal(pos), listItem);
        });
        layout->addWidget(btn, 0, Qt::AlignVCenter);
    }
    layout->addStretch();
    QTimer::singleShot(0, this, &EmbeddableBrowserWidget::trimBookmarksToFit);
    updateBookmarkStarButton();
}

void EmbeddableBrowserWidget::updateBookmarkStarButton()
{
    if (!bookmarkUrlBtn || !bookmarksList)
        return;
    QWebEngineView* wv = currentWebView();
    if (!wv) {
        bookmarkUrlBtn->setIcon(QIcon(QStringLiteral(":/icons/star_64dp")));
        return;
    }
    const QUrl url = wv->url();
    if (isInternalHomeUrl(url) || !url.isValid() || url.isEmpty() || url.scheme() == QLatin1String("about")) {
        bookmarkUrlBtn->setIcon(QIcon(QStringLiteral(":/icons/star_64dp")));
        return;
    }
    const QString urlStr = url.toString();
    for (int i = 0; i < bookmarksList->count(); ++i) {
        if (bookmarksList->item(i)->data(Qt::UserRole).toString() == urlStr) {
            bookmarkUrlBtn->setIcon(QIcon(QStringLiteral(":/icons/star_filled_64dp")));
            return;
        }
    }
    bookmarkUrlBtn->setIcon(QIcon(QStringLiteral(":/icons/star_64dp")));
}

void EmbeddableBrowserWidget::trimBookmarksToFit()
{
    if (!bookmarksScrollArea || !bookmarksButtonsContainer)
        return;
    int viewportW = bookmarksScrollArea->viewport()->width();
    auto* layout = static_cast<QHBoxLayout*>(bookmarksButtonsContainer->layout());
    if (!layout || layout->count() == 0)
        return;
    bookmarksButtonsContainer->adjustSize();
    while (bookmarksButtonsContainer->sizeHint().width() > viewportW && layout->count() > 1) {
        QLayoutItem* item = layout->takeAt(layout->count() - 1);
        if (item) {
            if (item->spacerItem())
                delete item;
            else {
                if (item->widget())
                    item->widget()->deleteLater();
                delete item;
            }
        }
        item = layout->takeAt(layout->count() - 1);
        if (item && item->widget())
            item->widget()->deleteLater();
        delete item;
        if (layout->count() > 0) {
            item = layout->takeAt(layout->count() - 1);
            if (item && item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        bookmarksButtonsContainer->adjustSize();
    }
    layout->addStretch();
}

bool EmbeddableBrowserWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == backButton && m_backLongPressTimer) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_backIgnoreNextButtonRelease = false;
                m_backLongPressTimer->start();
            }
            return false;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_backLongPressTimer->stop();
                if (m_backIgnoreNextButtonRelease) {
                    m_backIgnoreNextButtonRelease = false;
                } else {
                    QWebEngineView* v = currentWebView();
                    if (v && v->page() && v->page()->action(QWebEnginePage::Back)->isEnabled())
                        v->page()->triggerAction(QWebEnginePage::Back);
                }
            }
            return false;
        }
        if (event->type() == QEvent::Leave)
            m_backLongPressTimer->stop();
    }
    if (watched == bookmarksScrollArea && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &EmbeddableBrowserWidget::refreshBookmarksBar);
    return QWidget::eventFilter(watched, event);
}

void EmbeddableBrowserWidget::onBackLongPressTimeout()
{
    if (!(QApplication::mouseButtons() & Qt::LeftButton))
        return;
    if (!backButton->underMouse())
        return;
    QWebEnginePage* page = currentWebView() ? currentWebView()->page() : nullptr;
    if (!page)
        return;
    QWebEngineHistory* hist = page->history();
    if (!hist || !hist->canGoBack())
        return;
    const QList<QWebEngineHistoryItem> items = hist->backItems(25);
    if (items.isEmpty())
        return;

    m_backIgnoreNextButtonRelease = true;
    oclero::qlementine::Menu menu(this);
    for (const QWebEngineHistoryItem& it : items) {
        QString t = it.title();
        if (t.isEmpty())
            t = it.url().toString();
        QAction* a = menu.addAction(t);
        a->setToolTip(it.url().toString());
        connect(a, &QAction::triggered, this, [hist, it]() {
            hist->goToItem(it);
        });
    }
    const QPoint pos = backButton->mapToGlobal(QPoint(0, backButton->height()));
    menu.exec(pos);
}

void EmbeddableBrowserWidget::saveBookmarks()
{
    QSettings settings("Adaptix", "Browser");
    settings.beginWriteArray("bookmarks");
    for (int i = 0; i < bookmarksList->count(); ++i) {
        settings.setArrayIndex(i);
        QString url = bookmarksList->item(i)->data(Qt::UserRole).toString();
        QString title = bookmarksList->item(i)->text();
        settings.setValue("url", url);
        settings.setValue("title", title);
    }
    settings.endArray();
    reloadHomePageIfVisible();
}

void EmbeddableBrowserWidget::editBookmarkItem(QListWidgetItem* listItem)
{
    if (!listItem || !bookmarksList || bookmarksList->row(listItem) < 0)
        return;

    const QString title = listItem->text();
    const QString url = listItem->data(Qt::UserRole).toString();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit bookmark"));
    dlg.setSizeGripEnabled(false);
    auto* form = new QFormLayout(&dlg);
    form->setContentsMargins(16, 12, 16, 10);
    form->setSpacing(6);
    form->setVerticalSpacing(8);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto* titleEdit = new oclero::qlementine::LineEdit(&dlg);
    titleEdit->setText(title);
    titleEdit->setMinimumWidth(480);
    auto* urlEdit = new oclero::qlementine::LineEdit(&dlg);
    urlEdit->setText(url);
    urlEdit->setMinimumWidth(480);
    form->addRow(tr("Name:"), titleEdit);
    form->addRow(tr("URL:"), urlEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint());
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString newTitle = titleEdit->text();
    const QString newUrlRaw = urlEdit->text().trimmed();
    if (newUrlRaw.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("URL cannot be empty."));
        return;
    }
    QUrl parsed;
    if (newUrlRaw.startsWith(QStringLiteral("about:"), Qt::CaseInsensitive))
        parsed = QUrl(newUrlRaw);
    else
        parsed = urlFromUserInput(newUrlRaw);
    if (!parsed.isValid() || parsed.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("The entered URL is not valid."));
        return;
    }
    const QString newUrl = parsed.toString();
    if (bookmarksList->row(listItem) < 0)
        return;
    listItem->setText(newTitle);
    listItem->setData(Qt::UserRole, newUrl);
    listItem->setToolTip(newUrl);
    saveBookmarks();
    refreshBookmarksBar();
}

void EmbeddableBrowserWidget::deleteBookmarkListItem(QListWidgetItem* item, bool askConfirm)
{
    if (!item || !bookmarksList)
        return;
    const int row = bookmarksList->row(item);
    if (row < 0)
        return;
    if (askConfirm) {
        if (QMessageBox::question(this, tr("Delete bookmark"),
                                  tr("Delete this shortcut from the home page?"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No)
            != QMessageBox::Yes)
            return;
    }
    delete bookmarksList->takeItem(row);
    saveBookmarks();
    refreshBookmarksBar();
}

void EmbeddableBrowserWidget::handleHomeBookmarkEdit(int row)
{
    if (!bookmarksList || row < 0 || row >= bookmarksList->count())
        return;
    QListWidgetItem* item = bookmarksList->item(row);
    if (!item)
        return;
    editBookmarkItem(item);
}

void EmbeddableBrowserWidget::handleHomeBookmarkDelete(int row)
{
    if (!bookmarksList || row < 0 || row >= bookmarksList->count())
        return;
    QListWidgetItem* item = bookmarksList->item(row);
    if (!item)
        return;
    deleteBookmarkListItem(item, true);
}

bool EmbeddableBrowserWidget::isInternalHomeUrl(const QUrl& url) const
{
    return url.scheme() == QStringLiteral("adaptix-browser");
}

void EmbeddableBrowserWidget::loadHomePage()
{
    QWebEngineView* v = currentWebView();
    if (!v)
        return;
    const QUrl base(QStringLiteral("adaptix-browser://home/"));
    v->setHtml(buildBookmarksHomeHtml(), base);
}

void EmbeddableBrowserWidget::reloadHomePageIfVisible()
{
    if (!browserTabStack)
        return;
    const QString html = buildBookmarksHomeHtml();
    const QUrl base(QStringLiteral("adaptix-browser://home/"));
    for (int i = 0; i < browserTabStack->count(); ++i) {
        if (QWebEngineView* v = viewAtTabIndex(i)) {
            if (isInternalHomeUrl(v->url()))
                v->setHtml(html, base);
        }
    }
}

QString EmbeddableBrowserWidget::buildBookmarksHomeHtml() const
{
    const QString editIconUri = resourcePngAsDataUri(QStringLiteral(":/icons/edit_64dp"));
    const QString deleteIconUri = resourcePngAsDataUri(QStringLiteral(":/icons/delete_64dp"));

    QString tiles;
    for (int i = 0; i < bookmarksList->count(); ++i) {
        const QListWidgetItem* it = bookmarksList->item(i);
        const QString urlStr = it->data(Qt::UserRole).toString();
        const QString title = it->text();
        if (urlStr.isEmpty())
            continue;
        const QUrl u(urlStr);
        if (!u.isValid())
            continue;

        QString letter = QStringLiteral("?");
        for (const QChar& c : title) {
            if (c.isLetterOrNumber()) {
                letter = QString(c).toUpper();
                break;
            }
        }

        const QString host = u.host();
        tiles += QLatin1String(R"(<div class="tile-wrap">)");
        tiles += QLatin1String(R"(<div class="tile-actions">)");
        tiles += QLatin1String("<a class=\"tile-action edit\" href=\"adaptix-browser://edit/?i=");
        tiles += QString::number(i);
        tiles += QLatin1String("\" title=\"");
        tiles += tr("Edit").toHtmlEscaped();
        tiles += QLatin1String("\">");
        if (!editIconUri.isEmpty())
            tiles += QLatin1String("<img class=\"tile-action-icon\" src=\"") + editIconUri + QLatin1String("\" alt=\"\">");
        else
            tiles += QLatin1String("&#9998;");
        tiles += QLatin1String("</a><a class=\"tile-action del\" href=\"adaptix-browser://delete/?i=");
        tiles += QString::number(i);
        tiles += QLatin1String("\" title=\"");
        tiles += tr("Delete").toHtmlEscaped();
        tiles += QLatin1String("\">");
        if (!deleteIconUri.isEmpty())
            tiles += QLatin1String("<img class=\"tile-action-icon\" src=\"") + deleteIconUri + QLatin1String("\" alt=\"\">");
        else
            tiles += QLatin1String("&times;");
        tiles += QLatin1String("</a></div>");
        tiles += QLatin1String("<a class=\"tile\" href=\"");
        tiles += QString(urlStr).toHtmlEscaped();
        tiles += QLatin1String("\"><div class=\"ico\">");
        if (!host.isEmpty()) {
            tiles += QLatin1String(R"(<img alt="" src="https://www.google.com/s2/favicons?sz=64&amp;domain=)");
            tiles += QString::fromUtf8(QUrl::toPercentEncoding(host));
            tiles += QLatin1String(R"(" onerror="this.style.display='none';this.nextElementSibling.style.display='flex'">)");
        }
        tiles += QLatin1String(R"(<span class="letter" style="display:)");
        tiles += host.isEmpty() ? QLatin1String("flex") : QLatin1String("none");
        tiles += QLatin1String(R"(">)");
        tiles += letter.toHtmlEscaped();
        tiles += QLatin1String(R"(</span></div><div class="label">)");
        tiles += title.toHtmlEscaped();
        tiles += QLatin1String("</div></a></div>");
    }

    const QString emptyMsg = tr("No bookmarks yet. Use the \"Add shortcut\" tile below or the link button in the address bar.").toHtmlEscaped();
    const QString heading = tr("Adaptix Homepage").toHtmlEscaped();
    const QString addLabel = tr("Add shortcut").toHtmlEscaped();
    QString addTile = QLatin1String(R"(<a class="tile add-shortcut" href="adaptix-browser://add/">)"
                                    R"(<div class="ico"><span class="plus">+</span></div>)"
                                    R"(<div class="label">)");
    addTile += addLabel;
    addTile += QLatin1String("</div></a>");

    QString bodyInner = QLatin1String("<div class=\"grid\">") + tiles + addTile + QLatin1String("</div>");
    if (tiles.isEmpty())
        bodyInner += QLatin1String("<p class=\"empty\">") + emptyMsg + QLatin1String("</p>");

    const QString pageTitle = tr("Adaptix Homepage").toHtmlEscaped();

    return QStringLiteral("<!DOCTYPE html><html><head><title>")
        + pageTitle
        + QStringLiteral("</title><meta charset=\"utf-8\"><meta name=\"color-scheme\" content=\"dark\">"
               "<style>"
               "*{box-sizing:border-box;}"
               "body{margin:0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
               "background:#202124;color:#e8eaed;-webkit-font-smoothing:antialiased;}"
               "main{max-width:960px;margin:0 auto;padding:40px 28px 72px;}"
               "h1{font-size:22px;font-weight:400;margin:0 0 28px;text-align:center;color:#9aa0a6;}"
               ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(112px,1fr));"
               "gap:12px 20px;justify-items:center;}"
               ".tile-wrap{position:relative;width:112px;display:flex;justify-content:center;}"
               ".tile-actions{position:absolute;top:2px;right:2px;display:flex;gap:3px;z-index:2;"
               "opacity:.4;transition:opacity .15s ease;}"
               ".tile-wrap:hover .tile-actions{opacity:1;}"
               ".tile-action{font-size:13px;line-height:1;min-width:22px;height:22px;padding:0 4px;display:flex;"
               "align-items:center;justify-content:center;text-decoration:none;color:#e8eaed;"
               "background:rgba(0,0,0,.5);border-radius:6px;border:none;cursor:pointer;}"
               ".tile-action:hover{background:rgba(66,133,244,.75);}"
               ".tile-action.del:hover{background:rgba(234,67,53,.8);}"
               ".tile-action img.tile-action-icon{width:14px;height:14px;object-fit:contain;display:block;}"
               "a.tile{text-decoration:none;color:inherit;display:flex;flex-direction:column;align-items:center;"
               "width:112px;padding:14px 8px;border-radius:12px;transition:background .12s ease;}"
               "a.tile:hover{background:rgba(255,255,255,.08);}"
               ".ico{width:64px;height:64px;border-radius:50%;background:rgba(255,255,255,.12);"
               "display:flex;align-items:center;justify-content:center;overflow:hidden;margin-bottom:10px;}"
               ".ico img{width:36px;height:36px;}"
               ".ico .letter{font-size:24px;font-weight:500;color:#bdc1c6;display:flex;align-items:center;"
               "justify-content:center;width:100%;height:100%;}"
               ".label{font-size:13px;text-align:center;max-width:100%;line-height:1.35;color:#e8eaed;"
               "overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;}"
               ".empty{text-align:center;color:#9aa0a6;padding:24px 16px 40px;font-size:14px;line-height:1.5;max-width:420px;"
               "margin:0 auto;}"
               "a.tile.add-shortcut .ico{border:2px dashed rgba(255,255,255,.22);background:rgba(255,255,255,.05);}"
               "a.tile.add-shortcut .plus{font-size:36px;font-weight:300;color:#9aa0a6;line-height:1;display:flex;"
               "align-items:center;justify-content:center;width:100%;height:100%;}"
               "</style></head><body><main><h1>")
        + heading + QStringLiteral("</h1>") + bodyInner + QStringLiteral("</main></body></html>");
}

void EmbeddableBrowserWidget::onAddBookmark()
{
    QWebEngineView* wv = currentWebView();
    if (!wv)
        return;
    QUrl url = wv->url();
    if (isInternalHomeUrl(url)) {
        promptAddBookmarkFromHome();
        return;
    }
    if (!url.isValid() || url.isEmpty() || url.scheme() == QLatin1String("about"))
        return;

    QString urlStr = url.toString();
    QString title = wv->title();
    if (title.isEmpty())
        title = url.host();

    for (int i = 0; i < bookmarksList->count(); ++i) {
        if (bookmarksList->item(i)->data(Qt::UserRole).toString() == urlStr)
            return;
    }

    auto* item = new QListWidgetItem(title);
    item->setData(Qt::UserRole, urlStr);
    item->setToolTip(urlStr);
    bookmarksList->addItem(item);
    saveBookmarks();
    refreshBookmarksBar();
}

void EmbeddableBrowserWidget::onRemoveBookmark()
{
    int row = bookmarksList->currentRow();
    if (row >= 0) {
        delete bookmarksList->takeItem(row);
        saveBookmarks();
        refreshBookmarksBar();
    }
}

void EmbeddableBrowserWidget::onToggleBookmark()
{
    QWebEngineView* wv = currentWebView();
    if (!wv)
        return;
    QUrl url = wv->url();
    if (isInternalHomeUrl(url)) {
        promptAddBookmarkFromHome();
        return;
    }
    if (!url.isValid() || url.isEmpty() || url.scheme() == QLatin1String("about"))
        return;

    QString urlStr = url.toString();

    for (int i = 0; i < bookmarksList->count(); ++i) {
        if (bookmarksList->item(i)->data(Qt::UserRole).toString() == urlStr) {
            delete bookmarksList->takeItem(i);
            saveBookmarks();
            refreshBookmarksBar();
            return;
        }
    }

    QString title = wv->title();
    if (title.isEmpty())
        title = url.host();

    auto* item = new QListWidgetItem(title);
    item->setData(Qt::UserRole, urlStr);
    item->setToolTip(urlStr);
    bookmarksList->addItem(item);
    saveBookmarks();
    refreshBookmarksBar();
}

void EmbeddableBrowserWidget::onBookmarkClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    QUrl url(item->data(Qt::UserRole).toString());
    if (url.isValid())
        loadUrl(url);
}

void EmbeddableBrowserWidget::openBookmarkContextMenuAt(const QPoint& globalPos, QListWidgetItem* item)
{
    showBookmarkContextMenu(globalPos, item);
}

void EmbeddableBrowserWidget::onAllBookmarksListItemPressed(QListWidgetItem* item)
{
    if (!item)
        return;
    if (!(QApplication::mouseButtons() & Qt::LeftButton))
        return;
    onBookmarkClicked(item);
    if (allBookmarksButton && allBookmarksButton->popover()->isOpened())
        allBookmarksButton->setPopoverOpened(false);
}

void EmbeddableBrowserWidget::showBookmarkContextMenu(const QPoint& globalPos, QListWidgetItem* listItem)
{
    if (!listItem)
        return;

    auto showMenu = [this, listItem, globalPos]() {
        if (!listItem || bookmarksList->row(listItem) < 0)
            return;

        const QString url = listItem->data(Qt::UserRole).toString();

        oclero::qlementine::Menu menu(this);
        menu.setToolTipsVisible(true);

        QAction* editAction = menu.addAction(tr("Edit"));
        QAction* copyAction = menu.addAction(tr("Copy link"));
        QAction* deleteAction = menu.addAction(tr("Delete"));

        connect(editAction, &QAction::triggered, this, [this, listItem]() {
            if (bookmarksList->row(listItem) < 0)
                return;
            editBookmarkItem(listItem);
        });

        connect(copyAction, &QAction::triggered, this, [url]() {
            QApplication::clipboard()->setText(url);
        });

        connect(deleteAction, &QAction::triggered, this, [this, listItem]() {
            deleteBookmarkListItem(listItem, false);
        });

        menu.exec(globalPos);
    };

    QTimer::singleShot(0, this, showMenu);
}

void EmbeddableBrowserWidget::promptAddBookmarkFromHome()
{
    if (!bookmarksList)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add bookmark"));
    dlg.setSizeGripEnabled(false);
    auto* form = new QFormLayout(&dlg);
    form->setContentsMargins(16, 12, 16, 10);
    form->setSpacing(6);
    form->setVerticalSpacing(8);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto* titleEdit = new oclero::qlementine::LineEdit(&dlg);
    titleEdit->setMinimumWidth(480);
    auto* urlEdit = new oclero::qlementine::LineEdit(&dlg);
    urlEdit->setPlaceholderText(QStringLiteral("https://"));
    urlEdit->setMinimumWidth(480);
    form->addRow(tr("Name:"), titleEdit);
    form->addRow(tr("URL:"), urlEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint());
    if (dlg.exec() != QDialog::Accepted)
        return;

    QString title = titleEdit->text().trimmed();
    const QString urlRaw = urlEdit->text().trimmed();
    if (urlRaw.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("URL cannot be empty."));
        return;
    }
    QUrl parsed;
    if (urlRaw.startsWith(QStringLiteral("about:"), Qt::CaseInsensitive))
        parsed = QUrl(urlRaw);
    else
        parsed = urlFromUserInput(urlRaw);
    if (!parsed.isValid() || parsed.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("The entered URL is not valid."));
        return;
    }
    if (parsed.scheme() == QStringLiteral("adaptix-browser")) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("Cannot bookmark the start page or internal browser links."));
        return;
    }
    const QString urlStr = parsed.toString();
    for (int i = 0; i < bookmarksList->count(); ++i) {
        if (bookmarksList->item(i)->data(Qt::UserRole).toString() == urlStr) {
            QMessageBox::information(this, tr("Bookmark"), tr("This URL is already in bookmarks."));
            return;
        }
    }
    if (title.isEmpty()) {
        title = parsed.host();
        if (title.isEmpty())
            title = urlStr;
    }

    auto* item = new QListWidgetItem(title);
    item->setData(Qt::UserRole, urlStr);
    item->setToolTip(urlStr);
    bookmarksList->addItem(item);
    saveBookmarks();
    refreshBookmarksBar();
}

void EmbeddableBrowserWidget::updateDevToolsButtonIcon()
{
    if (!devToolsButton)
        return;
    devToolsButton->setIcon(QIcon(devToolsVisible ? QStringLiteral(":/icons/dev_mode_close_64dp")
                                                  : QStringLiteral(":/icons/dev_mode_64dp")));
}

void EmbeddableBrowserWidget::updateProxyButtonCheckedState()
{
    if (!proxyButton)
        return;
    const bool active = (currentProxyType != QNetworkProxy::NoProxy && !currentProxyHost.trimmed().isEmpty());
    const QSignalBlocker b(proxyButton);
    proxyButton->setChecked(active);
}

void EmbeddableBrowserWidget::updateSeparateWindowButtonCheckedState()
{
    if (!separateWindowButton)
        return;
    const bool floating = (m_browserFloatingWindow != nullptr);
    const QSignalBlocker b(separateWindowButton);
    separateWindowButton->setChecked(floating);
}

void EmbeddableBrowserWidget::onDevToolsToggle()
{
    devToolsVisible = !devToolsVisible;
    devToolsButton->setChecked(devToolsVisible);
    updateDevToolsButtonIcon();
    syncDevToolsToCurrentTab();
    if (devToolsView)
        devToolsView->setVisible(devToolsVisible);
    verticalSplitter->setSizes(devToolsVisible ? QList<int>{400, 300} : QList<int>{10000, 0});
}

void EmbeddableBrowserWidget::clearFloatingWindowReference()
{
    m_browserFloatingWindow = nullptr;
    updateSeparateWindowButtonCheckedState();
}

void EmbeddableBrowserWidget::openBrowserInSeparateWindow()
{
    if (m_browserFloatingWindow) {
        m_browserFloatingWindow->close();
        return;
    }
    if (!dockWidget)
        return;
    dockWidget->setWidget(nullptr);
    auto* host = new AdaptixBrowserFloatingDetail::BrowserFloatingHost(this, adaptixWidget);
    m_browserFloatingWindow = host;
    host->setCentralWidget(this);
    host->setWindowTitle(dockWidget->title());
    host->resize(1280, 840);
    host->show();
    updateSeparateWindowButtonCheckedState();
}

namespace AdaptixBrowserFloatingDetail {

BrowserFloatingHost::BrowserFloatingHost(EmbeddableBrowserWidget* browser, QWidget* parent)
    : QMainWindow(parent)
    , m_browser(browser)
{
    setAttribute(Qt::WA_DeleteOnClose);
}

void BrowserFloatingHost::closeEvent(QCloseEvent* e)
{
    EmbeddableBrowserWidget* const b = m_browser.data();
    if (b) {
        takeCentralWidget();
        if (auto* dw = b->dock())
            dw->setWidget(b);
        b->clearFloatingWindowReference();
    }
    QMainWindow::closeEvent(e);
}

}
