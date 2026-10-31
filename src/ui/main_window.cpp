#include "ui/main_window.h"

#include "core/download_repository.h"
#include "ui/add_download_dialog.h"
#include "ui/progress_delegate.h"
#include "ui/download_progress_dialog.h"
#include "ui/browser_setup_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/download_offer_dialog.h"
#include "integration/browser_extension_id.h"

#include <QAction>
#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QProcess>
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
#include <QTreeWidget>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QTimer>
#include <QIcon>
#include <QApplication>
#include <QSettings>
#include <QSplitter>
#include <QFileIconProvider>
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
namespace {

enum Category { All = 0, Incomplete = 1, Finished = 2, Programs = 3, Compressed = 4,
                Documents = 5, Music = 6, Videos = 7 };

class CategoryFilter final : public QSortFilterProxyModel {
public:
    explicit CategoryFilter(QObject *parent) : QSortFilterProxyModel(parent) {}
    void setCategory(int category) { m_category = category; invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        const auto *model = qobject_cast<const DownloadTableModel *>(sourceModel());
        const Download *download = model == nullptr || parent.isValid() ? nullptr : model->at(row);
        if (!download) return false;
        switch (m_category) {
        case All: return true;
        case Incomplete: return download->status != DownloadStatus::Completed;
        case Finished: return download->status == DownloadStatus::Completed;
        default: break;
        }
        const QString suffix = QFileInfo(download->fileName).suffix().toLower();
        if (m_category == Programs) return QStringList{QStringLiteral("exe"),QStringLiteral("msi"),QStringLiteral("deb"),QStringLiteral("rpm"),QStringLiteral("apk"),QStringLiteral("appimage")}.contains(suffix);
        if (m_category == Compressed) return QStringList{QStringLiteral("zip"),QStringLiteral("rar"),QStringLiteral("7z"),QStringLiteral("tar"),QStringLiteral("gz"),QStringLiteral("bz2")}.contains(suffix);
        if (m_category == Documents) return QStringList{QStringLiteral("pdf"),QStringLiteral("docx"),QStringLiteral("doc"),QStringLiteral("xlsx"),QStringLiteral("txt"),QStringLiteral("pptx")}.contains(suffix);
        if (m_category == Music) return QStringList{QStringLiteral("mp3"),QStringLiteral("flac"),QStringLiteral("wav"),QStringLiteral("ogg"),QStringLiteral("m4a")}.contains(suffix);
        if (m_category == Videos) return QStringList{QStringLiteral("mp4"),QStringLiteral("mkv"),QStringLiteral("avi"),QStringLiteral("webm"),QStringLiteral("mov")}.contains(suffix);
        return true;
    }
private:
    int m_category{All};
};

} // namespace

