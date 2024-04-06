#include "MainWindow.h"

#include "dialogs/SettingsDialog.h"
#include "ui_MainWindow.h"
#include "version.h"
#include "widgets/ConfigSerialWidget.h"
#include "widgets/ConfigTerminalEmulationWidget.h"
#include "widgets/ConfigTerminalWindowWidget.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QUrl>

static const QString _sourcesLink  = "https://github.com/Octaviarius/TermMaster";
static const QString _homepageLink = "https://github.com/Octaviarius/TermMaster";

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
    SettingsCategory("Connections", ":/res/icon/cat-connections"),
    SettingsCategory("Terminal", ":/res/icon/cat-terminal"),
};

static const QVector<SettingsEntry> _applicationEntries = {

    SettingsEntry("Terminal/Window", "terminal/window", ConfigBaseWidget::constructWidget<ConfigTerminalWindowWidget>),
    SettingsEntry("Connections/Serial", "connections/serial", ConfigBaseWidget::constructWidget<ConfigSerialWidget>),
    SettingsEntry("Connections/Console", "connections/console", ConfigBaseWidget::constructWidget<ConfigSerialWidget>),
    SettingsEntry("Connections/Socket", "connections/socket", ConfigBaseWidget::constructWidget<ConfigSerialWidget>),
    SettingsEntry("Terminal/Emulator",
                  "terminal/emulator",
                  ConfigBaseWidget::constructWidget<ConfigTerminalEmulationWidget>),
};

//===================================================================

MainWindow::MainWindow(Registree config, QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), _config(config)
{
    ui->setupUi(this);

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(_versionShort);
    ui->statusbar->addWidget(lblVersion);

    // Menu conf
    connect(ui->actionHelp_Sources, &QAction::triggered, this, &MainWindow::_onActionHelp_Sources);
    connect(ui->actionHelp_HomePage, &QAction::triggered, this, &MainWindow::_onActionHelp_Homepage);
    connect(ui->actionControl_Configure, &QAction::triggered, this, &MainWindow::_onActionHelp_Homepage);

    connect(ui->actionFile_New_Serial, &QAction::triggered, this, &MainWindow::_onActionFile_NewSerial);

    connect(ui->actionSetup_Terminal, &QAction::triggered, this, &MainWindow::_onActionSetup_Terminal);
    connect(ui->actionSetup_Application, &QAction::triggered, this, &MainWindow::_onActionSetup_Application);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::_onActionHelp_Sources()
{
    QDesktopServices::openUrl(QUrl(_sourcesLink));
}

void MainWindow::_onActionHelp_Homepage()
{
    QDesktopServices::openUrl(QUrl(_homepageLink));
}

void MainWindow::_onActionHelp_About()
{
}

void MainWindow::_onActionFile_NewSerial()
{
    auto widget = new ConfigSerialWidget(_config.node("default/serial"));
    widget->show();
}

void MainWindow::_onActionSetup_Terminal()
{
    auto widget = new ConfigSerialWidget(_config.node("default/terminal"));
    widget->show();
}

void MainWindow::_onActionSetup_Application()
{
    auto dialog = new SettingsDialog(_config);
    dialog->addCategories(_applicationCategories);
    dialog->addEntries(_applicationEntries);
    dialog->exec();

    delete dialog;
}
