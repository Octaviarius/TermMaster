#pragma once

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QString>

/** GUI-thread command. Actions are borrowed until their parent or command destroys them.
 * Callbacks must use deleteLater() for their emitting command/action. Shared mode
 * ignores action parents and owns its action. Empty initial shortcut uses default;
 * call setShortcut({}) after construction to explicitly disable it.
 */
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
            QKeySequence shortcut        = QKeySequence(),
            QObject*     owner           = nullptr);

    ~Command() override;

    QString      name() const;
    QString      text() const;
    QString      tooltip() const;
    QIcon        icon() const;
    QKeySequence shortcut() const;
    QKeySequence defaultShortcut() const;

    Command& setText(QString text);
    Command& setTooltip(QString tooltip);
    Command& setIcon(QIcon icon);
    Command& setShortcut(QKeySequence shortcuts);
    Command& setDefaultShortcut(QKeySequence shortcuts);
    Command& setSingleInstance(bool isSingle);

    QList<QAction*> actions() const;
    QAction*        newAction(QObject* parent = nullptr);

    Q_SLOT void   resetShortcut();
    Q_SLOT void   changeShortcut(QKeySequence shortcut);
    Q_SIGNAL void shortcutChanged(Command* command);

private:
    const QString            _name;
    QString                  _text;
    QString                  _tooltip;
    QIcon                    _icon;
    QKeySequence             _shortcut;
    QKeySequence             _defaultShortcut;
    QList<QPointer<QAction>> _actions;
    bool                     _isSingleInstance = false;
    bool                     _destroying       = false;

    void _actionDestroyed();
};
