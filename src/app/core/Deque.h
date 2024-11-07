#pragma once

#include <QQueue>

template <typename T>
class Deque : public QQueue<T>
{
public:
    using QQueue<T>::QQueue;

    Deque(int maxItems = -1) : QQueue<T>(), _maxItems(maxItems)
    {
    }

    inline void enqueue(const T& t)
    {
        QList<T>::append(t);
        if (_maxItems >= 0 && QQueue<T>::count() > _maxItems)
        {
            QList<T>::takeFirst();
        }
    }
    inline T dequeue()
    {
        return QList<T>::takeFirst();
    }

    int maxItems() const
    {
        return _maxItems;
    }

    void setMaxItems(int count)
    {
        _maxItems = count;

        while (_maxItems >= 0 && QQueue<T>::count() > _maxItems)
        {
            QList<T>::takeFirst();
        }
    }

    Deque& operator=(const QList<T>& other)
    {
        for (const auto& it : other)
        {
            enqueue(it);
        }
    }

private:
    int _maxItems = -1;
};
