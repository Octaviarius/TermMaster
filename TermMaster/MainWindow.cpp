#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QDesktopServices>
#include <QUrl>

static const QString _sourcesLink = "https://github.com/Octaviarius/TermMaster";

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(ui->actionHelp_Sources, SIGNAL(triggered()), this, SLOT(_onActionHelp_Sources()));
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
}

void MainWindow::_onActionHelp_About()
{
}
