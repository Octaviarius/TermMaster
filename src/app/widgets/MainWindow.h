#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/MenubarActionContainer.h"
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

    PrettyId id() const;

protected:
    bool event(QEvent* event) override;

private:
    Ui::MainWindow* ui;
    PrettyId        _id;

    MenubarActionContainer* _mainMenuContainer;

signals:
    void focusChanged(bool activeState);
};
#endif // MAINWINDOW_H
