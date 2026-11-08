#include "ui_AboutDialog.h"

#include <Termaster/Widgets/AboutDialog.h>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent), ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
