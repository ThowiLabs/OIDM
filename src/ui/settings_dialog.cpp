#include "ui/settings_dialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace oidm {

int SettingsDialog::preferredConnections()
{
    return qBound(1, QSettings().value(QStringLiteral("transfer/maxConnections"), 6).toInt(), 32);
}

QString SettingsDialog::defaultDownloadDirectory()
{
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString selected = QSettings().value(QStringLiteral("downloads/directory"), fallback).toString();
    return selected.isEmpty() ? QDir::homePath() : selected;
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Opciones de OIDM"));
    setMinimumSize(570, 400);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    auto *general = new QWidget(tabs);
    auto *generalLayout = new QVBoxLayout(general);
    auto *text = new QLabel(tr("OIDM usa una configuración de descarga segura por defecto. "
        "Las transferencias del navegador muestran un cuadro de confirmación antes de comenzar. "
        "La interfaz permanece en modo oscuro."), general);
    text->setWordWrap(true);
    generalLayout->addWidget(text);
    auto *themeForm = new QFormLayout();
    m_theme = new QComboBox(general);
    m_theme->setObjectName(QStringLiteral("settingsTheme"));
    m_theme->addItem(tr("Clásico claro (similar al escritorio de IDM)"), QStringLiteral("classic"));
    m_theme->addItem(tr("Oscuro OIDM"), QStringLiteral("dark"));
    const QString savedTheme = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("classic")).toString();
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(savedTheme)));
    themeForm->addRow(tr("Aspecto de la interfaz:"), m_theme);
    generalLayout->addLayout(themeForm);
    generalLayout->addStretch();
    tabs->addTab(general, tr("General"));

    auto *connections = new QWidget(tabs);
    auto *connectionsLayout = new QVBoxLayout(connections);
    auto *connectionForm = new QFormLayout();
    m_connections = new QSpinBox(connections);
    m_connections->setObjectName(QStringLiteral("settingsMaxConnections"));
    m_connections->setRange(1, 32);
    m_connections->setValue(preferredConnections());
    m_connections->setSuffix(tr(" conexiones"));
    connectionForm->addRow(tr("Conexiones por archivo:"), m_connections);
    m_concurrent = new QSpinBox(connections);
    m_concurrent->setObjectName(QStringLiteral("settingsConcurrentDownloads"));
    m_concurrent->setRange(1, 16);
    m_concurrent->setValue(qBound(1, QSettings().value(QStringLiteral("transfer/concurrentDownloads"), 3).toInt(), 16));
    m_concurrent->setSuffix(tr(" archivos"));
    connectionForm->addRow(tr("Descargas simultáneas:"), m_concurrent);
    connectionsLayout->addLayout(connectionForm);
    auto *connectionHint = new QLabel(tr("Valor inicial: 6 conexiones por archivo y 3 archivos a la vez. "
        "OIDM inicia automáticamente los segmentos cuando el servidor admite rangos HTTP. "
        "Si el servidor no admite rangos, se utiliza una conexión; "
        "sin ETag/Last-Modified no se reutilizan fragmentos en otra sesión."), connections);
    connectionHint->setWordWrap(true);
    connectionsLayout->addWidget(connectionHint);
    connectionsLayout->addStretch();
    tabs->addTab(connections, tr("Conexiones"));

    auto *downloads = new QWidget(tabs);
    auto *downloadsLayout = new QVBoxLayout(downloads);
    auto *directoryRow = new QHBoxLayout();
    m_folder = new QLineEdit(defaultDownloadDirectory(), downloads);
    m_folder->setObjectName(QStringLiteral("settingsDownloadFolder"));
    auto *browse = new QPushButton(tr("Examinar…"), downloads);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Carpeta de descargas"), m_folder->text());
        if (!folder.isEmpty()) m_folder->setText(folder);
    });
    directoryRow->addWidget(m_folder, 1);
    directoryRow->addWidget(browse);
    auto *directoryLabel = new QLabel(tr("Carpeta de descargas predeterminada:"), downloads);
    downloadsLayout->addWidget(directoryLabel);
    downloadsLayout->addLayout(directoryRow);
    auto *directoryHint = new QLabel(tr("Se usará para las nuevas descargas enviadas desde Chrome/Edge. "
        "Podrás modificar el destino antes de comenzar cada transferencia."), downloads);
    directoryHint->setWordWrap(true);
    downloadsLayout->addWidget(directoryHint);
    downloadsLayout->addStretch();
    tabs->addTab(downloads, tr("Descargas"));

    auto *browsers = new QWidget(tabs);
    auto *browserLayout = new QVBoxLayout(browsers);
    auto *browserHint = new QLabel(tr("La extensión de Chrome/Edge detecta descargas HTTP/HTTPS y "
        "pide a OIDM preparar la transferencia. Chrome solamente cancela la descarga "
        "original tras recibir la confirmación de OIDM. La instalación y reparación "
        "están disponibles en Ayuda → Instalar / conectar extensión."), browsers);
    browserHint->setWordWrap(true);
    browserLayout->addWidget(browserHint);
    browserLayout->addStretch();
    tabs->addTab(browsers, tr("Navegadores"));

    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::apply()
{
    const QString path = QDir::cleanPath(m_folder->text().trimmed());
    if (path.isEmpty() || path == QStringLiteral(".") || !QDir().mkpath(path)) {
        QMessageBox::warning(this, tr("Carpeta no disponible"),
            tr("Selecciona una carpeta válida donde tengas permisos de escritura."));
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("transfer/maxConnections"), m_connections->value());
    settings.setValue(QStringLiteral("transfer/concurrentDownloads"), m_concurrent->value());
    settings.setValue(QStringLiteral("ui/theme"), m_theme->currentData().toString());
    settings.setValue(QStringLiteral("downloads/directory"), path);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        QMessageBox::critical(this, tr("No se pudo guardar"),
            tr("No fue posible guardar los ajustes. Revisa los permisos de usuario."));
        return;
    }
    accept();
}

} // namespace oidm
