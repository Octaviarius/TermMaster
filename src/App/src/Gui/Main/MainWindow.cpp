#include "MainWindow.h"

#include "../Workspace/WorkspacePage.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <Termaster/Core/Gui/Action/MenuActionContainer.h>
#include <memory>

namespace
{
/** Vector-backed status presentation; pointer input remains with the native tab. */
class TerminalStatusIndicator final : public QWidget
{
public:
    explicit TerminalStatusIndicator(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(16, 16);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setObjectName("terminalStatusIndicator");
    }

    void setIcon(const QIcon& icon)
    {
        _icon = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        _icon.paint(&painter, rect(), Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }

private:
    QIcon _icon;
};

/** Preserve native tab painting and interactions while allowing two text lines. */
class TerminalTabBar final : public QTabBar
{
public:
    TerminalTabBar()
    {
        qApp->installEventFilter(this);
        connect(this, &QTabBar::currentChanged, this, [this] { _positionTabButtons(); });
        _refreshStyle();
    }

protected:
    QSize tabSizeHint(int index) const override
    {
        auto size       = QTabBar::tabSizeHint(index);
        size.rheight() += tabText(index).count('\n') * fontMetrics().lineSpacing();
        if (!qvariant_cast<QIcon>(tabData(index)).isNull())
        {
            size.rwidth() += 40;
        }
        return size;
    }

    void paintEvent(QPaintEvent* event) override
    {
        QTabBar::paintEvent(event);
        QPainter painter(this);
        QRegion  clip(rect());
        for (const auto* button : findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (button->isVisible())
            {
                clip -= button->geometry();
            }
        }
        painter.setClipRegion(clip);
        for (int index = 0; index < count(); ++index)
        {
            const auto icon = qvariant_cast<QIcon>(tabData(index));
            if (icon.isNull())
            {
                continue;
            }
            QStyleOptionTab option;
            initStyleOption(&option, index);
            const auto  text       = style()->subElementRect(QStyle::SE_TabBarTabText, &option, this);
            const auto  titleWidth = fontMetrics().size(Qt::TextShowMnemonic, option.text.section('\n', 0, 0)).width();
            const auto  center     = text.center().y() - option.text.count('\n') * fontMetrics().lineSpacing() / 2;
            const QRect glyph(text.center().x() - titleWidth / 2 - 20, center - 8, 16, 16);
            icon.paint(&painter, glyph, Qt::AlignCenter, isTabEnabled(index) ? QIcon::Normal : QIcon::Disabled);
        }
    }

    void tabLayoutChange() override
    {
        QTabBar::tabLayoutChange();
        _positionTabButtons();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QTabBar::resizeEvent(event);
        _positionTabButtons();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        {
            _refreshStyle();
        }
        return QTabBar::eventFilter(watched, event);
    }

private:
    void _positionTabButtons()
    {
        for (int index = 0; index < count(); ++index)
        {
            QStyleOptionTab option;
            initStyleOption(&option, index);
            const auto text   = style()->subElementRect(QStyle::SE_TabBarTabText, &option, this);
            const auto lines  = tabText(index).count('\n');
            const auto center = text.center().y() - lines * fontMetrics().lineSpacing() / 2;
            for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide})
            {
                if (auto* button = tabButton(index, side))
                {
                    button->move(button->x(), center - button->height() / 2);
                }
            }
        }
    }

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
    _terminalTabs->setTabsClosable(true);
    _terminalTabs->setMovable(true);
    _terminalTabs->setDocumentMode(true);
    _terminalTabs->tabBar()->setElideMode(Qt::ElideNone);
    _terminalTabs->tabBar()->setExpanding(false);
    _central = new QStackedWidget(this);
    _central->addWidget(_workspacePage);
    _central->addWidget(_terminalTabs);
    setCentralWidget(_central);
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
    menu("view", tr("&View"), {"panels", "window"});
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
void MainWindow::setTerminalStatusIcon(int index, const QIcon& icon)
{
    auto* bar = _terminalTabs->tabBar();
    if (index < 0 || index >= bar->count())
    {
        return;
    }
    auto* indicator = dynamic_cast<TerminalStatusIndicator*>(bar->tabButton(index, QTabBar::LeftSide));
    if (!indicator)
    {
        if (auto* close = bar->tabButton(index, QTabBar::LeftSide))
        {
            bar->setTabButton(index, QTabBar::LeftSide, nullptr);
            bar->setTabButton(index, QTabBar::RightSide, close);
        }
        indicator = new TerminalStatusIndicator(bar);
        bar->setTabButton(index, QTabBar::LeftSide, indicator);
    }
    indicator->setIcon(icon);
}

void MainWindow::showTerminals(bool visible)
{
    _central->setCurrentWidget(visible && _terminalTabs->count() ? static_cast<QWidget*>(_terminalTabs) :
                                                                   _workspacePage);
}
