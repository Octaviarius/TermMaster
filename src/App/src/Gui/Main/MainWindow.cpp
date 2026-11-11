#include "MainWindow.h"

#include "../Workspace/WorkspacePage.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <Termaster/Core/Gui/Icons.h>
#include <algorithm>
#include <memory>

namespace
{
/** Native tab geometry and dragging; the complete caption lives in its layout. */
class TerminalTabBar final : public QTabBar
{
public:
    TerminalTabBar()
    {
        qApp->installEventFilter(this);
        _refreshStyle();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        {
            _refreshStyle();
        }
        return QTabBar::eventFilter(watched, event);
    }

private:
    void _refreshStyle()
    {
        const auto   colors     = QApplication::palette();
        const auto   background = colors.color(QPalette::Window);
        const auto   accent     = colors.color(QPalette::Highlight);
        const QColor selected((4 * background.red() + accent.red()) / 5,
                              (4 * background.green() + accent.green()) / 5,
                              (4 * background.blue() + accent.blue()) / 5);
        const auto   sheet = QString("QTabBar::tab { background-color: %1; color: %2; "
                                     "border-right: 1px solid %5; border-bottom: 3px solid transparent; } "
                                     "QTabBar::tab:selected { background-color: %3; border-bottom-color: %4; }")
                                 .arg(background.name(),
                                      colors.color(QPalette::WindowText).name(),
                                      selected.name(),
                                      accent.name(),
                                      colors.color(QPalette::Mid).name());
        if (styleSheet() != sheet)
        {
            setStyleSheet(sheet);
        }
    }
};

class TerminalTabs final : public QTabWidget
{
public:
    explicit TerminalTabs(QWidget* parent) : QTabWidget(parent)
    {
        setTabBar(new TerminalTabBar);
    }
};
} // namespace

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent), _mainMenu(menuBar()), _toolbar(new QToolBar(tr("Main"), this))
{
    setObjectName("mainWindow");
    setWindowTitle(QCoreApplication::applicationName());
    resize(1000, 650);
    _workspacePage = new WorkspacePage(this);
    _terminalTabs  = new TerminalTabs(this);
    _terminalTabs->setObjectName("terminalTabs");
    // The close button belongs to the same native tab-button layout as its caption.
    _terminalTabs->setTabsClosable(false);
    _terminalTabs->setMovable(true);
    _terminalTabs->setDocumentMode(true);
    _terminalTabs->tabBar()->setElideMode(Qt::ElideNone);
    _terminalTabs->tabBar()->setExpanding(false);
    _terminalTabs->tabBar()->installEventFilter(this);
    _terminalDockHost = new QMainWindow(this);
    _terminalDockHost->setWindowFlags(Qt::Widget);
    _terminalDockHost->setObjectName("terminalDockHost");
    _terminalDockHost->setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    _terminalDockHost->setContextMenuPolicy(Qt::CustomContextMenu);
    _central = new QStackedWidget(_terminalDockHost);
    _central->addWidget(_workspacePage);
    _central->addWidget(_terminalTabs);
    _terminalDockHost->setCentralWidget(_central);
    setCentralWidget(_terminalDockHost);
    connect(_workspacePage, &WorkspacePage::openRequested, this, &MainWindow::openWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::createRequested, this, &MainWindow::createWorkspaceRequested);
    connect(_workspacePage, &WorkspacePage::openSessionRequested, this, &MainWindow::openSessionRequested);
    connect(_workspacePage, &WorkspacePage::createSessionRequested, this, &MainWindow::createSessionRequested);
    _createMenus();
    _toolbar.toolbar()->setIconSize(QSize(24, 24));
    _toolbar.toolbar()->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(_toolbar.toolbar());
    statusBar()->showMessage(tr("Ready"));
}

MenubarActionContainer& MainWindow::mainMenu()
{
    return _mainMenu;
}

ToolbarActionContainer& MainWindow::toolbar()
{
    return _toolbar;
}

void MainWindow::setWorkspace(const QString& name, const QString& file)
{
    setWindowTitle(name + " — " + QCoreApplication::applicationName());
    _workspacePage->setWorkspace(name, file);
    showTerminals(_terminalTabs->count() > 0);
}

void MainWindow::showWorkspaceChoice(const QString& error)
{
    setWindowTitle(QCoreApplication::applicationName());
    _workspacePage->showChoice(error);
    showTerminals(false);
}

