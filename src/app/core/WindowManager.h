#pragma once

#include "RandomNames.h"
#include "utils/Singleton.h"
#include "utils/utils.h"
#include "widgets/MainWindow.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>

class WindowManager : public QObject, public Singleton<WindowManager>
{
    Q_OBJECT
public:
    friend class Singleton<WindowManager>;

    MainWindow*        currentWindow();
    QList<MainWindow*> windows();

    MainWindow* newWindow(PrettyId id, bool show = true);

private:
    WindowManager();

    MainWindow*        _currentWindow;
    QList<MainWindow*> _windows;

signals:
    void windowCreated(MainWindow* window);
    void windowTerminated(MainWindow* window);
};
