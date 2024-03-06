#include "ConfigTerminalDialog.h"
#include "ui_ConfigTerminalDialog.h"

#include <QFontDatabase>
#include <QFontDialog>

ConfigTerminalDialog::ConfigTerminalDialog(QWidget* parent, const PathedValue& config) :
    QDialog(parent), ui(new Ui::ConfigTerminalDialog)
{
    ui->setupUi(this);
    _config = config;
    connect(ui->btnFontSelect, SIGNAL(clicked()), this, SLOT(_onFontSelectClicked()));
}

ConfigTerminalDialog::~ConfigTerminalDialog()
{
    delete ui;
}

int ConfigTerminalDialog::show(PathedValue& config)
{
}

void ConfigTerminalDialog::_onFontSelectClicked()
{
    auto dialog = new QFontDialog();

    dialog->setOption(QFontDialog::FontDialogOption::MonospacedFonts, ui->chbFontMonospace->isChecked());
    dialog->setOption(QFontDialog::FontDialogOption::ProportionalFonts, ui->chbFontProportional->isChecked());
    dialog->setOption(QFontDialog::FontDialogOption::ScalableFonts, ui->chbFontScalable->isChecked());
    dialog->setOption(QFontDialog::FontDialogOption::NonScalableFonts, ui->chbFontNonScalable->isChecked());

    dialog->setCurrentFont(QFont(_config["family"].toString(), _config["size"].toInt()));

    int res = dialog->exec();

    if (res == 1)
    {
        auto font = dialog->currentFont();

        ui->lblFontExample->setFont(font);
        ui->lblFontCurrentValue->setText(
            QString("%1, %2, %3pt").arg(font.family()).arg(font.styleName()).arg(font.pointSize()));
    }
}
