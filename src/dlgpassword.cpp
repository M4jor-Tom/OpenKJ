#include "dlgpassword.h"
#include "ui_dlgpassword.h"
#include "settings.h"

#include <QMessageBox>
#include <QApplication>

DlgPassword::DlgPassword(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DlgPassword)
{
    ui->setupUi(this);
    ui->label->hide();
}

DlgPassword::~DlgPassword()
{
    delete ui;
}

void DlgPassword::on_pushButtonOk_clicked()
{
    Settings settings;
    // chkPassword now runs PBKDF2 with a deliberately high iteration count, which
    // takes a noticeable fraction of a second here and over a second on modest
    // hardware. Without this the dialog just appears to hang on the click.
    ui->pushButtonOk->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = settings.chkPassword(ui->lineEditPassword->text());
    QApplication::restoreOverrideCursor();
    ui->pushButtonOk->setEnabled(true);

    if (ok)
    {
        ui->label->hide();
        password = ui->lineEditPassword->text();
        accept();
    }
    else
    {
        ui->label->show();
    }
}

void DlgPassword::on_pushButtonCancel_clicked()
{
    reject();
}

void DlgPassword::on_pushButtonReset_clicked()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Clear password?"));
    msgBox.setText(tr("Warning, this will erase all secured account and credit card data."));
    if (msgBox.exec() == QDialog::Accepted)
    {
        Settings settings;
        settings.clearPassword();
    }

}
