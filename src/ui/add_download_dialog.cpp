#include "ui/add_download_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace oidm {

AddDownloadDialog::AddDownloadDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Agregar descarga"));
    setModal(true);
    resize(560, 170);

    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(QStringLiteral("https://servidor.com/archivo.zip"));
    m_destination = new QLineEdit(this);
    m_destination->setPlaceholderText(tr("Selecciona dónde guardar el archivo"));

    auto *browse = new QPushButton(tr("Examinar…"), this);
    connect(browse, &QPushButton::clicked, this, &AddDownloadDialog::browseDestination);

    auto *destinationRow = new QHBoxLayout();
    destinationRow->addWidget(m_destination, 1);
    destinationRow->addWidget(browse);

    auto *form = new QFormLayout();
    form->addRow(tr("Dirección URL:"), m_url);
    form->addRow(tr("Guardar en:"), destinationRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Agregar a la cola"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    connect(m_url, &QLineEdit::textChanged, this, &AddDownloadDialog::validateForm);
    connect(m_destination, &QLineEdit::textChanged, this, &AddDownloadDialog::validateForm);
    validateForm();
}

Download AddDownloadDialog::download() const
{
    const QUrl url(m_url->text().trimmed());
    Download item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.url = url.toString(QUrl::FullyEncoded);
    item.destination = m_destination->text().trimmed();
    item.fileName = QFileInfo(item.destination).fileName();
    item.status = DownloadStatus::AwaitingConfirmation;
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.updatedAt = item.createdAt;
    return item;
}

void AddDownloadDialog::browseDestination()
{
    const QUrl url(m_url->text().trimmed());
    QString name = url.fileName(QUrl::FullyDecoded);
    if (name.isEmpty()) name = QStringLiteral("download.bin");
    const QString initial = m_destination->text().isEmpty()
        ? QDir::home().filePath(name) : m_destination->text();
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar descarga"), initial);
    if (!path.isEmpty()) m_destination->setText(path);
}

void AddDownloadDialog::validateForm()
{
    const QUrl url(m_url->text().trimmed());
    const bool validScheme = url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https");
    const bool validUrl = url.isValid() && !url.host().isEmpty() && validScheme;
    const bool validDestination = !m_destination->text().trimmed().isEmpty();
    auto *buttons = findChild<QDialogButtonBox *>();
    if (buttons != nullptr) buttons->button(QDialogButtonBox::Ok)->setEnabled(validUrl && validDestination);
}

} // namespace oidm