MainWindow::MainWindow(DownloadRepository *repository, QWidget *parent)
    : QMainWindow(parent),
      m_repository(repository),
      m_manager(new DownloadManager(repository, this)),
      m_model(new DownloadTableModel(this))
{
    setWindowTitle(tr("OIDM — Open Internet Download Manager"));
    resize(1220, 760);
    setMinimumSize(840, 540);
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
        if (download.status == DownloadStatus::Failed && m_details.value(download.id)) {
            m_details.value(download.id)->raise();
        }
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

    m_captureAction = new QAction(tr("Pausar capturas"), this);
    m_captureAction->setObjectName(QStringLiteral("browserCapturePaused"));
    m_captureAction->setCheckable(true);
    m_captureAction->setChecked(QSettings().value(QStringLiteral("browser/capturePaused"), false).toBool());
    connect(m_captureAction, &QAction::triggered, this, &MainWindow::toggleBrowserCapture);

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
    downloadsMenu->addActions({m_addAction, m_startAction, m_startQueueAction, m_pauseAction, m_cancelAction});
    downloadsMenu->addSeparator();
    downloadsMenu->addAction(m_removeAction);
    downloadsMenu->addSeparator();
    downloadsMenu->addAction(m_captureAction);
    downloadsMenu->addAction(tr("Mostrar detalles"), this, &MainWindow::showSelectedDetails);
    QMenu *viewMenu = menuBar()->addMenu(tr("Ver"));
    viewMenu->addAction(tr("Todas las descargas"), this, [this] {
        m_categories->setCurrentItem(m_categories->topLevelItem(0));
    });
    QMenu *toolsMenu = menuBar()->addMenu(tr("Herramientas"));
    toolsMenu->addAction(tr("Iniciar cola"), m_manager, &DownloadManager::startQueued);
    toolsMenu->addAction(m_captureAction);
    QMenu *helpMenu = menuBar()->addMenu(tr("Ayuda"));
    helpMenu->addAction(tr("Instalar / conectar extensión Chrome y Edge..."), this,
                        &MainWindow::openBrowserSetup);
    helpMenu->addAction(tr("Reparar integración Chrome / Edge..."), this,
                        &MainWindow::repairBrowserIntegration);
    toolsMenu->addAction(tr("Opciones / conexiones..."), this, &MainWindow::openSettings);
    helpMenu->addAction(tr("Acerca de OIDM"), this, [this] {
        QMessageBox::about(this, tr("OIDM"), tr("Open Internet Download Manager\n"
            "Versión %1 · ThowiLabs\nC++20 · Qt 6 · Código abierto\n"
            "Interfaz clásica oscura (implementación original).")
            .arg(QCoreApplication::applicationVersion()));
    });

    auto *toolbar = addToolBar(tr("Acciones principales"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setIconSize(QSize(32, 32));
    m_addAction->setText(tr("Agregar URL"));
    m_startAction->setText(tr("Reanudar"));
    m_startQueueAction->setText(tr("Iniciar todo"));
    m_pauseAction->setText(tr("Pausar"));
    m_cancelAction->setText(tr("Detener"));
    m_removeAction->setText(tr("Eliminar"));
    toolbar->addAction(m_addAction);
    toolbar->addAction(m_startAction);
    toolbar->addAction(m_startQueueAction);
    toolbar->addAction(m_pauseAction);
    toolbar->addAction(m_cancelAction);
    toolbar->addAction(m_removeAction);
    toolbar->addSeparator();
    toolbar->addAction(m_captureAction);
    toolbar->addSeparator();
    auto *details = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Detalles"),
                                      this, &MainWindow::showSelectedDetails);
    details->setObjectName(QStringLiteral("detailsAction"));
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Opciones"),
                       this, &MainWindow::openSettings);
}

