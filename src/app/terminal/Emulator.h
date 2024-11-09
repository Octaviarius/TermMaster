#ifndef EMULATOR_H
#define EMULATOR_H

#include <QObject>
#include <widgets/TermWidget/TermWidget.h>

class Emulator : public QObject
{
    Q_OBJECT
public:
    explicit Emulator(QObject* parent = nullptr);

    void setTermWidget(TermWidget* widget);

private:
    TermWidget* _widget;

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
