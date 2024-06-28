#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/Builders.h"
#include "core/RandomNames.h"

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
    friend class WindowManager;

    MainWindow(PrettyId id, QWidget* parent = nullptr);
    ~MainWindow();

    MenubarActionContainer* mainMenu();

    bool event(QEvent* event) override;

    static MainWindow* currentWindow();

private:
    Ui::MainWindow* ui;

    MenubarActionContainer* _mainMenuContainer;

    static MainWindow* _currentWindow;
};
#endif // MAINWINDOW_H
