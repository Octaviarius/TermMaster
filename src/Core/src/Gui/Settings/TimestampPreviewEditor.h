#pragma once

#include <QTimer>
#include <QVariantMap>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>

class QLabel;
class QLineEdit;
class TerminalDisplay;

/** Presentation-only Timestamp draft preview. Owns no transport or persisted settings. */
class TimestampPreviewEditor final : public SettingsFieldEditor
{
public:
    explicit TimestampPreviewEditor(QWidget* parent = nullptr);
    QVariant value() const override;
    void     setValue(const QVariant& value) override;
    void     setContext(const QVariantMap& context) override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    TerminalDisplay* _display = nullptr;
    QLineEdit*       _sample  = nullptr;
    QLabel*          _error   = nullptr;
    QVariantMap      _configuration;
    QTimer           _timer;

    void _render();
    void _schedule();
};
