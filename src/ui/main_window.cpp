#include "ui/main_window.h"

#include "core/download_repository.h"
#include "ui/add_download_dialog.h"
#include "ui/progress_delegate.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

namespace oidm {

MainWindow::MainWindow(DownloadRepository *repository, QWidget *parent)
    : QMainWindow(parent),
      m_repository(repository),
      m_manager(new DownloadManager(repository, this)),
      m_model(new DownloadTableModel(this))
{
    setWindowTitle(tr("OIDM — Open Internet Download Manager"));
    resize(1180, 740);
    createActions();
    createMenusAndToolbar();
    createCentralLayout();
    applyStyle();

    if (m_repository != nullptr) {
        m_model->setDownloads(m_repository->all());
    }
    connect(m_manager, &DownloadManager::downloadUpdated,
            this, &MainWindow::onDownloadUpdated);
    connect(m_manager, &DownloadManager::downloadFinished, this, [this](const Download &download) {
        statusBar()->showMessage(tr("%1: %2").arg(download.fileName, statusText(download.status)), 6000);
    });
    statusBar()->showMessage(tr("Listo"));
}

void MainWindow::createActions()
{
    m_addAction = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Agregar URL"), this);
    m_addAction->setShortcut(QKeySequence::New);
    connect(m_addAction, &QAction::triggered, this, &MainWindow::addDownload);

    m_startAction = new QAction(style()->standardIcon(QStyle::SP_MediaPlay), tr("Iniciar"), this);
    m_startAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_startAction, &QAction::triggered, this, &MainWindow::startSelected);

    m_startQueueAction = new QAction(style()->standardIcon(QStyle::SP_MediaSeekForward), tr("Iniciar cola"), this);
    connect(m_startQueueAction, &QAction::triggered, this, &MainWindow::startQueue);

    m_pauseAction = new QAction(style()->standardIcon(QStyle::SP_MediaPause), tr("Pausar"), this);
    m_pauseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pauseSelected);

    m_cancelAction = new QAction(style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancelar"), this);
    connect(m_cancelAction, &QAction::triggered, this, &MainWindow::cancelSelected);

    m_removeAction = new QAction(style()->standardIcon(QStyle::SP_TrashIcon), tr("Eliminar"), this);
    m_removeAction->setShortcut(QKeySequence::Delete);
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
}

void MainWindow::createMenusAndToolbar()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("Archivo"));
    fileMenu->addAction(m_addAction);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Salir"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *downloadsMenu = menuBar()->addMenu(tr("Descargas"));
    downloadsMenu->addAction(m_addAction);
    downloadsMenu->addAction(m_startAction);
    downloadsMenu->addAction(m_startQueueAction);
    downloadsMenu->addAction(m_pauseAction);
    downloadsMenu->addAction(m_cancelAction);
    downloadsMenu->addSeparator();
    downloadsMenu->addAction(m_removeAction);

    menuBar()->addMenu(tr("Ver"));
    menuBar()->addMenu(tr("Colas"));
    menuBar()->addMenu(tr("Ayuda"));

    auto *toolbar = addToolBar(tr("Principal"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_addAction);
    toolbar->addSeparator();
    toolbar->addAction(m_startAction);
    toolbar->addAction(m_startQueueAction);
    toolbar->addAction(m_pauseAction);
    toolbar->addAction(m_cancelAction);
    toolbar->addSeparator();
    toolbar->addAction(m_removeAction);
}

void MainWindow::createCentralLayout()
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(12);

    m_categories = new QListWidget(central);
    m_categories->setObjectName(QStringLiteral("categories"));
    m_categories->setFixedWidth(190);
    m_categories->addItems({
        tr("Todas las descargas"), tr("Sin terminar"), tr("Terminadas"),
        tr("Programas"), tr("Comprimidos"), tr("Documentos"), tr("Música"), tr("Vídeos")
    });
    m_categories->setCurrentRow(0);
    layout->addWidget(m_categories);

    auto *content = new QVBoxLayout();
    auto *heading = new QLabel(tr("Descargas"), central);
    heading->setObjectName(QStringLiteral("sectionHeading"));
    content->addWidget(heading);

    m_table = new QTableView(central);
    m_table->setObjectName(QStringLiteral("downloadsTable"));
    m_table->setModel(m_model);
    m_table->setItemDelegateForColumn(DownloadTableModel::Progress, new ProgressDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Name, QHeaderView::Stretch);
    for (int column = DownloadTableModel::Size; column < DownloadTableModel::ColumnCount; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::startSelected);
    content->addWidget(m_table, 1);

    auto *footer = new QLabel(tr("Motor de descargas C++ / Qt 6 · Aceleración por segmentos en desarrollo"), central);
    footer->setObjectName(QStringLiteral("footerNote"));
    content->addWidget(footer);
    layout->addLayout(content, 1);
    setCentralWidget(central);
}

void MainWindow::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f3f5f8; }"
        "QMenuBar { background: #ffffff; border-bottom: 1px solid #dce1e8; padding: 4px; }"
        "QToolBar { background: #ffffff; border: none; border-bottom: 1px solid #dce1e8; spacing: 8px; padding: 8px; }"
        "QToolButton { padding: 7px 10px; border-radius: 5px; }"
        "QToolButton:hover { background: #e9eef6; }"
        "QListWidget#categories { background: #ffffff; border: 1px solid #e0e5ec; border-radius: 7px; padding: 6px; }"
        "QListWidget#categories::item { padding: 10px 8px; border-radius: 4px; }"
        "QListWidget#categories::item:selected { background: #dce9fa; color: #174a86; }"
        "QLabel#sectionHeading { color: #1e293b; font-size: 20px; font-weight: 600; padding: 2px 0 8px 0; }"
        "QTableView#downloadsTable { background: #ffffff; alternate-background-color: #f8fafc; border: 1px solid #e0e5ec; border-radius: 7px; selection-background-color: #dce9fa; selection-color: #172b4d; }"
        "QHeaderView::section { background: #f5f7fa; color: #526174; border: none; border-bottom: 1px solid #dce1e8; padding: 9px 8px; font-weight: 600; }"
        "QLabel#footerNote { color: #7b8796; padding: 4px; }"
        "QStatusBar { background: #ffffff; border-top: 1px solid #dce1e8; color: #637083; }"
    ));
}

