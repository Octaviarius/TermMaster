#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "ConfigTerminalDialog.h"
#include "SettingsDialog.h"
#include "version.h"
#include <ConfigSerialDialog.h>
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

static const QString _versionLong =
    QString("%1_git%2+%3").arg(_versionShort).arg(APP_VERSION_GIT_REV).arg(APP_VERSION_GIT_BRANCH);

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //    config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(_versionShort);
    ui->statusbar->addWidget(lblVersion);

    // Menu conf
    connect(ui->actionHelp_Sources, SIGNAL(triggered()), this, SLOT(_onActionHelp_Sources()));
    connect(ui->actionHelp_HomePage, SIGNAL(triggered()), this, SLOT(_onActionHelp_Homepage()));
    connect(ui->actionControl_Configure, SIGNAL(triggered()), this, SLOT(_onActionHelp_Homepage()));

    connect(ui->actionFile_New_Serial, SIGNAL(triggered()), this, SLOT(_onActionFile_NewSerial()));

    connect(ui->actionSetup_Terminal, SIGNAL(triggered()), this, SLOT(_onActionSetup_Terminal()));
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
    auto dialog = new ConfigSerialDialog();
    PathedValue defConfig;

    dialog->show(defConfig);
}

void MainWindow::_onActionSetup_Terminal()
{
    auto dialog = new ConfigTerminalDialog();
    dialog->exec();
}
