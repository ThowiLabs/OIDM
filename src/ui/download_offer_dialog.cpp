#include "ui/download_offer_dialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace oidm {

DownloadOfferDialog::DownloadOfferDialog(const Download &download, QWidget *parent)
    : QDialog(parent), m_id(download.id)
{
    setObjectName(QStringLiteral("downloadOfferDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Información del archivo — OIDM"));
    setMinimumWidth(610);
    resize(710, 320);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(15);
    auto *title = new QLabel(tr("Preparar descarga"), this);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold; color: #d9eeff;"));
    layout->addWidget(title);

    auto *form = new QFormLayout();
    form->setVerticalSpacing(10);
    auto *filename = new QLabel(download.fileName, this);
    filename->setObjectName(QStringLiteral("downloadOfferName"));
    filename->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *address = new QLabel(this);
    address->setText(download.url.toHtmlEscaped());
    address->setWordWrap(true);
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_size = new QLabel(download.totalBytes > 0 ? formatBytes(download.totalBytes) : tr("Consultando servidor…"), this);
    m_size->setObjectName(QStringLiteral("downloadOfferSize"));
    m_type = new QLabel(download.contentType.isEmpty() ? tr("Desconocido") : download.contentType, this);
    form->addRow(tr("Archivo:"), filename);
    form->addRow(tr("Dirección:"), address);
    form->addRow(tr("Tamaño:"), m_size);
    form->addRow(tr("Tipo:"), m_type);
    layout->addLayout(form);

    auto *destinationRow = new QHBoxLayout();
    m_destination = new QLineEdit(download.destination, this);
    m_destination->setObjectName(QStringLiteral("downloadOfferDestination"));
    auto *browse = new QPushButton(tr("Examinar…"), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Guardar archivo"), m_destination->text());
        if (!path.isEmpty()) m_destination->setText(path);
    });
    destinationRow->addWidget(m_destination, 1);
    destinationRow->addWidget(browse);
    layout->addWidget(new QLabel(tr("Guardar como:"), this));
    layout->addLayout(destinationRow);

    auto *note = new QLabel(tr("La descarga no comenzará hasta que pulses Iniciar descarga. "
        "Si cancelas, OIDM descartará esta transferencia."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #9eafc2;"));
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(this);
    auto *start = buttons->addButton(tr("Iniciar descarga"), QDialogButtonBox::AcceptRole);
    start->setObjectName(QStringLiteral("downloadOfferStart"));
    auto *cancel = buttons->addButton(tr("Cancelar"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("downloadOfferCancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emit startRequested(destination());
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    probeMetadata(download);
}

QString DownloadOfferDialog::destination() const { return m_destination->text().trimmed(); }

void DownloadOfferDialog::probeMetadata(const Download &download)
{
    if (download.totalBytes > 0 && !download.contentType.isEmpty()) return;
    // HEAD requests metadata only: never download a second copy of the file.
    m_network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(download.url)};
    request.setTransferTimeout(9000);
    request.setMaximumRedirectsAllowed(5);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    const QUrl referrer(download.referrer);
    if (referrer.isValid() && !referrer.host().isEmpty()
        && (referrer.scheme() == QStringLiteral("https") || referrer.scheme() == QStringLiteral("http"))) {
        request.setRawHeader(QByteArrayLiteral("Referer"), referrer.toString(QUrl::FullyEncoded).toUtf8());
    }
    QNetworkReply *reply = m_network->head(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, known = download.totalBytes] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            if (known <= 0) {
                const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
                if (length.isValid() && length.toLongLong() > 0) m_size->setText(formatBytes(length.toLongLong()));
                else m_size->setText(tr("No anunciado por el servidor"));
            }
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            if (!type.isEmpty()) m_type->setText(type);
        } else if (known <= 0) {
            m_size->setText(tr("No disponible antes de iniciar"));
        }
        reply->deleteLater();
    });
}

} // namespace oidm
