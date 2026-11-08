#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;

/** Shows the active workspace or the first-run/recovery choice. */
class WorkspacePage final : public QWidget
{
    Q_OBJECT
public:
    explicit WorkspacePage(QWidget* parent = nullptr);
    void          setWorkspace(const QString& name, const QString& file);
    void          showChoice(const QString& error = {});
    Q_SIGNAL void openRequested();
    Q_SIGNAL void createRequested();

private:
    QLabel*    _title;
    QLabel*    _message;
    QLineEdit* _file;
};
