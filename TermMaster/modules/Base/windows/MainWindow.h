#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/Registree.h"
#include "core/SettingsManager.h"

#include <QList>
#include <QMainWindow>
#include <QMap>

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
} // namespace Ui
QT_END_NAMESPACE

class MenuSocket;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(SettingsProxy config, QWidget* parent = nullptr);
    ~MainWindow();

    QMenuBar& menuBar();
    QToolBar& toolBar();

    QStringList menuSockets();
    bool        addMenuSocket(QString socketName, QMenu* owner, QAction* refAction);
    bool        addMenuActions(QString socketName, QActionGroup* group);

private:
    Ui::MainWindow*            ui;
    SettingsProxy              _config;
    QMap<QString, MenuSocket*> _menuSockets;
};
#endif // MAINWINDOW_H
