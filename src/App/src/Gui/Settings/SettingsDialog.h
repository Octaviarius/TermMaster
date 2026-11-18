#pragma once

#include <QDialog>
#include <QList>
#include <Termaster/Core/Help/HelpSource.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <functional>
#include <memory>

class SettingsSchema;
class SettingsWidgetRegistry;
class SettingsFormModel;
class SettingsFormWidget;
class QComboBox;
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
    ~SettingsDialog() override;
    void addPage(const QString&                        id,
                 const QString&                        category,
                 const QString&                        title,
                 const QString&                        description,
                 std::shared_ptr<const SettingsSchema> schema,
                 Settings::Node                        target);
    void addPage(const QString&                        id,
                 const QString&                        category,
                 const QString&                        title,
                 const QString&                        description,
                 std::shared_ptr<const SettingsSchema> schema,
                 SettingsOverlay                       target);
    /** Alternative forms share one navigation entry; inactive drafts are retained and never applied. */
    void               addPageVariant(const QString&                        id,
                                      const QString&                        choice,
                                      const QString&                        label,
                                      std::shared_ptr<const SettingsSchema> schema,
                                      SettingsOverlay                       target);
    QString            pageChoice(const QString& id) const;
    void               setPageChoice(const QString& id, const QString& choice);
    void               setVisible(bool visible) override;
    bool               hasHeightForWidth() const override;
    bool               selectPage(const QString& id);
    QString            currentPage() const;
    HelpSource::Topic  helpContext() const;
    SettingsFormModel* pageModel(const QString& id) const;
    void               refreshInheritedValues();
    Q_SIGNAL void      closing();
    Q_SIGNAL void      applied();
    Q_SIGNAL void      helpRequested();

private:
    struct Variant
    {
        QString             choice;
        SettingsFormModel*  model;
        SettingsFormWidget* form;
    };
    struct Page
    {
        QString             id;
        QString             category;
        QString             title;
        QString             description;
        SettingsFormModel*  model;
        SettingsFormWidget* form;
        QWidget*            widget;
        QComboBox*          choices = nullptr;
        QString             originalChoice;
        QList<Variant>      variants;
    };
    enum Role
    {
        PageIdRole = Qt::UserRole + 1,
        SearchRole
    };

    SettingsWidgetRegistry&       _registry;
    std::function<bool(QString*)> _persist;
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
    bool                          _initialGeometrySet = false;

    void _checkPage(const QString&                               id,
                    const QString&                               category,
                    const QString&                               title,
                    const std::shared_ptr<const SettingsSchema>& schema) const;
    void _addPage(const QString&     id,
                  const QString&     category,
                  const QString&     title,
                  const QString&     description,
                  SettingsFormModel* model);
    void _fitToScreen();
    void _filterPages();
    void _showSelection();
    bool _apply();
    void _updateButtons();
};
