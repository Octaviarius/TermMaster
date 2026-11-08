#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <Termaster/Core/gui/settings/SettingsFormWidget.h>
#include <algorithm>

static void initializeSettingsFormResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(settingsFormIcons);
        return true;
    }();
    Q_UNUSED(initialized)
}

SettingsFormWidget::SettingsFormWidget(SettingsFormModel& model, SettingsWidgetRegistry& registry, QWidget* parent) :
    QWidget(parent), _model(&model)
{
    initializeSettingsFormResources();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* contents = new QWidget(scroll);
    auto* layout   = new QVBoxLayout(contents);
    _summary       = new QLabel(contents);
    _summary->setTextFormat(Qt::PlainText);
    _summary->setWordWrap(true);
    _summary->setStyleSheet("color: #c43c35;");
    _summary->setAccessibleName(tr("Form validation errors"));
    layout->addWidget(_summary);
    auto addGroup = [&](const QString& id, const QString& title) {
        if (_groups.contains(id))
        {
            return;
        }
        auto* box  = new QGroupBox(title, contents);
        auto* form = new QFormLayout(box);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        _groups.insert(id, {title, box, form});
        layout->addWidget(box);
    };
    auto groups = model.schema().groups();
    std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
    for (const auto& group : groups)
    {
        addGroup(group.id, group.title);
    }
    auto fields = model.schema().fields();
    std::stable_sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
    for (const auto& field : fields)
    {
        addGroup(field.group, field.group.isEmpty() ? tr("Settings") : field.group);
        auto* container   = new QWidget(contents);
        auto* fieldLayout = new QVBoxLayout(container);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        auto* controlRow = new QHBoxLayout;
        controlRow->setContentsMargins(0, 0, 0, 0);
        fieldLayout->addLayout(controlRow);
        auto* editorWidget = registry.create(field, container);
        _editors.insert(field.key, editorWidget);
        editorWidget->setToolTip(field.description);
        if (!model.supportsInheritance() && !field.required)
        {
            auto* present = new QCheckBox(tr("Set value"), container);
            present->setToolTip(
                tr("Uncheck to remove the value from the active scope. An inherited value becomes active again."));
            present->setEnabled(!field.readOnly);
            _presence.insert(field.key, present);
            controlRow->addWidget(present, 0, Qt::AlignVCenter);
            connect(present, &QCheckBox::toggled, this, [this, key = field.key](bool checked) {
                if (!_model || _updating)
                {
                    return;
                }
                if (checked)
                {
                    _model->setValue(key,
                                     _model->supportsInheritance() ? _model->value(key) : _editors.value(key)->value());
                }
                else
                {
                    _model->removeValue(key);
                }
                _refresh(key);
            });
        }
        controlRow->addWidget(editorWidget, 1);
        if (model.supportsInheritance())
        {
            auto* reset = new QToolButton(container);
            reset->setObjectName(QStringLiteral("reset_") + field.key);
            reset->setIcon(QIcon(":/Termaster/Core/gui/settings/ResetOverride.svg"));
            reset->setIconSize(QSize(14, 14));
            reset->setAccessibleName(
                tr("Reset %1 to inherited value").arg(field.title.isEmpty() ? field.key : field.title));
            reset->setFixedSize(20, 26);
            reset->setStyleSheet("QToolButton { background: #f0a13b; border: 1px solid #b87522; border-radius: 4px; }"
                                 "QToolButton:hover { background: #ffbd66; }"
                                 "QToolButton:pressed { background: #d98a24; }"
                                 "QToolButton:disabled { background: #d8c6ad; border-color: #b8aa98; }");
            auto resetPolicy = reset->sizePolicy();
            resetPolicy.setRetainSizeWhenHidden(true);
            reset->setSizePolicy(resetPolicy);
            _resets.insert(field.key, reset);
            controlRow->addWidget(reset, 0, Qt::AlignTop);
            connect(reset, &QToolButton::clicked, this, [this, key = field.key] {
                if (_model)
                {
                    _model->removeValue(key);
                    _refresh(key);
                }
            });
        }
        auto* error = new QLabel(container);
        error->setTextFormat(Qt::PlainText);
        error->setWordWrap(true);
        error->setStyleSheet("color: #c43c35;");
        error->setAccessibleName(tr("Validation error"));
        fieldLayout->addWidget(error);
        _errors.insert(field.key, error);
        auto* label = new QLabel(field.title.isEmpty() ? field.key : field.title, contents);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(false);
        label->setBuddy(editorWidget);
        label->setToolTip(field.description);
        _groups.value(field.group).form->addRow(label, container);
        _rows.insert(field.key, {field, container, true});
        connect(editorWidget, &SettingsFieldEditor::valueChanged, this, [this, key = field.key, editorWidget] {
            if (!_model || _updating)
            {
                return;
            }
            _updating = true;
            if (editorWidget->hasAcceptableInput())
            {
                _model->setValue(key, editorWidget->value());
            }
            _model->setInputError(key, editorWidget->inputError());
            _updating = false;
            _refreshState(key);
            _refreshErrors();
        });
        _refresh(field.key);
    }
    _empty = new QLabel(tr("No settings match your search."), contents);
    _empty->setTextFormat(Qt::PlainText);
    _empty->setWordWrap(true);
    layout->addWidget(_empty);
    layout->addStretch();
    scroll->setWidget(contents);
    outer->addWidget(scroll);
    connect(&model, &SettingsFormModel::fieldChanged, this, &SettingsFormWidget::_refresh);
    connect(&model, &SettingsFormModel::validationChanged, this, &SettingsFormWidget::_refreshErrors);
    connect(&model, &QObject::destroyed, this, [this] { setEnabled(false); });
    _refreshErrors();
}

