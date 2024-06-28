#include "MainWindow.h"

#include "core/CommandManager.h"
#include "core/SettingsManager.h"
#include "ui_MainWindow.h"

#include <QKeySequenceEdit>
#include <QLabel>
#include <QShortcut>

//===================================================================
MainWindow* MainWindow::_currentWindow;

MainWindow::MainWindow(PrettyId id, QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    _mainMenuContainer = new MenubarActionContainer(ui->menubar);

    setWindowTitle(QString("%1 - %2").arg(QApplication::applicationName()).arg(id.toString()));

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(SettingsManager::shortVersion);
    ui->statusbar->addWidget(lblVersion);
}

MainWindow::~MainWindow()
{
    delete ui;
}

MenubarActionContainer* MainWindow::mainMenu()
{
    return _mainMenuContainer;
}

MainWindow* MainWindow::currentWindow()
{
    return _currentWindow;
}

bool MainWindow::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::FocusIn:
            _currentWindow = this;
            break;

        case QEvent::FocusOut:
            if (_currentWindow == this)
            {
                _currentWindow = nullptr;
            }
            break;

        default:
            break;
    }

    return QMainWindow::event(event);
}
