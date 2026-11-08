#pragma once

#include <QDialog>
#include <QPointer>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class PluginManager;
class PluginTableModel;

class PluginManagerWindow final : public QDialog
{
    Q_OBJECT

private:
    QPointer<PluginManager> _manager;
    PluginTableModel*       _model;
    QSortFilterProxyModel*  _filter;
    QTableView*             _table;
    QLineEdit*              _search;
    QPushButton*            _enable;
    QPushButton*            _disable;
    QPushButton*            _reload;
    QPushButton*            _refresh;
    QPushButton*            _openFolder;
    QLabel*                 _summary;
    QLabel*                 _empty;
    QLabel*                 _operationMessage;
    QLineEdit*              _id;
    QLineEdit*              _path;
    QPlainTextEdit*         _dependencies;
    QPlainTextEdit*         _reverseDependencies;
    QPlainTextEdit*         _diagnostic;
    QString                 _selectedPath;
    bool                    _busy      = false;
    bool                    _resetting = false;

    void    _rememberSelection();
    void    _restoreSelection();
    void    _updateDetails();
    void    _updateActions();
    QString _selectedId() const;

public:
    explicit PluginManagerWindow(PluginManager& manager, QWidget* parent = nullptr);
    Q_DISABLE_COPY_MOVE(PluginManagerWindow)

    void setBusy(bool busy);
    void showError(const QString& error);
    void showResult(const QString& message);

signals:
    void enabledChangeRequested(const QString& id, bool enabled);
    void reloadRequested(const QString& id);
    void refreshRequested();
    void openFolderRequested();
};
