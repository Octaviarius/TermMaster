#ifndef EMULATOR_H
#define EMULATOR_H

#include <QObject>
#include <widgets/TermWidget/TermWidget.h>

#define EMU_ESC(x)               "\x1b" x
#define EMU_CSI(x)               EMU_ESC("[") x
#define EMU_CSI_SPECKEY(key)     EMU_CSI(key) "~"
#define EMU_CSI_REND(x)          EMU_CSI(x) "m"
#define EMU_CSI_REND_RESET_ATTRS EMU_CSI_REND(EMU_ATTR_RESET)

#define EMU_FCLR_DEFAULT         "39"
#define EMU_FCLR_BLACK           "30"
#define EMU_FCLR_RED             "31"
#define EMU_FCLR_GREEN           "32"
#define EMU_FCLR_YELLOW          "33"
#define EMU_FCLR_BLUE            "34"
#define EMU_FCLR_MAGENTA         "35"
#define EMU_FCLR_CYAN            "36"
#define EMU_FCLR_WHITE           "37"
#define EMU_FCLR_GRAY            "90"
#define EMU_FCLR_BR_RED          "91"
#define EMU_FCLR_BR_GREEN        "92"
#define EMU_FCLR_BR_YELLOW       "93"
#define EMU_FCLR_BR_BLUE         "94"
#define EMU_FCLR_BR_MAGENTA      "95"
#define EMU_FCLR_BR_CYAN         "96"
#define EMU_FCLR_BR_WHITE        "97"

#define EMU_BCLR_DEFAULT         "49"
#define EMU_BCLR_BLACK           "40"
#define EMU_BCLR_RED             "41"
#define EMU_BCLR_GREEN           "42"
#define EMU_BCLR_YELLOW          "43"
#define EMU_BCLR_BLUE            "44"
#define EMU_BCLR_MAGENTA         "45"
#define EMU_BCLR_CYAN            "46"
#define EMU_BCLR_WHITE           "47"
#define EMU_BCLR_GRAY            "100"
#define EMU_BCLR_BR_RED          "101"
#define EMU_BCLR_BR_GREEN        "102"
#define EMU_BCLR_BR_YELLOW       "103"
#define EMU_BCLR_BR_BLUE         "104"
#define EMU_BCLR_BR_MAGENTA      "105"
#define EMU_BCLR_BR_CYAN         "106"
#define EMU_BCLR_BR_WHITE        "107"

#define EMU_ATTR_RESET           "0"
#define EMU_ATTR_SET_BOLD        "1"
#define EMU_ATTR_SET_FAINT       "2"
#define EMU_ATTR_SET_ITALIC      "3"
#define EMU_ATTR_SET_UNDERLINE   "4"
#define EMU_ATTR_SET_BLINK       "5"
#define EMU_ATTR_SET_FBLINK      "6"
#define EMU_ATTR_SET_INVERSE     "7"
#define EMU_ATTR_SET_HIDE        "8"
#define EMU_ATTR_SET_STRIKE      "9"
#define EMU_ATTR_CLR_BOLD        "21"
#define EMU_ATTR_CLR_FAINT       "22"
#define EMU_ATTR_CLR_ITALIC      "23"
#define EMU_ATTR_CLR_UNDERLINE   "24"
#define EMU_ATTR_CLR_BLINK       "25"
#define EMU_ATTR_CLR_INVERSE     "27"
#define EMU_ATTR_CLR_HIDE        "28"
#define EMU_ATTR_CLR_STRIKE      "29"

class Emulator : public QObject
{
    Q_OBJECT
public:
    explicit Emulator(QObject* parent = nullptr);

    void setTermWidget(TermWidget* widget);

    void setEchoMode(bool enable);
    bool echoMode() const;

    void    setOutputNewLine(QString syms);
    QString outputNewLine() const;

private:
    TermWidget* _widget;

    QString _outputNewLine;

    QString    _escSequence;
    QByteArray _incompleteBytes;

    bool _echoMode = false;

    void _putCharacter(QChar ch);

    void _escFinished();
    void _handleEsc(QString esc);
    void _handleEscCsi(QString esc);
    void _handleEscCsiRendition(QString esc);
    void _handleEscCsiSpecKeys(QString esc);

public slots:
    void inputData(QByteArray data);
    void inputKey(QKeyEvent* event);

signals:
    void outputData(QByteArray data);
};

#endif // EMULATOR_H