void MainWindow::_createMenus()
{
    _mainMenu.addGroup("main");
    const auto menu = [this](const QString& id, const QString& title, const QStringList& groups) {
        auto item = std::make_unique<MenuActionContainer>(id, title);
        for (const auto& group : groups)
        {
            item->addGroup(group);
        }
        _mainMenu.addMenu(item.get(), "main");
        item.release();
    };
    menu("file", tr("&File"), {"workspace", "settings", "recent", "application"});
    auto* file   = _mainMenu.findMenu("/file");
    auto  recent = std::make_unique<MenuActionContainer>("recentWorkspaces", tr("Recent &workspaces"));
    recent->addGroup("entries");
    file->addMenu(recent.get(), "recent");
    recent.release();
    menu("edit", tr("&Edit"), {"history", "clipboard", "selection"});
    menu("view", tr("&View"), {"panels", "terminal", "window"});
    menu("session", tr("&Session"), {"session", "recent", "settings"});
    auto recentSessions = std::make_unique<MenuActionContainer>("recentSessions", tr("Recent &sessions"));
    recentSessions->addGroup("entries");
    _mainMenu.findMenu("/session")->addMenu(recentSessions.get(), "recent");
    recentSessions.release();
    menu("terminal", tr("&Terminal"), {"create", "allConnections", "connection", "management", "output", "settings"});
    menu("tools", tr("&Tools"), {"management"});
    menu("help", tr("&Help"), {"about"});
    for (const auto& group : QStringList {"terminalCreation", "terminal"})
    {
        _toolbar.addGroup(group);
    }
}

void MainWindow::setSession(const QString& name, const QString& file, int objects, int links)
{
    _workspacePage->setSession(name, file, objects, links);
}

QTabWidget& MainWindow::terminalTabs()
{
    return *_terminalTabs;
}
QMainWindow& MainWindow::terminalDockHost()
{
    return *_terminalDockHost;
}

void MainWindow::setTerminalTab(int            index,
                                const QString& title,
                                const QString& detail,
                                const QIcon&   typeIcon,
                                const QIcon&   statusIcon)
{
    auto* bar = _terminalTabs->tabBar();
    if (index < 0 || index >= bar->count())
    {
        return;
    }

    auto*       header       = bar->tabButton(index, QTabBar::LeftSide);
    const bool  created      = !header;
    const QSize previousSize = header ? header->size() : QSize();
    if (!header)
    {
        header = new QWidget(bar);
        header->setObjectName("terminalTabHeader");
        auto* row = new QHBoxLayout(header);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        row->setSizeConstraint(QLayout::SetFixedSize);

        const auto iconButton = [header](const QString& name) {
            auto* button = new QToolButton(header);
            button->setObjectName(name);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setIconSize(QSize(16, 16));
            button->setFixedSize(16, 16);
            return button;
        };
        auto* status = iconButton("terminalStatusIndicator");
        status->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(status, 0, Qt::AlignTop);

        auto* caption = new QVBoxLayout;
        caption->setContentsMargins(0, 0, 0, 0);
        caption->setSpacing(0);
        auto* titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(4);
        titleRow->addStretch();
        auto* glyph = iconButton("terminalTypeIcon");
        glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(glyph);
        auto* titleLabel = new QLabel(header);
        titleLabel->setObjectName("terminalTabTitle");
        titleLabel->setTextFormat(Qt::PlainText);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(titleLabel);
        titleRow->addStretch();
        caption->addLayout(titleRow);
        auto* detailLabel = new QLabel(header);
        detailLabel->setObjectName("terminalTabDetail");
        detailLabel->setTextFormat(Qt::PlainText);
        detailLabel->setAlignment(Qt::AlignCenter);
        detailLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        caption->addWidget(detailLabel);
        row->addLayout(caption);

        auto* close = iconButton("terminalTabClose");
        close->setIcon(Icons::icon("x"));
        close->setToolTip(tr("Close terminal"));
        close->setAccessibleName(tr("Close terminal"));
        row->addWidget(close, 0, Qt::AlignTop);
        connect(close, &QToolButton::clicked, header, [this, header] {
            auto* currentBar = _terminalTabs->tabBar();
            for (int current = 0; current < currentBar->count(); ++current)
            {
                if (currentBar->tabButton(current, QTabBar::LeftSide) == header)
                {
                    emit _terminalTabs->tabCloseRequested(current);
                    return;
                }
            }
        });
    }

    header->findChild<QToolButton*>("terminalStatusIndicator")->setIcon(statusIcon);
    header->findChild<QToolButton*>("terminalTypeIcon")->setIcon(typeIcon);
    header->findChild<QLabel*>("terminalTabTitle")->setText(title);
    auto* detailLabel = header->findChild<QLabel*>("terminalTabDetail");
    detailLabel->setText(detail);
    detailLabel->setVisible(!detail.isEmpty());
    header->layout()->activate();
    header->adjustSize();

    // A single standard tab button owns all visible content. Qt moves and sizes
    // it as one unit during dragging, scrolling and animation.
    _terminalTabs->setTabText(index, {});
    if (created || header->size() != previousSize)
    {
        if (!created)
        {
            bar->setTabButton(index, QTabBar::LeftSide, nullptr);
        }
        bar->setTabButton(index, QTabBar::LeftSide, header);
    }
}