QString MainWindow::selectedDownloadId() const
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid()) return {};
    return current.data(Qt::UserRole).toString();
}

bool MainWindow::enqueueFromBrowser(const QString &urlText, const QString &suggestedName,
                                    QString *id, QString *error)
{
    const QUrl url(urlText);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        if (error) *error = tr("Enlace HTTP/HTTPS no válido o con credenciales incrustadas.");
        return false;
    }
    QString name = suggestedName.isEmpty() ? url.fileName(QUrl::FullyDecoded) : suggestedName;
    if (name.size() > 160) name.truncate(160);
    name.replace(QRegularExpression(QStringLiteral("[\\/:*?\"<>|\x00-\x1F]")), QStringLiteral("_"));
    name = name.trimmed().trimmed();
    while (name.endsWith(QLatin1Char('.'))) name.chop(1);
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        name = QStringLiteral("download.bin");
    }
    const QString basename = name.section(QLatin1Char('.'), 0, 0).toUpper();
    const QStringList reserved{QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
        QStringLiteral("NUL"), QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("LPT1")};
    if (reserved.contains(basename)) name.prepend(QLatin1Char('_'));

    QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (folder.isEmpty()) folder = QDir::homePath();
    if (!QDir().mkpath(folder)) {
        if (error) *error = tr("No se pudo crear la carpeta de descargas.");
        return false;
    }
    const QString stem = QFileInfo(name).completeBaseName();
    const QString suffix = QFileInfo(name).completeSuffix();
    QString destination;
    const auto records = m_repository ? m_repository->all() : QVector<Download>{};
    for (int i = 0; i < 10000; ++i) {
        const QString candidate = i == 0 ? name
            : (stem + QStringLiteral(" (%1)").arg(i)
                + (suffix.isEmpty() ? QString{} : QLatin1Char('.') + suffix));
        const QString path = QDir(folder).filePath(candidate);
        bool used = QFileInfo::exists(path)
            || QFileInfo::exists(path + QStringLiteral(".oidm-part"))
            || QFileInfo::exists(path + QStringLiteral(".oidm-parts"));
        for (const auto &existing : records) {
            if (existing.destination == path) { used = true; break; }
        }
        if (!used) { destination = path; break; }
    }
    if (destination.isEmpty()) {
        if (error) *error = tr("No se encontró un nombre de archivo disponible.");
        return false;
    }

    Download download;
    download.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    download.url = url.toString(QUrl::FullyEncoded);
    download.destination = destination;
    download.fileName = QFileInfo(destination).fileName();
    download.status = DownloadStatus::Queued;
    download.createdAt = QDateTime::currentDateTimeUtc();
    download.updatedAt = download.createdAt;
    if (!m_repository || !m_repository->insert(download, error)) return false;
    m_model->upsert(download);
    if (id) *id = download.id;
    QString ignored;
    if (!m_manager->start(download.id, &ignored)) {
        statusBar()->showMessage(tr("Descarga en cola: %1").arg(ignored), 4000);
    }
    return true;
}

void MainWindow::addDownload()
{
    AddDownloadDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const Download download = dialog.download();
    if (QFileInfo::exists(download.destination)) {
        QMessageBox::warning(this, tr("El archivo ya existe"),
                             tr("El destino seleccionado ya existe. Elige otro nombre para proteger el archivo actual."));
        return;
    }
    QString error;
    if (m_repository == nullptr || !m_repository->insert(download, &error)) {
        QMessageBox::critical(this, tr("No se pudo agregar la descarga"), error);
        return;
    }
    m_model->upsert(download);
    statusBar()->showMessage(tr("Descarga agregada a la cola"), 4000);
}

void MainWindow::startSelected()
{
    const QString id = selectedDownloadId();
    if (id.isEmpty()) return;
    QString error;
    if (!m_manager->start(id, &error)) statusBar()->showMessage(error, 5000);
}

void MainWindow::startQueue()
{
    m_manager->startQueued();
    statusBar()->showMessage(tr("Cola de descargas iniciada"), 4000);
}

void MainWindow::pauseSelected()
{
    const QString id = selectedDownloadId();
    if (id.isEmpty()) return;
    QString error;
    if (!m_manager->pause(id, &error)) statusBar()->showMessage(error, 5000);
}

void MainWindow::cancelSelected()
{
    const QString id = selectedDownloadId();
    if (id.isEmpty()) return;
    QString error;
    if (!m_manager->cancel(id, &error)) statusBar()->showMessage(error, 5000);
}

void MainWindow::removeSelected()
{
    const QString id = selectedDownloadId();
    if (id.isEmpty()) return;
    QString error;
    if (!m_manager->remove(id, &error)) {
        statusBar()->showMessage(error, 5000);
        return;
    }
    m_model->remove(id);
}

void MainWindow::onDownloadUpdated(const Download &download)
{
    m_model->upsert(download);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_manager->pauseAll();
    QMainWindow::closeEvent(event);
}

} // namespace oidm
