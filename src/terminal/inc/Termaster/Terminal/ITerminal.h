#pragma once

#include <QObject>

class ITerminal : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Closed,
        Openning,
        Opened,
        Closing
    };

    ITerminal(QObject* parent);

    virtual bool open()  = 0;
    virtual bool close() = 0;

    State state() const;

protected:
    void _setState(State state);

private:
    State _state;

public slots:
    virtual bool dataWrite(QByteArray data) = 0;

signals:
    void stateChanged(State state);
    void dataRead(QByteArray data);
};
