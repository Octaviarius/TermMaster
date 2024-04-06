#include "MainWindow.h"

#include "dialogs/SettingsDialog.h"
#include "ui_MainWindow.h"
#include "version.h"

#include <QKeySequenceEdit>
#include <QLabel>
#include <QShortcut>

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

MainWindow::MainWindow(SettingsProxy config, QWidget* parent) :
    QMainWindow(parent), ui(new Ui::MainWindow), _config(config)
{
    ui->setupUi(this);

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(_versionShort);
    ui->statusbar->addWidget(lblVersion);

    // config menu
    QAction* action;

    action = new QAction("New Serial");
    ui->menuFile->addAction(action);

    action->setShortcut(QKeySequence("A"));

    auto ontrig = []() { qDebug() << "ACTION"; };
    connect(action, &QAction::triggered, ontrig);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::addMenu(QAction* menu)
{
    auto onTrig = []() { qDebug() << "Triggered!"; };
    QObject::connect(menu, &QAction::triggered, onTrig);

    ui->menuFile->addAction(menu);
}