void MainWindow::showTerminals(bool visible)
{
    const bool attachedTabs = _terminalTabs->count() > 0;
    _central->setCurrentWidget(visible && attachedTabs ? static_cast<QWidget*>(_terminalTabs) : _workspacePage);
    bool dockVisible = false;
    for (auto* dock : _terminalDockHost->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (!dock->isHidden())
        {
            dockVisible = true;
            break;
        }
    }
    // Hidden central widgets consume no central layout space, letting native
    // dock panels fill the host when the tab strip has no attached terminals.
    _central->setVisible(attachedTabs || !dockVisible);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    auto* bar = _terminalTabs->tabBar();
    if (watched == bar && event->type() == QEvent::MouseButtonPress)
    {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            ++_dragGeneration;
            _dragTerminalId.clear();
            _dragPanel      = nullptr;
            _dragGrabOffset = QPoint();
            _dragStart      = mouse->globalPosition().toPoint();
            const int index = bar->tabAt(mouse->position().toPoint());
            if (auto* panel = _terminalTabs->widget(index))
            {
                _dragTerminalId = panel->property("terminalId").toString();
                _dragPanel      = panel;
                _dragGrabOffset = mouse->position().toPoint() - bar->tabRect(index).topLeft();
            }
        }
    }
    else if (watched == bar && event->type() == QEvent::MouseMove)
    {
        const auto* mouse             = static_cast<QMouseEvent*>(event);
        const int   platformThreshold = QApplication::startDragDistance();
        const int   exitThreshold     = std::max(platformThreshold * 2, bar->height() / 2);
        const bool  moved   = (mouse->globalPosition().toPoint() - _dragStart).manhattanLength() >= platformThreshold;
        const bool  outside = !bar->rect()
                                   .adjusted(-exitThreshold, -exitThreshold, exitThreshold, exitThreshold)
                                   .contains(mouse->position().toPoint());
        if ((mouse->buttons() & Qt::LeftButton) && _dragPanel && !_dragTerminalId.isEmpty() && moved && outside)
        {
            const QString           terminalId     = _dragTerminalId;
            const QPointer<QWidget> panel          = _dragPanel;
            const QPoint            globalPosition = mouse->globalPosition().toPoint();
            const QPoint            grabOffset     = _dragGrabOffset;
            const quint64           generation     = _dragGeneration;
            _dragTerminalId.clear();
            _dragPanel      = nullptr;
            _dragStart      = QPoint();
            _dragGrabOffset = QPoint();

            // Qt finishes this tab move event before the owner transfers the panel
            // and asks the platform to continue the still-held system move.
            QTimer::singleShot(0, this, [this, terminalId, panel, globalPosition, grabOffset, generation] {
                if (generation != _dragGeneration || !panel || _terminalTabs->indexOf(panel.data()) < 0
                    || panel->property("terminalId").toString() != terminalId)
                {
                    return;
                }
                if (QApplication::mouseButtons() & Qt::LeftButton)
                {
                    emit terminalTearOffRequested(terminalId, globalPosition, grabOffset);
                }
                else
                {
                    // A quick release before delivery uses the existing one-shot
                    // fallback, provided the pointer did not return to the strip.
                    auto*     currentBar = _terminalTabs->tabBar();
                    const int threshold  = std::max(QApplication::startDragDistance() * 2, currentBar->height() / 2);
                    if (!currentBar->rect()
                             .adjusted(-threshold, -threshold, threshold, threshold)
                             .contains(currentBar->mapFromGlobal(QCursor::pos())))
                    {
                        emit terminalDetachRequested(terminalId);
                    }
                }
            });
        }
    }
    else if (watched == bar && event->type() == QEvent::MouseButtonRelease)
    {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
        {
            const QString           terminalId = _dragTerminalId;
            const QPointer<QWidget> panel      = _dragPanel;
            const quint64           generation = _dragGeneration;
            _dragTerminalId.clear();
            _dragPanel           = nullptr;
            const int  threshold = std::max(QApplication::startDragDistance() * 2, bar->height() / 2);
            const bool moved     = (mouse->globalPosition().toPoint() - _dragStart).manhattanLength() >= threshold;
            const bool outside   = !bar->rect()
                                        .adjusted(-threshold, -threshold, threshold, threshold)
                                        .contains(mouse->position().toPoint());
            _dragStart           = QPoint();
            _dragGrabOffset      = QPoint();
            if (panel && !terminalId.isEmpty() && moved && outside)
            {
                // Let the native tab bar finish its release and reorder first.
                QTimer::singleShot(0, this, [this, terminalId, panel, generation] {
                    // A replacement session may reuse an ID, but never this live page.
                    if (generation == _dragGeneration && panel && _terminalTabs->indexOf(panel.data()) >= 0
                        && panel->property("terminalId").toString() == terminalId)
                    {
                        emit terminalDetachRequested(terminalId);
                    }
                });
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
