#ifndef SERIALTERMINAL_H
#define SERIALTERMINAL_H

#include "Terminal.h"
#include <QSerialPort>

class SerialTerminal : public Terminal
{
    Q_OBJECT
public:
    SerialTerminal();

private:
    QSerialPort _serialPort;

protected:
    void onConfigChanged() override;

public slots:
    void inputData(QByteArray data) override;
    void start() override;
    void stop() override;
    void clearBuffer() override;
};

#endif // SERIALTERMINAL_H
