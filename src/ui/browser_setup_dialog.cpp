#include "ui/browser_setup_dialog.h"

#include "integration/browser_extension_id.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QUrl>
#ifdef Q_OS_WIN
#  include <QSettings>
#  include <windows.h>
#endif

namespace oidm {
namespace {
QString browserExecutable(const QString &browser)
{
#ifdef Q_OS_WIN
    const QString exe = browser == QStringLiteral("Chrome")
        ? QStringLiteral("chrome.exe") : QStringLiteral("msedge.exe");
    const QString key = QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\") + exe;
    const QString registered = QSettings(key, QSettings::NativeFormat).value(QStringLiteral(".")).toString();
    if (QFileInfo(registered).isFile()) return registered;
    const QStringList roots{
        qEnvironmentVariable("PROGRAMFILES"),
        qEnvironmentVariable("PROGRAMFILES(X86)"),
        qEnvironmentVariable("LOCALAPPDATA")
    };
    const QString relative = browser == QStringLiteral("Chrome")
        ? QStringLiteral("Google/Chrome/Application/chrome.exe")
        : QStringLiteral("Microsoft/Edge/Application/msedge.exe");
    for (const QString &root : roots) {
        if (root.isEmpty()) continue;
        const QString candidate = QDir(root).filePath(relative);
        if (QFileInfo(candidate).isFile()) return candidate;
    }
    return QStandardPaths::findExecutable(exe);
#else
    const QStringList names = browser == QStringLiteral("Chrome")
        ? QStringList{QStringLiteral("google-chrome"), QStringLiteral("chromium"), QStringLiteral("chromium-browser")}
        : QStringList{QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge-stable")};
    for (const QString &name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) return found;
    }
    return {};
#endif
}

bool runHostCommand(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start();
    return process.waitForStarted(1500) && process.waitForFinished(4000)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace

BrowserSetupDialog::BrowserSetupDialog(QWidget *parent)
    : QDialog(parent),
      m_extensionDirectory(QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("extension/chromium")))
{
    setWindowTitle(tr("Conectar OIDM con Chrome y Edge"));
    setMinimumWidth(580);
    resize(620, 520);
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 22, 22, 18);
    layout->setSpacing(14);

    auto *title = new QLabel(tr("Integración con tu navegador"), this);
    title->setObjectName(QStringLiteral("browserSetupTitle"));
    title->setStyleSheet(QStringLiteral("font-size: 21px; font-weight: 700; color: #e8f2ff;"));
    layout->addWidget(title);

