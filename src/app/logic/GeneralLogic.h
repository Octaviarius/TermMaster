#pragma once

#include "core/Singleton.h"
#include "managers/WindowManager.h"

#include <QObject>

class GeneralLogic : public QObject, public Singleton<GeneralLogic>
{
    Q_OBJECT
public:
    friend class Singleton<GeneralLogic>;

private:
    GeneralLogic();

private slots:
    void onWindowCreated(MainWindow* window);
    void onWindowTerminated(MainWindow* window);
};
