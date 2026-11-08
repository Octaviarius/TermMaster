#include "MainWindow.h"

#include <QCoreApplication>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent), _mainMenu(menuBar()), _toolbar(new QToolBar(tr("Main"), this))
{
    setObjectName("mainWindow");
    setWindowTitle(QCoreApplication::applicationName());
    resize(1000, 650);
    setCentralWidget(new QWidget(this));
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