void SettingsFormWidget::setFilter(const QString& text)
{
    _filter = text.trimmed();
    _applyFilter();
}

bool SettingsFormWidget::hasMatches() const
{
    return _hasMatches;
}

QStringList SettingsFormWidget::visibleFields() const
{
    QStringList keys;
    for (auto it = _rows.cbegin(); it != _rows.cend(); ++it)
    {
        if (it->visible)
        {
            keys.append(it.key());
        }
    }
    keys.sort();
    return keys;
}

void SettingsFormWidget::_applyFilter()
{
    const auto    matches = [this](const QString& text) { return text.contains(_filter, Qt::CaseInsensitive); };
    QSet<QString> visibleGroups;
    _hasMatches = false;
    for (auto it = _rows.begin(); it != _rows.end(); ++it)
    {
        const auto& field = it->field;
        const auto& group = _groups[field.group];
        it->visible       = _filter.isEmpty() || matches(field.group) || matches(group.title) || matches(field.key)
                         || matches(field.title) || matches(field.description) || matches(field.settingsPath);
        group.form->setRowVisible(it->container, it->visible);
        if (it->visible)
        {
            visibleGroups.insert(field.group);
            _hasMatches = true;
        }
    }
    for (auto it = _groups.cbegin(); it != _groups.cend(); ++it)
    {
        it->box->setVisible(visibleGroups.contains(it.key()));
    }
    _empty->setVisible(!_hasMatches);
}

SettingsFieldEditor* SettingsFormWidget::editor(const QString& key) const
{
    return _editors.value(key);
}

void SettingsFormWidget::_refresh(const QString& key)
{
    if (!_model || _updating)
    {
        return;
    }
    auto* fieldEditor = _editors.value(key);
    if (!fieldEditor)
    {
        return;
    }
    const auto& fields = _model->schema().fields();
    const auto  it = std::find_if(fields.begin(), fields.end(), [&key](const auto& field) { return field.key == key; });
    if (it == fields.end())
    {
        return;
    }
    _updating = true;
    fieldEditor->setValue(_model->value(key));
    fieldEditor->setEnabled(!it->readOnly && (_model->supportsInheritance() || _model->contains(key) || it->required));
    if (auto* presence = _presence.value(key))
    {
        const QSignalBlocker blocker(presence);
        presence->setChecked(_model->supportsInheritance() ? _model->hasOverride(key) : _model->contains(key));
    }
    _updating = false;
    _refreshState(key);
}

void SettingsFormWidget::_refreshState(const QString& key)
{
    if (!_model || !_model->supportsInheritance())
    {
        return;
    }
    auto* editor = _editors.value(key);
    auto* reset  = _resets.value(key);
    if (!editor || !reset)
    {
        return;
    }
    const bool inherited = !_model->hasOverride(key);
    editor->setProperty("settingsInherited", inherited);
    const auto fallback = _model->inheritedValue(key);
    QString    shown    = fallback.isValid() ? fallback.toString() : tr("No value");
    if (fallback.typeId() == QMetaType::QVariantMap || fallback.typeId() == QMetaType::QVariantList)
    {
        shown = QString::fromUtf8(QJsonDocument::fromVariant(fallback).toJson(QJsonDocument::Compact));
    }
    reset->setToolTip(tr("Reset to inherited/default value: %1").arg(shown));
    reset->setVisible(!inherited);
    reset->setEnabled(_model->canReset(key));
    const auto    field = _rows.value(key).field;
    const QString state = inherited ? tr("Inherited value: %1. Edit to override.").arg(shown) :
                                      tr("Local override. Reset value: %1").arg(shown);
    editor->setToolTip(field.description.isEmpty() ? state : field.description + "\n" + state);
    editor->setAccessibleDescription(state);
}

void SettingsFormWidget::_refreshErrors()
{
    if (!_model)
    {
        return;
    }
    _summary->clear();
    _summary->hide();
    for (auto* label : _errors)
    {
        label->clear();
        label->hide();
    }
    for (const auto& error : _model->errors())
    {
        QString key = error.path;
        if (!_errors.contains(key))
        {
            if (key.startsWith('/'))
            {
                key.remove(0, 1);
            }
            key.replace("~1", "/");
            key.replace("~0", "~");
        }
        auto* label = _errors.value(key);
        if (!label)
        {
            for (const auto& field : _model->schema().fields())
            {
                QString escaped = field.key;
                escaped.replace("~", "~0");
                escaped.replace("/", "~1");
                if (error.path.startsWith('/' + escaped + '/'))
                {
                    label = _errors.value(field.key);
                    break;
                }
            }
        }
        if (!label)
        {
            label = _summary;
        }
        label->setText(label->text().isEmpty() ? error.message : label->text() + '\n' + error.message);
        label->show();
    }
    _applyFilter();
}
