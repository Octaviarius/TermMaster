#pragma once

#include "utils/Singleton.h"

#include <QList>
#include <QMainWindow>
#include <QMap>
#include <QObject>

class WindowContainer;
class DocumentContainer;
class WindowManager;

class WindowContainer : public QObject
{
    Q_OBJECT
public:
    friend class WindowManager;

    QMainWindow* window();

private:
    QMainWindow* _window;

    WindowContainer(QMainWindow* window);

signals:
    void focused();
};

/******************************************************************************/

class DocumentContainer : public QObject
{
    Q_OBJECT
public:
    friend class WindowManager;
};

/******************************************************************************/

class WindowManager : public QObject, public Singleton<WindowManager>
{
    Q_OBJECT
public:
    friend class Singleton<WindowManager>;

    WindowContainer*   currentWindow();
    DocumentContainer* currentDocument();

    QList<DocumentContainer*> documents();
    QList<WindowContainer*>   windows();

    WindowContainer* newWindow();

private:
    WindowManager();

    QList<WindowContainer*> _windows;
    WindowContainer*        _currentWindow;

signals:
    void windowClosed(WindowContainer* window);
    void windowCreated(WindowContainer* window);
};
