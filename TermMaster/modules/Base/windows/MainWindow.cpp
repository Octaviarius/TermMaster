#include "MainWindow.h"

#include "modules/Base/dialogs/SettingsDialog.h"
#include "ui_MainWindow.h"
#include "version.h"

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

MainWindow::MainWindow(SettingsProxy config, QWidget* parent) :
    QMainWindow(parent), ui(new Ui::MainWindow), _config(config)
{
    ui->setupUi(this);

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(_versionShort);
    ui->statusbar->addWidget(lblVersion);

    _config.sync();
}

MainWindow::~MainWindow()
{
    delete ui;
}
