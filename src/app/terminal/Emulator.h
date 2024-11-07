#ifndef EMULATOR_H
#define EMULATOR_H

#include <QObject>
#include <widgets/TerminalWidget.h>

class Emulator : public QObject
{
    Q_OBJECT
public:
    explicit Emulator(QObject* parent = nullptr);

    void setTerminalWidget(TerminalWidget* widget);

private:
    TerminalWidget* _widget;

    QString    _escSequence;
    bool       _isEscSequence;
    QByteArray _incompleteBytes;

    void _putCharacter(QChar ch);

    void _handleEsc(QString esc);
    void _handleEscCsi(QString esc);
    void _handleEscCsiRendition(QString esc);
    void _handleEscCsiSpecKeys(QString esc);

public slots:
    void inputData(QByteArray data);
    void inputKey(Qt::Key key);

signals:
    void outputData(QByteArray data);
};

#endif // EMULATOR_H
