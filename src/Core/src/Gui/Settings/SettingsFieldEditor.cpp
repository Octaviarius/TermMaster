#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLayout>
#include <QLineEdit>
#include <QStyle>
#include <QStyleOptionButton>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <algorithm>

QWidget* SettingsFieldEditor::primaryControl() const
{
    if (layout() && layout()->count() > 0)
    {
        if (auto* control = layout()->itemAt(0)->widget())
        {
            return control;
        }
    }
    return const_cast<SettingsFieldEditor*>(this);
}

int SettingsFieldEditor::controlVisualHeight(const QWidget* control, int height)
{
    if (const auto* check = qobject_cast<const QCheckBox*>(control))
    {
        QStyleOptionButton option;
        option.initFrom(check);
        option.rect = check->rect();
        return check->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, check).height();
    }
    // Qt's Windows 11 style paints these frames 1.5 logical pixels inside option.rect.
    // Its public frame/layout subrects still return the whole widget rectangle.
    const bool insetFrame = control->styleSheet().isEmpty()
                         && control->style()->objectName() == QStringLiteral("windows11")
                         && (qobject_cast<const QComboBox*>(control) || qobject_cast<const QAbstractSpinBox*>(control)
                             || qobject_cast<const QLineEdit*>(control));
    return std::max(1, (height >= 0 ? height : control->height()) - (insetFrame ? 2 : 0));
}