void MainWindow::createCentralLayout()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainContent"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 4);
    layout->setSpacing(7);

    auto *split = new QSplitter(Qt::Horizontal, central);
    split->setObjectName(QStringLiteral("downloadsSplitter"));
    split->setChildrenCollapsible(false);

    m_categories = new QTreeWidget(split);
    m_categories->setObjectName(QStringLiteral("categories"));
    m_categories->setHeaderLabel(tr("Categorías"));
    m_categories->setRootIsDecorated(true);
    m_categories->setMinimumWidth(165);
    m_categories->setMaximumWidth(320);

    auto *all = new QTreeWidgetItem(m_categories, {tr("Todas las descargas")});
    all->setData(0, Qt::UserRole, All);
    all->setIcon(0, style()->standardIcon(QStyle::SP_DirHomeIcon));
    const auto addSub = [this, all](const QString &label, int category, QStyle::StandardPixmap icon) {
        auto *child = new QTreeWidgetItem(all, {label});
        child->setData(0, Qt::UserRole, category);
        child->setIcon(0, style()->standardIcon(icon));
    };
    addSub(tr("Programas"), Programs, QStyle::SP_ComputerIcon);
    addSub(tr("Comprimidos"), Compressed, QStyle::SP_DriveHDIcon);
    addSub(tr("Documentos"), Documents, QStyle::SP_FileIcon);
    addSub(tr("Música"), Music, QStyle::SP_MediaVolume);
    addSub(tr("Vídeo"), Videos, QStyle::SP_MediaPlay);

    auto *unfinished = new QTreeWidgetItem(m_categories, {tr("Sin terminar")});
    unfinished->setData(0, Qt::UserRole, Incomplete);
    unfinished->setIcon(0, style()->standardIcon(QStyle::SP_BrowserReload));
    auto *finished = new QTreeWidgetItem(m_categories, {tr("Terminadas")});
    finished->setData(0, Qt::UserRole, Finished);
    finished->setIcon(0, style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_categories->expandAll();
    m_categories->setCurrentItem(all);

    auto *panel = new QWidget(split);
    auto *content = new QVBoxLayout(panel);
    content->setContentsMargins(3, 0, 0, 0);
    content->setSpacing(8);
    auto *heading = new QLabel(tr("TODAS LAS DESCARGAS"), panel);
    heading->setObjectName(QStringLiteral("sectionHeading"));
    content->addWidget(heading);

    m_categoryModel = new CategoryFilter(this);
    m_categoryModel->setSourceModel(m_model);
    m_categoryModel->setDynamicSortFilter(true);
    m_table = new QTableView(panel);
    m_table->setObjectName(QStringLiteral("downloadsTable"));
    m_table->setModel(m_categoryModel);
    m_table->setItemDelegateForColumn(DownloadTableModel::Progress, new ProgressDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setSortingEnabled(false);
    m_table->setWordWrap(false);
    m_table->setIconSize(QSize(18,18));
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(36);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Name, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Size, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Status, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Progress, QHeaderView::Fixed);
    m_table->setColumnWidth(DownloadTableModel::Progress, 145);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Speed, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DownloadTableModel::Date, QHeaderView::ResizeToContents);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction(m_startAction);
        menu.addAction(m_pauseAction);
        menu.addAction(m_cancelAction);
        menu.addSeparator();
        menu.addAction(tr("Ver progreso / detalles"), this, &MainWindow::showSelectedDetails);
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });
    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::showSelectedDetails);
    content->addWidget(m_table, 1);
    connect(m_categories, &QTreeWidget::currentItemChanged, this,
        [this, heading](QTreeWidgetItem *item) {
            if (!item) return;
            static_cast<CategoryFilter *>(m_categoryModel)->setCategory(item->data(0, Qt::UserRole).toInt());
            heading->setText(item->text(0).toUpper());
        });
    split->addWidget(m_categories);
    split->addWidget(panel);
    split->setSizes({210, 980});
    layout->addWidget(split, 1);
    auto *footer = new QLabel(tr("●  MOTOR LISTO     ·     C++20 / Qt 6     ·     Descargas HTTP/HTTPS y conexiones segmentadas"), central);
    footer->setObjectName(QStringLiteral("footerNote"));
    layout->addWidget(footer);
    setCentralWidget(central);
}

