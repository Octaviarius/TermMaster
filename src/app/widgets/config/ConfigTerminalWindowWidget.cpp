#include "ConfigTerminalWindowWidget.h"

#include "ui_ConfigTerminalWindowWidget.h"

#include <QFontDatabase>
#include <QFontDialog>

ConfigTerminalWindowWidget::ConfigTerminalWindowWidget(SettingsProxy config, QWidget* parent) :
    ConfigBaseWidget(config, parent), ui(new Ui::ConfigTerminalWindowWidget)
{
    ui->setupUi(this);
    connect(ui->btnFontSelect, SIGNAL(clicked()), this, SLOT(_onFontSelectClicked()));
}

ConfigTerminalWindowWidget::~ConfigTerminalWindowWidget()
{
    delete ui;
}

void ConfigTerminalWindowWidget::commit()
{
}

void ConfigTerminalWindowWidget::_onFontSelectClicked()
{
    auto Widget = new QFontDialog();

    Widget->setOption(QFontDialog::FontDialogOption::MonospacedFonts, ui->chbFontMonospace->isChecked());
    Widget->setOption(QFontDialog::FontDialogOption::ProportionalFonts, ui->chbFontProportional->isChecked());
    Widget->setOption(QFontDialog::FontDialogOption::ScalableFonts, ui->chbFontScalable->isChecked());
    Widget->setOption(QFontDialog::FontDialogOption::NonScalableFonts, ui->chbFontNonScalable->isChecked());

    Widget->setCurrentFont(QFont(_config.value("family").toString(), _config.value("size").toInt()));

    int res = Widget->exec();

    if (res == 1)
    {
        auto font = Widget->currentFont();

        ui->lblFontExample->setFont(font);
        ui->lblFontCurrentValue->setText(
            QString("%1, %2, %3pt").arg(font.family()).arg(font.styleName()).arg(font.pointSize()));
    }
}
