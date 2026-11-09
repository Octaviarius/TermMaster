#include "TerminalProcessingPanel.h"

#include "../../Terminal/ProcessingRouter.h"
#include "../Processing/ProcessorSelector.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsFormModel.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsSchema.h>
#include <exception>

namespace
{
QString directionKey(int index)
{
    return index == 0 ? QStringLiteral("rx") : QStringLiteral("tx");
}
QString stateName(ProcessingRouter::State state)
{
    switch (state)
    {
        case ProcessingRouter::State::Bypass:
            return QObject::tr("Bypass");
        case ProcessingRouter::State::Idle:
            return QObject::tr("Ready");
        case ProcessingRouter::State::Busy:
            return QObject::tr("Busy");
        case ProcessingRouter::State::Cancelling:
            return QObject::tr("Cancelling");
        case ProcessingRouter::State::Failed:
            return QObject::tr("Failed");
    }
    return {};
}
} // namespace

struct TerminalProcessingPanel::NodeDraft
{
    QString                            path;
    std::unique_ptr<Settings>          inherited;
    std::unique_ptr<Settings>          local;
    std::unique_ptr<SettingsFormModel> model;
};

TerminalProcessingPanel::TerminalProcessingPanel(ProcessorRegistry&      registry,
                                                 SettingsWidgetRegistry& editors,
                                                 ProcessingRouter&       router,
                                                 QVariantMap             configuration,
                                                 Apply                   apply,
                                                 QWidget*                parent,
                                                 ProcessorFavorites*     favorites) :
    QWidget(parent), _registry(registry), _editors(editors), _router(router), _baseline(configuration),
    _latest(configuration), _draft(std::move(configuration)), _apply(std::move(apply))
{
    setObjectName("terminalProcessing");
    auto* bar = new QHBoxLayout(this);
    bar->setContentsMargins(4, 2, 4, 2);
    for (int index = 0; index < 2; ++index)
    {
        bar->addWidget(new QLabel(index == 0 ? tr("Rx") : tr("Tx"), this));
        _selectors[index] = new ProcessorSelector(this, favorites);
        _selectors[index]->setObjectName(index == 0 ? "rxProcessor" : "txProcessor");
        _selectors[index]->setMinimumContentsLength(12);
        _selectors[index]->setMaximumWidth(240);
        _selectors[index]->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        bar->addWidget(_selectors[index], 1);
        connect(_selectors[index], &QComboBox::activated, this, [this, index] {
            QString error;
            if (!_flush(&error))
            {
                _error->setText(error);
                _catalog();
                return;
            }
            const auto id = _selectors[index]->currentData().toString();
            if (_draft.value(directionKey(index)).toMap().value("type").toString() == id)
            {
                return;
            }
            if (id.isEmpty())
            {
                _draft.remove(directionKey(index));
            }
            else
            {
                _draft.insert(directionKey(index),
                              QVariantMap {
                                  {"type", id}
                });
            }
            _direction->setCurrentIndex(index);
            _shownDirection = index;
            _showProperties();
        });
    }
    auto* toggle = new QToolButton(this);
    toggle->setObjectName("processingPropertiesToggle");
    toggle->setIcon(Icons::icon("sliders-horizontal"));
    toggle->setToolTip(tr("Processing properties"));
    toggle->setCheckable(true);
    bar->addWidget(toggle);
    bar->addStretch();

    _properties = new QWidget(parent);
    _properties->setObjectName("processingProperties");
    _properties->setMinimumWidth(300);
    auto* layout = new QVBoxLayout(_properties);
    layout->setContentsMargins(6, 4, 6, 4);
    _direction = new QComboBox(_properties);
    _direction->setObjectName("processingPropertyDirection");
    _direction->addItems({tr("Receive (Rx)"), tr("Transmit (Tx)")});
    layout->addWidget(_direction);
    _table = new QTableWidget(0, 2, _properties);
    _table->setObjectName("processingPropertyTable");
    _table->setHorizontalHeaderLabels({tr("Parameter"), tr("Value")});
    _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    _table->setColumnWidth(0, 150);
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _table->verticalHeader()->hide();
    _table->setSelectionMode(QAbstractItemView::NoSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(_table, 1);
    _status = new QLabel(_properties);
    _status->setObjectName("processingDiagnostics");
    _status->setTextFormat(Qt::PlainText);
    _status->setWordWrap(true);
    layout->addWidget(_status);
    _error = new QLabel(_properties);
    _error->setObjectName("processingError");
    _error->setTextFormat(Qt::PlainText);
    _error->setWordWrap(true);
    layout->addWidget(_error);
    auto* buttons = new QHBoxLayout;
    auto* revert  = new QPushButton(tr("Revert"), _properties);
    revert->setObjectName("revertProcessing");
    _applyButton = new QPushButton(tr("Apply"), _properties);
    _applyButton->setObjectName("applyProcessing");
    buttons->addStretch();
    buttons->addWidget(revert);
    buttons->addWidget(_applyButton);
    layout->addLayout(buttons);
    _properties->hide();
    connect(toggle, &QToolButton::toggled, _properties, &QWidget::setVisible);
    connect(toggle, &QToolButton::toggled, this, [this](bool visible) {
        if (visible && _nodes.empty())
        {
            _showProperties();
        }
    });
    connect(_direction, &QComboBox::activated, this, [this](int index) {
        QString error;
        if (!_flush(&error))
        {
            _direction->setCurrentIndex(_shownDirection);
            _error->setText(error);
            return;
        }
        _shownDirection = index;
        _showProperties();
    });
    connect(revert, &QPushButton::clicked, this, [this] {
        _clearRows();
        _nodes.clear();
        _baseline = _latest;
        _draft    = _baseline;
        _catalog();
        _showProperties();
    });
    connect(_applyButton, &QPushButton::clicked, this, [this] {
        QString error;
        applyDraft(&error);
    });
    connect(&_registry, &ProcessorRegistry::changed, this, [this] {
        _catalog();
        bool dirty = _draft != _baseline;
        for (const auto& node : _nodes)
        {
            dirty |= node->model->isDirty();
        }
        if (!dirty)
        {
            _showProperties();
        }
        else
        {
            _error->setText(tr("Catalog changed. Apply validates the current processor definitions."));
        }
    });
    auto* diagnostics = new QTimer(this);
    diagnostics->setInterval(200);
    connect(diagnostics, &QTimer::timeout, this, [this] {
        if (_properties && _properties->isVisible())
        {
            _diagnostics();
        }
    });
    diagnostics->start();
    _catalog();
    _showProperties();
    _diagnostics();
}

TerminalProcessingPanel::~TerminalProcessingPanel()
{
    for (auto* timer : findChildren<QTimer*>())
    {
        timer->stop();
    }
    delete _properties.data();
}

QWidget* TerminalProcessingPanel::propertiesWidget() const
{
    return _properties;
}

void TerminalProcessingPanel::setConfiguration(const QVariantMap& configuration)
{
    _latest = configuration;
    if (_baseline == configuration)
    {
        return;
    }
    bool dirty = _draft != _baseline;
    for (const auto& node : _nodes)
    {
        dirty |= node->model->isDirty();
    }
    if (dirty)
    {
        _error->setText(tr("Terminal settings changed. Revert the draft before editing again."));
        return;
    }
    _baseline = configuration;
    _draft    = configuration;
    _catalog();
    _showProperties();
}

void TerminalProcessingPanel::releaseEditors()
{
    QString error;
    if (!_flush(&error))
    {
        throw std::runtime_error(("Cannot release invalid processing settings: " + error).toStdString());
    }
    _clearRows();
    _nodes.clear();
}

bool TerminalProcessingPanel::applyDraft(QString* error)
{
    QString message;
    if (_latest != _baseline)
    {
        message = tr("Terminal settings changed. Revert the draft before applying.");
    }
    else if (_flush(&message) && _draft != _baseline)
    {
        const QPointer<TerminalProcessingPanel> self(this);
        auto                                    apply    = _apply;
        const auto                              baseline = _baseline;
        const auto                              draft    = _draft;
        const bool                              accepted = apply(baseline, draft, &message);
        if (!self)
        {
            return false;
        }
        if (!accepted && message.isEmpty())
        {
            message = tr("Cannot apply processing settings.");
        }
    }
    if (!message.isEmpty())
    {
        _error->setText(message);
        if (error)
        {
            *error = message;
        }
        return false;
    }
    _baseline = _draft;
    _latest   = _baseline;
    _catalog();
    _showProperties();
    return true;
}

bool TerminalProcessingPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize)
    {
        const auto reset  = _resetControls.value(watched);
        auto*      widget = qobject_cast<QWidget*>(watched);
        if (reset && widget && widget->height() > 0 && reset->height() != widget->height())
        {
            reset->setFixedHeight(widget->height());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TerminalProcessingPanel::_clearRows()
{
    _resetControls.clear();
    for (int row = 0; row < _table->rowCount(); ++row)
    {
        for (int column = 0; column < _table->columnCount(); ++column)
        {
            if (auto* widget = _table->cellWidget(row, column))
            {
                for (auto* child : widget->findChildren<QObject*>())
                {
                    QObject::disconnect(child, nullptr, this, nullptr);
                }
                _table->removeCellWidget(row, column);
                delete widget;
            }
        }
    }
    _table->setRowCount(0);
}

void TerminalProcessingPanel::_catalog()
{
    const auto types = _registry.types();
    for (int index = 0; index < 2; ++index)
    {
        _selectors[index]->setCatalog(types, _draft.value(directionKey(index)).toMap().value("type").toString());
    }
}

bool TerminalProcessingPanel::_flush(QString* error)
{
    auto selection = _draft.value(directionKey(_shownDirection)).toMap();
    auto overrides = selection.value("overrides").toMap();
    for (const auto& node : _nodes)
    {
        if (!node->model->apply(error))
        {
            return false;
        }
        const auto local = node->local->values();
        if (node->path.isEmpty())
        {
            overrides = local;
        }
        else if (local.isEmpty())
        {
            overrides.remove(node->path);
        }
        else
        {
            overrides.insert(node->path, local);
        }
    }
    if (!selection.isEmpty())
    {
        if (overrides.isEmpty())
        {
            selection.remove("overrides");
        }
        else
        {
            selection.insert("overrides", overrides);
        }
        _draft.insert(directionKey(_shownDirection), selection);
    }
    return true;
}

void TerminalProcessingPanel::_showProperties()
{
    _clearRows();
    _nodes.clear();
    _error->clear();
    const auto selection = _draft.value(directionKey(_shownDirection)).toMap();
    const auto id        = selection.value("type").toString();
    if (id.isEmpty())
    {
        return;
    }
    QString    error;
    const auto properties = _registry.properties(id,
                                                 selection.value("configuration").toMap(),
                                                 selection.value("overrides").toMap(),
                                                 &error);
    if (!properties)
    {
        _error->setText(error);
        return;
    }
    try
    {
        for (const auto& property : *properties)
        {
            const auto row = _table->rowCount();
            _table->insertRow(row);
            auto* header = new QTableWidgetItem(property.path.isEmpty() ? property.title : property.path);
            header->setToolTip(property.path + "\n" + property.type);
            auto font = header->font();
            font.setBold(true);
            header->setFont(font);
            header->setBackground(palette().alternateBase());
            _table->setItem(row, 0, header);
            _table->setSpan(row, 0, 1, 2);
            if (property.settingsSchema.isEmpty())
            {
                continue;
            }
            auto node                 = std::make_unique<NodeDraft>();
            node->path                = property.path;
            node->inherited           = std::make_unique<Settings>(QString {});
            node->local               = std::make_unique<Settings>(QString {});
            const auto overrides      = selection.value("overrides").toMap();
            const auto inheritedBytes = node->inherited->serializedContent(property.inheritedConfiguration, &error);
            const auto localBytes     = node->local->serializedContent(property.path.isEmpty() ?
                                                                           overrides :
                                                                           overrides.value(property.path).toMap(),
                                                                       &error);
            if (!inheritedBytes || !localBytes || !node->inherited->replaceSerialized(*inheritedBytes, &error)
                || !node->local->replaceSerialized(*localBytes, &error))
            {
                throw std::runtime_error(error.toStdString());
            }
            auto schema = std::make_shared<SettingsSchema>(property.settingsSchema);
            node->model = std::make_unique<SettingsFormModel>(schema,
                                                              SettingsOverlay(node->inherited->node({}),
                                                                              node->local->node({})));
            auto* model = node->model.get();
            _nodes.push_back(std::move(node));
            for (const auto& field : schema->fields())
            {
                const auto fieldRow = _table->rowCount();
                _table->insertRow(fieldRow);
                auto* label = new QTableWidgetItem(field.title.isEmpty() ? field.key : field.title);
                label->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                label->setToolTip(field.description);
                _table->setItem(fieldRow, 0, label);
                auto* cell = new QWidget(_table);
                _table->setCellWidget(fieldRow, 1, cell);
                auto* layout = new QHBoxLayout(cell);
                layout->setContentsMargins(2, 1, 2, 1);
                auto* editor = _editors.create(field, cell);
                editor->setObjectName(property.path + "/" + field.key);
                editor->setValue(model->value(field.key));
                editor->setEnabled(!field.readOnly);
                auto* reset = new QToolButton(cell);
                reset->setObjectName("reset_" + property.path + "/" + field.key);
                reset->setIcon(Icons::icon("undo-2", QColor("#33200c")));
                reset->setToolTip(tr("Inherited: %1").arg(model->inheritedValue(field.key).toString()));
                reset->setStyleSheet(
                    "QToolButton { background: #f0a13b; border: 1px solid #b87522; border-radius: 3px; }");
                QWidget* control = editor;
                if (editor->layout() && editor->layout()->count() > 0 && editor->layout()->itemAt(0)->widget())
                {
                    control = editor->layout()->itemAt(0)->widget();
                }
                const auto controlHeight = control->sizeHint().height();
                reset->setFixedSize(20, controlHeight > 0 ? controlHeight : fontMetrics().height() + 6);
                control->installEventFilter(this);
                _resetControls.insert(control, reset);
                reset->setVisible(model->hasOverride(field.key));
                const bool compact = editor->sizePolicy().horizontalPolicy() == QSizePolicy::Maximum;
                layout->addWidget(editor, compact ? 0 : 1);
                layout->addWidget(reset, 0, Qt::AlignVCenter);
                if (compact)
                {
                    layout->addStretch();
                }
                _table->setRowHeight(fieldRow, cell->sizeHint().height());
                connect(editor, &SettingsFieldEditor::valueChanged, this, [this, editor, model, field, reset] {
                    if (_updating)
                    {
                        return;
                    }
                    model->setInputError(field.key, editor->inputError());
                    if (editor->hasAcceptableInput())
                    {
                        model->setValue(field.key, editor->value());
                    }
                    reset->setVisible(model->hasOverride(field.key));
                    _error->setText(model->isValid() ? QString {} : tr("Invalid value: %1").arg(field.title));
                });
                connect(editor, &SettingsFieldEditor::validationChanged, this, [editor, model, field] {
                    model->setInputError(field.key, editor->inputError());
                });
                connect(reset, &QToolButton::clicked, this, [this, model, editor, reset, field] {
                    model->removeValue(field.key);
                    _updating = true;
                    editor->setValue(model->value(field.key));
                    _updating = false;
                    model->setInputError(field.key, editor->inputError());
                    reset->hide();
                });
            }
        }
    }
    catch (const std::exception& exception)
    {
        _clearRows();
        _nodes.clear();
        _error->setText(QString::fromUtf8(exception.what()));
    }
}

void TerminalProcessingPanel::_diagnostics()
{
    QStringList text;
    QStringList detail;
    for (int index = 0; index < 2; ++index)
    {
        const auto value = _router.diagnostics(index == 0 ? ProcessingRouter::Direction::Rx :
                                                            ProcessingRouter::Direction::Tx);
        const auto title = index == 0 ? tr("Rx") : tr("Tx");
        text.append(tr("%1: %2").arg(title, stateName(value.state)));
        detail.append(tr("%1 queue: %2 B, %3 chunks").arg(title).arg(value.pendingBytes).arg(value.pendingChunks));
        detail.append(tr("%1: accepted %2 B, forwarded %3 B, failures %4")
                          .arg(title)
                          .arg(value.acceptedBytes)
                          .arg(value.forwardedBytes)
                          .arg(value.failures));
        if (!value.lastError.isEmpty())
        {
            detail.append(title + ": " + value.lastError);
        }
    }
    _status->setText(text.join(QStringLiteral("  ·  ")));
    _status->setToolTip(detail.join('\n'));
}
