#ifndef TERMINAL_H
#define TERMINAL_H

#include "core/Registree.h"
#include <QObject>

class Terminal : public QObject
{
    Q_OBJECT
public:
    explicit Terminal(QObject* parent = nullptr);

protected:
    Registree _config;
    QByteArray _buffer;

    int _charsRate;
    int _lineRate;

    virtual void onConfigChanged() = 0;

public slots:
    virtual void inputData(QByteArray data) = 0;
    virtual void start()                    = 0;
    virtual void stop()                     = 0;
    virtual void clearBuffer()              = 0;

    void setConfig(const Registree& config);
    Registree config();

signals:
    void outputData(QByteArray data);

    void started();
    void stopped();
    void connectionChanged(int state);
};

#endif // TERMINAL_H