void MainWindow::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow, QDialog { background: #171b22; color: #e2e8f0; }"
        "QWidget#mainContent { background: #171b22; }"
        "QMenuBar { background: #202630; color: #dce4ed; border-bottom: 1px solid #353e4c; padding: 4px 8px; }"
        "QMenuBar::item { padding: 6px 12px; }"
        "QMenuBar::item:selected, QMenu::item:selected { background: #30435b; }"
        "QMenu { background: #242b36; color: #e2e8f0; border: 1px solid #404b5c; }"
        "QMenu::item { padding: 7px 26px; }"
        "QToolBar#mainToolbar { background: #242b36; color: #f1f5f9; border: none; border-bottom: 1px solid #405064; spacing: 6px; padding: 8px 12px; }"
        "QToolBar::separator { background: #405064; width: 1px; margin: 8px 8px; }"
        "QToolButton { color: #e2e8f0; background: transparent; padding: 7px 8px; border: 1px solid transparent; border-radius: 5px; min-width: 65px; }"
        "QToolButton:hover { background: #344359; border: 1px solid #526880; }"
        "QToolButton:pressed { background: #274462; }"
        "QTreeWidget#categories { color: #cdd8e7; background: #1e242e; border: 1px solid #343e4d; border-radius: 4px; outline: 0; }"
        "QTreeWidget#categories::item { padding: 7px 5px; min-height: 22px; border-radius: 3px; }"
        "QTreeWidget#categories::item:selected { background: #265880; color: #ffffff; }"
        "QTreeWidget#categories::item:hover { background: #303d4c; }"
        "QHeaderView::section { color: #bdc9d8; background: #27303d; border: none; border-right: 1px solid #343e4d; border-bottom: 1px solid #455366; padding: 9px 9px; font-weight: 600; }"
        "QTableView#downloadsTable { color: #e2e8f0; background: #1c222c; alternate-background-color: #222a36; gridline-color: #35404d; border: 1px solid #343e4d; selection-background-color: #285b84; selection-color: #ffffff; outline: 0; }"
        "QTableView#downloadsTable::item { padding: 5px 7px; }"
        "QTableView#downloadsTable::item:hover { background: #2c3d51; }"
        "QLabel#sectionHeading { color: #a9c4e5; font-size: 13px; font-weight: 700; padding: 10px 5px 4px; }"
        "QLabel#footerNote { color: #8b99ab; background: #202630; padding: 9px 10px; border: 1px solid #333e4e; }"
        "QStatusBar { color: #b4c5d6; background: #202630; border-top: 1px solid #3d4b5c; padding: 4px; }"
        "QSplitter::handle { background: #303a48; width: 5px; }"
        "QScrollBar { background: #202630; }"
        "QScrollBar::handle { background: #4a596c; border-radius: 4px; min-height: 22px; min-width: 12px; }"
        "QPushButton { background: #2b3a4b; color: #edf2f9; border: 1px solid #495d72; border-radius: 5px; padding: 8px 13px; }"
        "QPushButton:hover { background: #365c7b; }"
        "QLineEdit, QTabWidget::pane, QTableWidget { color: #e2e8f0; background: #202733; border: 1px solid #415062; }"
        "QProgressBar { color: white; background: #1b2834; border: 1px solid #496079; border-radius: 4px; text-align: center; }"
        "QProgressBar::chunk { background: #16a085; border-radius: 3px; }"
    ));
}

void MainWindow::selectAndRevealDownload(const QString &id)
{
    m_categories->setCurrentItem(m_categories->topLevelItem(0));
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const Download *download = m_model->at(row);
        if (!download || download->id != id) continue;
        const QModelIndex visible = m_categoryModel->mapFromSource(m_model->index(row, 0));
        if (visible.isValid()) {
            m_table->selectRow(visible.row());
            m_table->scrollTo(visible, QAbstractItemView::PositionAtCenter);
        }
        break;
    }
    if (isMinimized()) showNormal();
    if (!isVisible()) show();
    raise();
    activateWindow();
}

