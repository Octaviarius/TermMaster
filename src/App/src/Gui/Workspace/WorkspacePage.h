#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

/** Shows the active workspace or the first-run/recovery choice. */
class WorkspacePage final : public QWidget
{
    Q_OBJECT
public:
    explicit WorkspacePage(QWidget* parent = nullptr);
    void          setWorkspace(const QString& name, const QString& file);
    void          showChoice(const QString& error = {});
    void          setSession(const QString& name, const QString& file, int objects, int links);
    Q_SIGNAL void openSessionRequested();
    Q_SIGNAL void createSessionRequested();
    Q_SIGNAL void openRequested();
    Q_SIGNAL void createRequested();

private:
    QLabel*      _title;
    QLabel*      _message;
    QLineEdit*   _file;
    QLabel*      _sessionTitle;
    QLabel*      _sessionSummary;
    QLineEdit*   _sessionFile;
    QPushButton* _createSession;
    QPushButton* _openSession;
};
