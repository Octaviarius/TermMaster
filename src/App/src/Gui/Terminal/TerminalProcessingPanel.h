#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QVariantMap>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

class ProcessorRegistry;
class SettingsWidgetRegistry;
class ProcessingRouter;
class LineflowEditorDialog;
class CodeflowEditorDialog;
class QComboBox;
class ProcessorSelector;
class ProcessorFavorites;
class QTableWidget;
class QLabel;
class QPushButton;
class QToolButton;

/** Host-owned terminal draft. Factories are borrowed only while editors are visible. */
class TerminalProcessingPanel final : public QWidget
{
    Q_OBJECT
public:
    using Apply = std::function<bool(const QVariantMap&, const QVariantMap&, QString*)>;
    TerminalProcessingPanel(ProcessorRegistry&      registry,
                            SettingsWidgetRegistry& editors,
                            ProcessingRouter&       router,
                            QVariantMap             configuration,
                            Apply                   apply,
                            QWidget*                parent    = nullptr,
                            ProcessorFavorites*     favorites = nullptr);
    ~TerminalProcessingPanel() override;
    Q_SIGNAL void compositionEditorRequested(const QString& id, const QString& kind);
    QWidget*      propertiesWidget() const;
    void          setConfiguration(const QVariantMap& configuration);
    void          releaseEditors();
    void          setWorkspaceDirectory(QString directory);
    bool          applyDraft(QString* error = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct NodeDraft;
    QString                                 _workspaceDirectory;
    QList<QPointer<CodeflowEditorDialog>>   _codeEditors;
    QHash<QObject*, QPointer<QToolButton>>  _resetControls;
    ProcessorRegistry&                      _registry;
    SettingsWidgetRegistry&                 _editors;
    ProcessingRouter&                       _router;
    QVariantMap                             _baseline;
    QVariantMap                             _latest;
    QVariantMap                             _draft;
    Apply                                   _apply;
    ProcessorSelector*                      _selector;
    QComboBox*                              _modeSelector;
    QToolButton*                            _inplaceButton;
    QPointer<LineflowEditorDialog>          _inplaceEditor;
    QPointer<QWidget>                       _properties;
    QTableWidget*                           _table;
    QLabel*                                 _error;
    QLabel*                                 _status;
    QPushButton*                            _applyButton;
    std::vector<std::unique_ptr<NodeDraft>> _nodes;
    bool                                    _propertiesPending = true;
    bool                                    _updating          = false;
    void                                    _catalog();
    void                                    _mode(const QString& mode);
    void                                    _editInplace();
    void                                    _clearRows();
    void                                    _showProperties();
    bool                                    _flush(QString* error);
    void                                    _diagnostics();
    void                                    _editInline(const QString&     path,
                                                        const QString&     type,
                                                        const QString&     title,
                                                        int                channels,
                                                        const QVariantMap& configuration);
};
