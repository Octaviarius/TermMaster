#pragma once
#include "LineflowEditorModel.h"

#include <QDialog>
#include <QPointer>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <map>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTreeView;
class QVBoxLayout;
class SettingsFormWidget;

/** Draft-only editor. The supplied application callback owns atomic persistence
 * and conflict handling. Registry and widget factories outlive the dialog.
 */
class LineflowEditorDialog final : public QDialog
{
    Q_OBJECT
public:
    struct ApplyResult
    {
        bool                       success;
        std::optional<QVariantMap> definitions;
        ApplyResult(bool ok) : success(ok)
        {
        }
        ApplyResult(bool ok, QVariantMap committed) : success(ok), definitions(std::move(committed))
        {
        }
    };
    using Apply = std::function<
        ApplyResult(const QVariantMap&, const QVariantMap&, const QList<LineflowEditorModel::Rename>&, QString*)>;
    LineflowEditorDialog(QVariantMap             definitions,
                         ProcessorRegistry&      registry,
                         SettingsWidgetRegistry& widgets,
                         Apply                   apply,
                         QWidget*                parent = nullptr);
    ~LineflowEditorDialog() override;
    LineflowEditorModel& model();
    bool                 applyDraft(QString* error = nullptr);
    void                 releaseEditors();
    void                 setVisible(bool visible) override;

private:
    struct Draft
    {
        std::unique_ptr<Settings>             defaults;
        std::unique_ptr<Settings>             settings;
        std::shared_ptr<const SettingsSchema> schema;
        QPointer<SettingsFormModel>           form;
        QByteArray                            fingerprint;
        QString                               block;
        QString                               node;
        ~Draft()
        {
            delete form.data();
        }
    };
    ProcessorRegistry&                        _registry;
    SettingsWidgetRegistry&                   _widgets;
    Apply                                     _apply;
    LineflowEditorModel*                      _model;
    QComboBox*                                _blocks;
    QTreeView*                                _tree;
    QListWidget*                              _catalog;
    QLineEdit*                                _search;
    QLineEdit*                                _name;
    QLabel*                                   _error;
    QWidget*                                  _properties;
    QVBoxLayout*                              _propertyLayout;
    QPointer<SettingsFormWidget>              _formWidget;
    std::map<QString, std::unique_ptr<Draft>> _drafts;
    QString                                   _active;
    bool                                      _updating  = false;
    bool                                      _resetting = false;

    bool _geometryInitialized = false;

    void _refreshBlocks();
    void _refreshCatalog();
    void _select();
    void _showProperties(const QModelIndex& index);
    void _clearProperties();
    bool _flush(Draft& draft, QString* error);
    void _message(const QString& message);
    void _addOperation();
    void _renameOperation();
    void _refreshActions();
    bool _refreshDraft(Draft& draft, const ProcessorCatalog::Type& metadata, QString* error);
};
