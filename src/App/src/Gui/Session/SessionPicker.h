#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;

/** Human-readable names remain separate from stable session identity. */
class SessionPicker final : public QDialog
{
public:
    struct Entry
    {
        QString name;
        QString id;
    };
    explicit SessionPicker(const QList<Entry>& entries, QWidget* parent = nullptr);
    QString selectedSession() const;

private:
    QComboBox* _sessions;
};
