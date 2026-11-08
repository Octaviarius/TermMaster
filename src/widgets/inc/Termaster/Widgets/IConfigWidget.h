#ifndef ICONFIGWIDGET_H
#define ICONFIGWIDGET_H

#include <QWidget>

class IConfigWidget : public QWidget
{
    Q_OBJECT
public:
    using List = QList<IConfigWidget*>;
    typedef IConfigWidget* (*Constructor)(QObject* parent);

    template <typename T>
    static Constructor getWidgetConstructor()
    {
        struct wapper
        {
            static IConfigWidget* constructWidget(QWidget* parent = nullptr)
            {
                return new T(parent);
            }
        };

        return reinterpret_cast<Constructor>(&wapper::constructWidget);
    }

    virtual void commit() = 0;
};

#endif // ICONFIGWIDGET_H
