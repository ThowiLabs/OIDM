#include "ui/download_progress_dialog.h"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace oidm {

DownloadProgressDialog::DownloadProgressDialog(const Download &download, QWidget *parent)
    : QDialog(parent), m_id(download.id)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(download.fileName + tr(" — Detalles de descarga"));
    setMinimumSize(560, 460);
    resize(640, 505);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);

    auto *header = new QLabel(download.fileName, this);
    header->setObjectName(QStringLiteral("transferHeading"));
    header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(header);

    auto *tabs = new QTabWidget(this);
    auto *statusTab = new QWidget(tabs);
    auto *statusLayout = new QVBoxLayout(statusTab);
    auto *form = new QFormLayout();
    form->setVerticalSpacing(9);
    m_urlLabel = new QLabel(statusTab);
    m_urlLabel->setWordWrap(true);
    m_urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel = new QLabel(statusTab);
    m_sizeLabel = new QLabel(statusTab);
    m_downloadedLabel = new QLabel(statusTab);
    m_speedLabel = new QLabel(statusTab);
    m_timeLabel = new QLabel(statusTab);
    m_resumeLabel = new QLabel(statusTab);
    form->addRow(tr("Dirección:"), m_urlLabel);
    form->addRow(tr("Estado:"), m_statusLabel);
    form->addRow(tr("Tamaño:"), m_sizeLabel);
    form->addRow(tr("Descargado:"), m_downloadedLabel);
    form->addRow(tr("Velocidad:"), m_speedLabel);
    form->addRow(tr("Tiempo restante:"), m_timeLabel);
    form->addRow(tr("Reanudar:"), m_resumeLabel);
    statusLayout->addLayout(form);

    m_progress = new QProgressBar(statusTab);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(true);
    m_progress->setMinimumHeight(23);
    statusLayout->addWidget(m_progress);
    m_connectionsLabel = new QLabel(statusTab);
    m_connectionsLabel->setObjectName(QStringLiteral("connectionsHeading"));
    statusLayout->addWidget(m_connectionsLabel);
    m_connections = new QTableWidget(statusTab);
    m_connections->setColumnCount(2);
    m_connections->setHorizontalHeaderLabels({tr("Conexión / segmento"), tr("Progreso real")});
    m_connections->horizontalHeader()->setStretchLastSection(true);
    m_connections->verticalHeader()->hide();
    m_connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_connections->setSelectionMode(QAbstractItemView::NoSelection);
    m_connections->setAlternatingRowColors(true);
    statusLayout->addWidget(m_connections, 1);
    tabs->addTab(statusTab, tr("Estado de descarga"));

    auto *limitsTab = new QWidget(tabs);
    auto *limitsLayout = new QVBoxLayout(limitsTab);
    auto *limitsNote = new QLabel(tr("El limitador de velocidad todavía no está disponible en esta versión."), limitsTab);
    limitsNote->setWordWrap(true);
    limitsLayout->addWidget(limitsNote);
    limitsLayout->addStretch();
    tabs->addTab(limitsTab, tr("Limitador de velocidad"));

    auto *completionTab = new QWidget(tabs);
    auto *completionLayout = new QVBoxLayout(completionTab);
    auto *completionNote = new QLabel(tr("Las acciones automáticas al finalizar se incorporarán en una versión posterior."), completionTab);
    completionNote->setWordWrap(true);
    completionLayout->addWidget(completionNote);
    completionLayout->addStretch();
    tabs->addTab(completionTab, tr("Al finalizar"));
    layout->addWidget(tabs, 1);

    auto *buttons = new QHBoxLayout();
    m_pauseButton = new QPushButton(tr("Pausar"), this);
    m_resumeButton = new QPushButton(tr("Reanudar"), this);
    m_cancelButton = new QPushButton(tr("Cancelar"), this);
    auto *closeButton = new QPushButton(tr("Cerrar detalles"), this);
    connect(m_pauseButton, &QPushButton::clicked, this, [this] { emit pauseRequested(m_id); });
    connect(m_resumeButton, &QPushButton::clicked, this, [this] { emit resumeRequested(m_id); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { emit cancelRequested(m_id); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(m_pauseButton);
    buttons->addWidget(m_resumeButton);
    buttons->addWidget(m_cancelButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);
    updateDownload(download);
}

void DownloadProgressDialog::updateDownload(const Download &download)
{
    if (download.id != m_id) return;
    m_urlLabel->setText(download.url.toHtmlEscaped());
    m_statusLabel->setText(statusText(download.status) +
        (download.error.isEmpty() ? QString{} : QStringLiteral(" — ") + download.error));
    m_sizeLabel->setText(formatBytes(download.totalBytes));
    m_downloadedLabel->setText(formatBytes(download.downloadedBytes));
    m_speedLabel->setText(formatSpeed(download.speedBytesPerSecond));
    m_progress->setValue(qRound(download.progressPercent() * 10));
    if (download.totalBytes > 0 && download.speedBytesPerSecond > 1.0 &&
        download.downloadedBytes < download.totalBytes) {
        const auto remaining = static_cast<qint64>(
            static_cast<double>(download.totalBytes - download.downloadedBytes) / download.speedBytesPerSecond);
        m_timeLabel->setText(tr("%1 min %2 s").arg(remaining / 60).arg(remaining % 60));
    } else {
        m_timeLabel->setText(tr("Calculando…"));
    }
    m_resumeLabel->setText(download.status == DownloadStatus::Paused ? tr("Disponible si el servidor admite rangos") : tr("Según compatibilidad del servidor"));
    const auto &segments = download.connectionPercents;
    m_connectionsLabel->setText(segments.isEmpty() ? tr("Conexiones: comprobando servidor")
        : tr("Conexiones utilizadas: %1").arg(segments.size()));
    m_connections->setRowCount(static_cast<int>(segments.size()));
    for (int i = 0; i < segments.size(); ++i) {
        auto *name = m_connections->item(i, 0);
        if (!name) { name = new QTableWidgetItem(); m_connections->setItem(i, 0, name); }
        name->setText(tr("Segmento %1").arg(i + 1));
        auto *progress = m_connections->item(i, 1);
        if (!progress) { progress = new QTableWidgetItem(); m_connections->setItem(i, 1, progress); }
        progress->setText(QStringLiteral("%1 %").arg(segments.at(i)));
    }
    const bool active = download.status == DownloadStatus::Downloading || download.status == DownloadStatus::Inspecting;
    m_pauseButton->setEnabled(active);
    m_resumeButton->setEnabled(download.status == DownloadStatus::Paused || download.status == DownloadStatus::Failed || download.status == DownloadStatus::Queued);
    m_cancelButton->setEnabled(download.status != DownloadStatus::Cancelled && download.status != DownloadStatus::Completed);
}

} // namespace oidm
