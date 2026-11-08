#pragma once

#include <QDialog>
#include <QList>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <functional>
#include <memory>

class SettingsSchema;
class SettingsWidgetRegistry;
class SettingsFormModel;
class SettingsFormWidget;
class QLabel;
class QDialogButtonBox;
class QLineEdit;
class QTreeView;
class QStackedWidget;
class QStandardItemModel;
class QSortFilterProxyModel;

/** Owns settings page drafts; the application callback persists their borrowed owners. */
class SettingsDialog final : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(SettingsWidgetRegistry& registry, std::function<bool(QString*)> persist, QWidget* parent = nullptr);
    void               addPage(const QString&                        id,
                               const QString&                        category,
                               const QString&                        title,
                               const QString&                        description,
                               std::shared_ptr<const SettingsSchema> schema,
                               Settings::Node                        target,
                               bool                                  preview = false);
    void               addPage(const QString&                        id,
                               const QString&                        category,
                               const QString&                        title,
                               const QString&                        description,
                               std::shared_ptr<const SettingsSchema> schema,
                               SettingsOverlay                       target,
                               bool                                  preview = false);
    bool               selectPage(const QString& id);
    QString            currentPage() const;
    SettingsFormModel* pageModel(const QString& id) const;
    Settings&          previewSettings();
    Q_SIGNAL void      applied();

private:
    struct Page
    {
        QString             id;
        QString             category;
        QString             title;
        QString             description;
        SettingsFormModel*  model;
        SettingsFormWidget* form;
        QWidget*            widget;
        bool                preview;
    };
    enum Role
    {
        PageIdRole = Qt::UserRole + 1,
        SearchRole
    };

    SettingsWidgetRegistry&       _registry;
    std::function<bool(QString*)> _persist;
    Settings                      _previewSettings {QString {}};
    QList<Page>                   _pages;
    QLineEdit*                    _search;
    QTreeView*                    _tree;
    QStandardItemModel*           _treeModel;
    QSortFilterProxyModel*        _filter;
    QStackedWidget*               _stack;
    QWidget*                      _empty;
    QLabel*                       _error;
    QDialogButtonBox*             _buttons;
    bool                          _pendingPersistence = false;

    void _checkPage(const QString&                               id,
                    const QString&                               category,
                    const QString&                               title,
                    const std::shared_ptr<const SettingsSchema>& schema) const;
    void _addPage(const QString&     id,
                  const QString&     category,
                  const QString&     title,
                  const QString&     description,
                  SettingsFormModel* model,
                  bool               preview);
    void _filterPages();
    void _showSelection();
    bool _apply();
    void _updateButtons();
};
