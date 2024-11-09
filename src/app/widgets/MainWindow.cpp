#include "MainWindow.h"

#include "core/RandomNames.h"
#include "core/Version.h"
#include "managers/SettingsManager.h"
#include "ui_MainWindow.h"

#include <QFocusEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QShortcut>
#include "widgets/TermWidget/TermWidget.h"

//===================================================================

MainWindow::MainWindow(uint id, QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), _id(id)
{
    ui->setupUi(this);

    _settings = SettingsManager::instance().session(id);

    _mainMenuContainer = new MenubarActionContainer(ui->menubar);
    _toolbarContainer  = new ToolbarActionContainer(ui->toolBar);

    auto name = _settings->value("name", RandomNames::instance().getId(_id).toString());

    setWindowTitle(QString("%1 (%2)").arg(QApplication::applicationName()).arg(name));

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(Version::shortVersion);
    ui->statusbar->addWidget(lblVersion);




}

MainWindow::~MainWindow()
{
    _settings->sync();
    delete ui;
    delete _settings;
}

uint MainWindow::id() const
{
    return _id;
}

Settings* MainWindow::settings()
{
    return _settings;
}

MenubarActionContainer* MainWindow::mainMenu()
{
    return _mainMenuContainer;
}

ToolbarActionContainer* MainWindow::toolbar()
{
    return _toolbarContainer;
}

bool MainWindow::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::WindowActivate:
            emit focusChanged(true);
            break;

        case QEvent::WindowDeactivate:
            emit focusChanged(false);
            break;

        default:
            break;
    }

    return QMainWindow::event(event);
}
