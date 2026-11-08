#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <Termaster/Core/gui/settings/SettingsFormWidget.h>
#include <algorithm>

SettingsFormWidget::SettingsFormWidget(SettingsFormModel& model, SettingsWidgetRegistry& registry, QWidget* parent) :
    QWidget(parent), _model(&model)
{
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
    QHash<QString, QFormLayout*> forms;
    auto                         addGroup = [&](const QString& id, const QString& title) {
        if (forms.contains(id))
        {
            return;
        }
        auto* box  = new QGroupBox(title, contents);
        auto* form = new QFormLayout(box);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        forms.insert(id, form);
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
        if (!field.required)
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
                    _model->setValue(key, _editors.value(key)->value());
                }
                else
                {
                    _model->removeValue(key);
                }
            });
        }
        controlRow->addWidget(editorWidget, 1);
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
        forms.value(field.group)->addRow(label, container);
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
            _refreshErrors();
        });
        _refresh(field.key);
    }
    layout->addStretch();
    scroll->setWidget(contents);
    outer->addWidget(scroll);
    connect(&model, &SettingsFormModel::fieldChanged, this, &SettingsFormWidget::_refresh);
    connect(&model, &SettingsFormModel::validationChanged, this, &SettingsFormWidget::_refreshErrors);
    connect(&model, &QObject::destroyed, this, [this] { setEnabled(false); });
    _refreshErrors();
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
    fieldEditor->setEnabled(!it->readOnly && (_model->contains(key) || it->required));
    if (auto* presence = _presence.value(key))
    {
        const QSignalBlocker blocker(presence);
        presence->setChecked(_model->contains(key));
    }
    _updating = false;
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
}
