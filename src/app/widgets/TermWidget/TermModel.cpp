#include "TermModel.h"

#include <core/utils.h>

TermModel::TermModel(QObject* parent) :
    QObject(parent), _cursor(this), _scrollPosition(0, 0), _selectionStart(0, 0), _selectionStop(0, 0)
{
}

void TermModel::update()
{
    emit updated();
}

void TermModel::setSize(QSize size)
{
    if (_termSize != size)
    {
        _termSize = size;

        if (_lines.count() < _termSize.height())
        {
            _lines.resize(_termSize.height());
        }

        update();
    }
}

int TermModel::tabulateColumn(int col) const
{
    return divCeil(col + 1, _tabSize) * _tabSize;
}

int TermModel::tabulateSpaces(int col) const
{
    return tabulateColumn(col) - col;
}

QList<TermLine> TermModel::lines(int start, int count)
{
    return _lines.sliced(start, count);
}

const QList<TermLine>& TermModel::lines()
{
    return _lines;
}

void TermModel::inputData(QString data)
{
    _cursor.addString(data);
}

void TermModel::setStartSelection(QPoint start)
{
    if (_selectionStart != start)
    {
        _selectionStart = start;

        qDebug() << "Selected " << _selectionStart << ":" << _selectionStop;
    }
}

void TermModel::setStopSelection(QPoint stop)
{
    if (_selectionStop != stop)
    {
        _selectionStop = stop;

        qDebug() << "Selected " << _selectionStart << ":" << _selectionStop;
    }
}

void TermModel::resetSelection()
{
    _selectionStart = QPoint();
    _selectionStop  = QPoint();

    emit updated();
}

QRect TermModel::selection() const
{
    return QRect(_selectionStart, _selectionStop);
}
