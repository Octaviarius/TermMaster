#include "MainWindow.h"

#include "core/Version.h"
#include "ui_MainWindow.h"

#include <QFocusEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QShortcut>

//===================================================================

MainWindow::MainWindow(PrettyId id, QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow), _id(id)
{
    ui->setupUi(this);

    _mainMenuContainer = new MenubarActionContainer(ui->menubar);

    setWindowTitle(QString("%1 (%2)").arg(QApplication::applicationName()).arg(id.toString()));

    // config statusbar
    auto lblVersion = new QLabel();
    lblVersion->setFrameStyle(QFrame::Sunken);
    lblVersion->setText(Version::shortVersion);
    ui->statusbar->addWidget(lblVersion);
}

MainWindow::~MainWindow()
{
    delete ui;
}

PrettyId MainWindow::id() const
{
    return _id;
}

MenubarActionContainer* MainWindow::mainMenu()
{
    return _mainMenuContainer;
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
