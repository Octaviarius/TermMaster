#include "MainWindow.h"

#include "modules/Base/dialogs/SettingsDialog.h"
#include "ui_MainWindow.h"
#include "version.h"

#include <QActionGroup>
#include <QLabel>

static const QString _versionShort = QString("v%1.%2.%3.%4%5")
                                         .arg(APP_VERSION_MAJOR)
                                         .arg(APP_VERSION_MINOR)
                                         .arg(APP_VERSION_PATCH)
                                         .arg(APP_VERSION_BUILD)
                                         .arg(APP_VERSION_FLAVOR);

static const QString _versionLong = QString("%1_git%2+%3")
                                        .arg(_versionShort)
                                        .arg(APP_VERSION_GIT_REV)
                                        .arg(APP_VERSION_GIT_BRANCH);

//===================================================================
static QVector<SettingsCategory> _applicationCategories = {
    /* SettingsCategory("Connections", ":/res/icon/cat-connections"),
     SettingsCategory("Terminal", ":/res/icon/cat-terminal"),*/
};

static const QVector<SettingsEntry> _applicationEntries = {
    /*
        SettingsEntry("Terminal/Window", "terminal/window",
       ConfigBaseWidget::constructWidget<ConfigTerminalWindowWidget>), SettingsEntry("Connections/Serial",
       "connections/serial", ConfigBaseWidget::constructWidget<ConfigSerialWidget>),
        SettingsEntry("Connections/Console", "connections/console",
       ConfigBaseWidget::constructWidget<ConfigSerialWidget>), SettingsEntry("Connections/Socket", "connections/socket",
       ConfigBaseWidget::constructWidget<ConfigSerialWidget>), SettingsEntry("Terminal/Emulator", "terminal/emulator",
                      ConfigBaseWidget::constructWidget<ConfigTerminalEmulationWidget>),*/
};

//===================================================================

class MenuSocket
{
public:
    MenuSocket(QString socketName, QMenu* owner, QAction* refAction) :
        name(socketName), ownerMenu(owner), refAction(refAction)
    {
    }

    ~MenuSocket()
    {
    }

    QString              name;
    QMenu*               ownerMenu;
    QAction*             refAction;
    QList<QActionGroup*> groups;
};

//===================================================================

QStringList MainWindow::menuSockets()
{
    return _menuSockets.keys();
}

bool MainWindow::addMenuSocket(QString socketName, QMenu* owner, QAction* refAction)
{
    if (_menuSockets.contains(socketName))
    {
        return false;
    }
    else
    {
        refAction->setSeparator(true);
        refAction->setVisible(false);

        auto socket              = new MenuSocket(socketName, owner, refAction);
        _menuSockets[socketName] = socket;
        return true;
    }
}

bool MainWindow::addMenuActions(QString socketName, QActionGroup* group)
{
    auto socket = _menuSockets.value(socketName);

    if (!socket)
    {
        return false;
    }
    else
    {
        socket->ownerMenu->insertActions(socket->refAction, group->actions());
        socket->groups += group;
        socket->refAction->setVisible(true);
        return true;
    }
}

MainWindow::MainWindow(SettingsProxy config, QWidget* parent) :
    QMainWindow(parent), ui(new Ui::MainWindow), _config(config)
{
    ui->setupUi(this);

    auto m = new QMenu("Zero");

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(_versionShort);
    ui->statusbar->addWidget(lblVersion);
}
/*
QMenu* MainWindow::addMenu(QString path)
{
    QMenu* menu = nullptr;

    for (const auto& p : path.split('/'))
    {
        if (!menu)
        {
            auto m = ui->menubar->findChild<QMenu*>(p, Qt::FindDirectChildrenOnly);

            if (!m)
            {
                menu = new QMenu(p);
                ui->menubar->addMenu(menu);
            }
        }
        else
        {
            auto m = menu->findChild<QMenu*>(p, Qt::FindDirectChildrenOnly);
            if (!m)
            {
                auto newMenu = new QMenu(p);
                menu->addMenu(newMenu);
                menu = newMenu;
            }
        }
    }

    return menu;
}

void MainWindow::addMenuAction(QString path, QActionGroup* group)
{
    auto menu = addMenu(path);

    menu->addActions(group->actions());
}
*/
MainWindow::~MainWindow()
{
    delete ui;
}

QMenuBar& MainWindow::menuBar()
{
    return *ui->menubar;
}

QToolBar& MainWindow::toolBar()
{
    return *ui->toolBar;
}
