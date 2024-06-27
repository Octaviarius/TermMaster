#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/Builders.h"

#include <QMainWindow>
#include <QPointer>

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
} // namespace Ui
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    MenubarActionContainer* mainMenu();

private:
    Ui::MainWindow* ui;

    MenubarActionContainer* _mainMenuContainer;
};
#endif // MAINWINDOW_H
