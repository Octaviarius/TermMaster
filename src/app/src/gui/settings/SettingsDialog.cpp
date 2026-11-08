#include "SettingsDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/gui/settings/SettingsFormModel.h>
#include <Termaster/Core/gui/settings/SettingsFormWidget.h>
#include <Termaster/Core/gui/settings/SettingsWidgetRegistry.h>

SettingsDialog::SettingsDialog(std::shared_ptr<const SettingsSchema> schema,
                               SettingsOverlay                       target,
                               SettingsWidgetRegistry&               registry,
                               std::function<bool(QString*)>         persist,
                               QWidget* parent) : QDialog(parent), _persist(std::move(persist))
{
    setObjectName("settingsDialog");
    setWindowTitle(tr("Settings"));
    resize(620, 420);
    auto* layout = new QVBoxLayout(this);
    _model       = new SettingsFormModel(std::move(schema), std::move(target), this);
    layout->addWidget(new SettingsFormWidget(*_model, registry, this));
    _error = new QLabel(this);
    _error->setObjectName("settingsError");
    _error->setTextFormat(Qt::PlainText);
    _error->setWordWrap(true);
    layout->addWidget(_error);
    _buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    layout->addWidget(_buttons);
    connect(_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (_apply())
        {
            accept();
        }
    });
    connect(_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { _apply(); });
    connect(_model, &SettingsFormModel::validationChanged, this, &SettingsDialog::_updateButtons);
    connect(_model, &SettingsFormModel::dirtyChanged, this, &SettingsDialog::_updateButtons);
    _updateButtons();
}

bool SettingsDialog::_apply()
{
    QString error;
    if (!_model->apply(&error))
    {
        _error->setText(error);
        _updateButtons();
        return false;
    }
    if (!_persist(&error))
    {
        _pendingPersistence = true;
        _error->setText(error);
        _updateButtons();
        return false;
    }
    _pendingPersistence = false;
    _error->clear();
    _updateButtons();
    emit applied();
    return true;
}

void SettingsDialog::_updateButtons()
{
    _buttons->button(QDialogButtonBox::Ok)->setEnabled(_model->isValid());
    _buttons->button(QDialogButtonBox::Apply)
        ->setEnabled(_model->isValid() && (_model->isDirty() || _pendingPersistence));
}
