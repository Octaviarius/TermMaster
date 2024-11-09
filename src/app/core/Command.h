#pragma once

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>

class Command : public QObject
{
    Q_OBJECT
public:
    using List = QList<Command*>;
    using Map  = QMap<QString, Command*>;

    Command(QString      name,
            QString      text            = "",
            QIcon        icon            = QIcon(),
            QKeySequence defaultShortcut = QKeySequence(),
            QKeySequence shortcut        = QKeySequence());

    ~Command();

    QString      name() const;
    QString      text() const;
    QString      tooltip() const;
    QIcon        icon() const;
    QKeySequence shortcut() const;
    QKeySequence defaultShortcut() const;

    Command& setName(QString name);
    Command& setText(QString text);
    Command& setTooltip(QString tooltip);
    Command& setIcon(QIcon icon);
    Command& setShortcut(QKeySequence shortcuts);
    Command& setDefaultShortcut(QKeySequence shortcuts);
    Command& setSingleInstance(bool isSingle);

    const QList<QAction*> actions() const;
    QAction*              newAction(QObject* parent = nullptr);

private:
    QString      _name;
    QString      _text;
    QString      _tooltip;
    QIcon        _icon;
    QKeySequence _shortcut;
    QKeySequence _defaultShortcut;

    QList<QAction*> _actions;
    bool            _isSingleInstance;

public slots:
    void resetShortcut();
    void changeShortcut(QKeySequence shortcut);

private slots:
    void _actionDestroyed(QObject* action);

signals:
    void shortcutChanged(Command* command);
};
