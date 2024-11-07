#pragma once

#include "core/Singleton.h"
#include "managers/WindowManager.h"

#include <QList>
#include <QObject>

class GeneralLogic : public QObject, public Singleton<GeneralLogic>
{
    Q_OBJECT
public:
    friend class Singleton<GeneralLogic>;

private:
    GeneralLogic();
    QList<QAction*> _recentSessions;

    void _updateRecentSessions();

private slots:
    void onWindowCreated(MainWindow* window);
    void onWindowTerminated(MainWindow* window);
};