    auto *intro = new QLabel(tr("OIDM prepara la integración sin permisos de administrador ni comandos. "
        "Chrome y Edge requieren que tú autorices la extensión una vez; "
        "no es posible instalarla de manera oculta."), this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: #aebed0;"));
    layout->addWidget(intro);

    const auto addStep = [this, layout](const QString &heading, const QString &description) {
        auto *label = new QLabel(heading, this);
        label->setStyleSheet(QStringLiteral("color: #7dcbff; font-size: 14px; font-weight: 600;"));
        layout->addWidget(label);
        auto *details = new QLabel(description, this);
        details->setWordWrap(true);
        details->setStyleSheet(QStringLiteral("color: #d1dde9;"));
        layout->addWidget(details);
    };

    addStep(tr("1  Preparar conexión de OIDM"),
        tr("Registra el host nativo y autoriza el ID de la extensión incluida en OIDM."));
    auto *prepare = new QPushButton(tr("Preparar y comprobar conexión"), this);
    prepare->setObjectName(QStringLiteral("prepareBrowserIntegration"));
    connect(prepare, &QPushButton::clicked, this, &BrowserSetupDialog::prepareIntegration);
    layout->addWidget(prepare);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("browserSetupStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    addStep(tr("2  Activar extensión en Chrome / Edge"),
        tr("Elige tu navegador. En su página de extensiones activa Modo de desarrollador "
           "y pulsa Cargar descomprimida. Después selecciona la carpeta de OIDM."));
    auto *browserRow = new QHBoxLayout();
    m_chromeButton = new QPushButton(tr("Abrir Chrome"), this);
    m_edgeButton = new QPushButton(tr("Abrir Edge"), this);
    m_chromeButton->setObjectName(QStringLiteral("setupChromeButton"));
    m_edgeButton->setObjectName(QStringLiteral("setupEdgeButton"));
    m_chromeButton->setEnabled(!browserExecutable(QStringLiteral("Chrome")).isEmpty());
    m_edgeButton->setEnabled(!browserExecutable(QStringLiteral("Edge")).isEmpty());
    connect(m_chromeButton, &QPushButton::clicked, this, [this] { openBrowser(QStringLiteral("Chrome")); });
    connect(m_edgeButton, &QPushButton::clicked, this, [this] { openBrowser(QStringLiteral("Edge")); });
    browserRow->addWidget(m_chromeButton);
    browserRow->addWidget(m_edgeButton);
    layout->addLayout(browserRow);

    auto *folderRow = new QHBoxLayout();
    auto *folderButton = new QPushButton(tr("Mostrar carpeta de extensión"), this);
    auto *copyButton = new QPushButton(tr("Copiar ruta"), this);
    folderButton->setObjectName(QStringLiteral("setupExtensionFolder"));
    connect(folderButton, &QPushButton::clicked, this, &BrowserSetupDialog::showExtensionFolder);
    connect(copyButton, &QPushButton::clicked, this, &BrowserSetupDialog::copyExtensionPath);
    folderRow->addWidget(folderButton);
    folderRow->addWidget(copyButton);
    layout->addLayout(folderRow);

    addStep(tr("3  Comprobar que funciona"),
        tr("Recarga la extensión en el navegador y haz clic en su icono. "
           "Debería decir OIDM conectado. Después prueba una descarga normal."));

    layout->addStretch();
    auto *closeButton = new QPushButton(tr("Terminar"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeButton, 0, Qt::AlignRight);

    if (!QFileInfo(QDir(m_extensionDirectory).filePath(QStringLiteral("manifest.json"))).isFile()) {
        m_status->setText(tr("No se encontró la extensión. Extrae o instala el paquete completo de OIDM."));
        m_status->setStyleSheet(QStringLiteral("color: #ff9999;"));
        prepare->setEnabled(false);
    } else {
        m_status->setText(tr("Extensión localizada. Lista para preparar la conexión."));
        m_status->setStyleSheet(QStringLiteral("color: #adccdf;"));
    }
}

void BrowserSetupDialog::prepareIntegration()
{
    const QString host = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("oidm-native-host.exe")
#else
        QStringLiteral("oidm-native-host")
#endif
    );
    if (!QFileInfo(host).isFile()) {
        m_status->setText(tr("No se encuentra el host nativo. Vuelve a instalar el paquete completo."));
        m_status->setStyleSheet(QStringLiteral("color: #ff9999;"));
        return;
    }
    const QString id = QStringLiteral(OIDM_CHROMIUM_EXTENSION_ID);
    if (!runHostCommand(host, {QStringLiteral("--register-chromium"), id})
        || !runHostCommand(host, {QStringLiteral("--diagnose-chromium"), id})) {
        m_status->setText(tr("No se pudo completar el registro del host. Comprueba que OIDM se inició "
            "con tu usuario habitual y que tienes permiso para modificar sus datos."));
        m_status->setStyleSheet(QStringLiteral("color: #ff9999;"));
        return;
    }
    m_status->setText(tr("Conexión nativa preparada correctamente. Ahora solo falta cargar y activar la extensión en Chrome o Edge."));
    m_status->setStyleSheet(QStringLiteral("color: #73e2b2;"));
}

void BrowserSetupDialog::openBrowser(const QString &browser)
{
    const QString executable = browserExecutable(browser);
    if (executable.isEmpty()) {
        QMessageBox::information(this, tr("Navegador no detectado"),
            tr("No se encontró %1. Puedes abrirlo manualmente y entrar en su página de extensiones.").arg(browser));
        return;
    }
    const QString page = browser == QStringLiteral("Chrome")
        ? QStringLiteral("chrome://extensions") : QStringLiteral("edge://extensions");
    if (!QProcess::startDetached(executable, {page})) {
        QMessageBox::warning(this, tr("No se pudo abrir el navegador"),
            tr("Abre %1 manualmente y entra en %2.").arg(browser, page));
    }
    copyExtensionPath();
}

void BrowserSetupDialog::showExtensionFolder()
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
        {QDir::toNativeSeparators(m_extensionDirectory)});
#else
    (void)QDesktopServices::openUrl(QUrl::fromLocalFile(m_extensionDirectory));
#endif
    copyExtensionPath();
}

void BrowserSetupDialog::copyExtensionPath()
{
    QApplication::clipboard()->setText(QDir::toNativeSeparators(m_extensionDirectory));
    m_status->setText(tr("Ruta de la extensión copiada al portapapeles. Pégala en el selector de carpeta del navegador."));
}

} // namespace oidm
