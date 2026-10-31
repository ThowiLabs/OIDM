#pragma once

#include "core/download_manager.h"
#include "ui/download_table_model.h"

#include <QMainWindow>
#include <QPointer>
#include <QHash>

class QCloseEvent;
class QTableView;
class QTreeWidget;
class QSortFilterProxyModel;
class QAction;

namespace oidm {

class DownloadRepository;
class DownloadProgressDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(DownloadRepository *repository, QWidget *parent = nullptr);
    [[nodiscard]] bool enqueueFromBrowser(const QString &url, const QString &suggestedName, const QString &referrer,
                                          QString *id, QString *error);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addDownload();
    void repairBrowserIntegration();
    void showSelectedDetails();
    void startSelected();
    void startQueue();
    void pauseSelected();
    void cancelSelected();
    void removeSelected();
    void onDownloadUpdated(const oidm::Download &download);

private:
    [[nodiscard]] QString selectedDownloadId() const;
    void selectAndRevealDownload(const QString &id);
    void showDetails(const Download &download);
    void createActions();
    void createMenusAndToolbar();
    void createCentralLayout();
    void applyStyle();

    DownloadRepository *m_repository = nullptr;
    DownloadManager *m_manager = nullptr;
    DownloadTableModel *m_model = nullptr;
    QSortFilterProxyModel *m_categoryModel = nullptr;
    QHash<QString, QPointer<DownloadProgressDialog>> m_details;
    QTableView *m_table = nullptr;
    QTreeWidget *m_categories = nullptr;
    QAction *m_addAction = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_startQueueAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_cancelAction = nullptr;
    QAction *m_removeAction = nullptr;
};

} // namespace oidm
