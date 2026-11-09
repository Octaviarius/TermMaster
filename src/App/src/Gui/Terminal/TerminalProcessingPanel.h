#pragma once

#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

class ProcessorRegistry;
class SettingsWidgetRegistry;
class ProcessingRouter;
class QComboBox;
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
                            QWidget*                parent = nullptr);
    ~TerminalProcessingPanel() override;
    QWidget* propertiesWidget() const;
    void     setConfiguration(const QVariantMap& configuration);
    void     releaseEditors();
    bool     applyDraft(QString* error = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct NodeDraft;
    QHash<QObject*, QPointer<QToolButton>>  _resetControls;
    ProcessorRegistry&                      _registry;
    SettingsWidgetRegistry&                 _editors;
    ProcessingRouter&                       _router;
    QVariantMap                             _baseline;
    QVariantMap                             _latest;
    QVariantMap                             _draft;
    Apply                                   _apply;
    QComboBox*                              _selectors[2];
    QComboBox*                              _direction;
    QPointer<QWidget>                       _properties;
    QTableWidget*                           _table;
    QLabel*                                 _error;
    QLabel*                                 _status;
    QPushButton*                            _applyButton;
    std::vector<std::unique_ptr<NodeDraft>> _nodes;
    bool                                    _updating       = false;
    int                                     _shownDirection = 0;
    void                                    _catalog();
    void                                    _clearRows();
    void                                    _showProperties();
    bool                                    _flush(QString* error);
    void                                    _diagnostics();
};