void MainWindow::showDetails(const Download &download)
{
    QPointer<DownloadProgressDialog> dialog = m_details.value(download.id);
    if (!dialog) {
        dialog = new DownloadProgressDialog(download, this);
        m_details.insert(download.id, dialog);
        connect(dialog, &DownloadProgressDialog::pauseRequested, this, [this](const QString &id) {
            QString error;
            if (!m_manager->pause(id, &error)) statusBar()->showMessage(error, 6000);
        });
        connect(dialog, &DownloadProgressDialog::resumeRequested, this, [this](const QString &id) {
            QString error;
            if (!m_manager->start(id, &error)) statusBar()->showMessage(error, 6000);
        });
        connect(dialog, &DownloadProgressDialog::cancelRequested, this, [this](const QString &id) {
            QString error;
            if (!m_manager->cancel(id, &error)) statusBar()->showMessage(error, 6000);
        });
        // QPointer automatically becomes null when the modeless dialog closes.
        // Do not mutate m_details from QObject::destroyed: Qt destroys child
        // dialogs after MainWindow's own members have been destroyed.
    } else {
        dialog->updateDownload(download);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::toggleBrowserCapture()
{
    QSettings settings;
    settings.sync();
    const bool nextPaused = !settings.value(QStringLiteral("browser/capturePaused"), false).toBool();
    settings.setValue(QStringLiteral("browser/capturePaused"), nextPaused);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        m_captureAction->setChecked(!nextPaused);
        statusBar()->showMessage(tr("No se pudo guardar el estado de capturas."), 9000);
        return;
    }
    m_captureAction->setChecked(nextPaused);
    statusBar()->showMessage(nextPaused
        ? tr("Captura PAUSADA: Chrome/Edge descarga normalmente.")
        : tr("Captura ACTIVA: OIDM recibirá nuevas descargas."), 9000);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::showOffer(const Download &download)
{
    if (m_offers.value(download.id)) {
        m_offers.value(download.id)->raise();
        return;
    }
    auto *dialog = new DownloadOfferDialog(download, this);
    m_offers.insert(download.id, dialog);
    connect(dialog, &DownloadOfferDialog::startRequested, this,
        [this, id = download.id, dialog](const QString &destination) {
            if (beginOfferedDownload(id, destination)) dialog->accept();
        });
    connect(dialog, &QDialog::rejected, this, [this, id = download.id] {
        QString error;
        if (m_manager->remove(id, &error)) m_model->remove(id);
        else statusBar()->showMessage(tr("No se pudo cancelar: %1").arg(error), 8000);
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool MainWindow::browserTransferReleased(const QString &id)
{
    if (!m_repository) return false;
    const QVector<Download> records = m_repository->all();
    for (const Download &item : records) {
        if (item.id != id || item.status != DownloadStatus::Queued) continue;
        selectAndRevealDownload(id);
        showOffer(item);
        return true;
    }
    return false;
}

bool MainWindow::browserTransferAbandoned(const QString &id)
{
    if (m_offers.value(id)) return false;
    if (!m_repository) return false;
    const QVector<Download> records = m_repository->all();
    for (const Download &item : records) {
        if (item.id != id || item.status != DownloadStatus::Queued) continue;
        QString error;
        if (!m_manager->remove(id, &error)) return false;
        m_model->remove(id);
        statusBar()->showMessage(tr("Descarga devuelta al navegador."), 5000);
        return true;
    }
    return false;
}

bool MainWindow::beginOfferedDownload(const QString &id, const QString &destination)
{
    QString error;
    const QFileInfo pathInfo(destination);
    if (destination.isEmpty() || pathInfo.fileName().isEmpty()
        || QFileInfo::exists(destination) || pathInfo.isSymLink()
        || QFileInfo::exists(destination + QStringLiteral(".oidm-part"))
        || QFileInfo::exists(destination + QStringLiteral(".oidm-parts"))
        || !QDir().mkpath(pathInfo.absolutePath())) {
        QMessageBox::warning(this, tr("Destino no disponible"),
            tr("Elige un destino válido y libre. La descarga quedará en cola para corregirlo."));
        return false;
    }
    auto downloads = m_repository->all(&error);
    if (!error.isEmpty()) {
        statusBar()->showMessage(error, 8000);
        return false;
    }
    for (const Download &item : downloads) {
        if (item.id != id && item.destination.compare(destination, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, tr("Destino ocupado"),
                tr("Otra descarga ya utiliza ese nombre. Elige otro."));
            return false;
        }
    }
    if (!m_repository->updateQueuedDestination(id, destination, &error)) {
        statusBar()->showMessage(error, 8000);
        return false;
    }
    for (Download &item : downloads) {
        if (item.id != id) continue;
        item.destination = destination;
        item.fileName = QFileInfo(destination).fileName();
        m_model->upsert(item);
        selectAndRevealDownload(id);
        showDetails(item);
        break;
    }
    if (!m_manager->start(id, &error)) {
        statusBar()->showMessage(error, 8000);
        return false;
    }
    return true;
}

void MainWindow::showSelectedDetails()
{
    const QString id = selectedDownloadId();
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (const Download *item = m_model->at(i); item && item->id == id) {
            showDetails(*item);
            return;
        }
    }
}

QString MainWindow::selectedDownloadId() const
{
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid()) return {};
    return current.data(Qt::UserRole).toString();
}

bool MainWindow::enqueueFromBrowser(const QString &urlText, const QString &suggestedName,
                                    const QString &referrer, qint64 totalBytes, bool browserOwned, QString *id, QString *error)
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

    QString folder = SettingsDialog::defaultDownloadDirectory();
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
    const QUrl page(referrer);
    if (page.isValid() && !page.host().isEmpty() && page.userInfo().isEmpty()
        && (page.scheme() == QStringLiteral("http") || page.scheme() == QStringLiteral("https"))) {
        download.referrer = page.toString(QUrl::FullyEncoded);
    }
    download.destination = destination;
    download.fileName = QFileInfo(destination).fileName();
    download.status = DownloadStatus::Queued;
    download.totalBytes = totalBytes > 0 ? totalBytes : -1;
    download.createdAt = QDateTime::currentDateTimeUtc();
    download.updatedAt = download.createdAt;
    if (!m_repository || !m_repository->insert(download, error)) return false;
    m_model->upsert(download);
    selectAndRevealDownload(download.id);
    if (!browserOwned) showOffer(download);
    if (id) *id = download.id;
    statusBar()->showMessage(tr("Descarga preparada: confirma Iniciar o Cancelar."), 8000);
    return true;
}

void MainWindow::openBrowserSetup()
{
    BrowserSetupDialog dialog(this);
    dialog.exec();
}

void MainWindow::repairBrowserIntegration()
{
    bool confirmed = false;
    const QString extensionId = QInputDialog::getText(
        this, tr("Reparar integración del navegador"),
        tr("1. Abre chrome://extensions o edge://extensions.\n"
           "2. Activa Modo de desarrollador y copia el ID de la extensión OIDM.\n"
           "3. Confirma o reemplaza este ID:"),
        QLineEdit::Normal, QStringLiteral(OIDM_CHROMIUM_EXTENSION_ID), &confirmed).trimmed();
    if (!confirmed) return;
    static const QRegularExpression idPattern(QStringLiteral("^[a-p]{32}$"));
    if (!idPattern.match(extensionId).hasMatch()) {
        QMessageBox::warning(this, tr("ID inválido"),
            tr("El ID de Chrome o Edge debe contener exactamente 32 letras de a a p."));
        return;
    }
    const QString host = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("oidm-native-host.exe")
#else
        QStringLiteral("oidm-native-host")
#endif
    );
    if (!QFileInfo(host).isFile()) {
        QMessageBox::warning(this, tr("Host no encontrado"),
            tr("No se encuentra oidm-native-host al lado de OIDM.\n"
               "Extrae el ZIP completo e inicia OIDM desde la misma carpeta."));
        return;
    }
    QProcess registration;
    registration.setProgram(host);
    registration.setArguments({QStringLiteral("--register-chromium"), extensionId});
    registration.start();
    if (!registration.waitForStarted(2000) || !registration.waitForFinished(4000)
        || registration.exitStatus() != QProcess::NormalExit || registration.exitCode() != 0) {
        QMessageBox::critical(this, tr("No se pudo registrar la extensión"),
            tr("Falló el registro nativo de Chrome/Edge.\n"
               "Usa CONFIGURAR-INTEGRACION.cmd para ver el diagnóstico."));
        return;
    }
    QProcess diagnostic;
    diagnostic.start(host, {QStringLiteral("--diagnose-chromium"), extensionId});
    if (!diagnostic.waitForFinished(4000) || diagnostic.exitCode() != 0) {
        QMessageBox::warning(this, tr("Integración incompleta"),
            tr("Se creó el registro, pero no pasó la comprobación.\n"
               "Ejecuta CONFIGURAR-INTEGRACION.cmd para ver el detalle."));
        return;
    }
    QMessageBox::information(this, tr("Integración registrada"),
        tr("OIDM registró el host para el ID %1.\n\n"
           "Recarga la extensión en chrome://extensions o edge://extensions, "
           "y haz clic en su icono para comprobar la conexión.").arg(extensionId));
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
    selectAndRevealDownload(download.id);
    showOffer(download);
    statusBar()->showMessage(tr("Descarga agregada. Confirma Iniciar o Cancelar."), 4000);
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
    if (QPointer<DownloadProgressDialog> dialog = m_details.value(download.id)) {
        dialog->updateDownload(download);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_manager->pauseAll();
    QMainWindow::closeEvent(event);
}

} // namespace oidm
