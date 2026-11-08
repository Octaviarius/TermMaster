#pragma once

#include <QDialog>
#include <functional>
#include <memory>

class SettingsSchema;
class SettingsOverlay;
class SettingsWidgetRegistry;
class SettingsFormModel;
class QLabel;
class QDialogButtonBox;

/** Applies a generated draft, then asks the application owner to persist it. */
class SettingsDialog final : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(std::shared_ptr<const SettingsSchema> schema,
                   SettingsOverlay                       target,
                   SettingsWidgetRegistry&               registry,
                   std::function<bool(QString*)>         persist,
                   QWidget*                              parent = nullptr);
    Q_SIGNAL void applied();

private:
    SettingsFormModel*            _model;
    QLabel*                       _error;
    QDialogButtonBox*             _buttons;
    std::function<bool(QString*)> _persist;
    bool                          _pendingPersistence = false;

    bool _apply();
    void _updateButtons();
};
