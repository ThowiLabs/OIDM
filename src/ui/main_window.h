#pragma once

#include "core/download_manager.h"
#include "ui/download_table_model.h"

#include <QMainWindow>

class QCloseEvent;
class QTableView;
class QListWidget;
class QAction;

namespace oidm {

class DownloadRepository;

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
    void startSelected();
    void startQueue();
    void pauseSelected();
    void cancelSelected();
    void removeSelected();
    void onDownloadUpdated(const oidm::Download &download);

private:
    [[nodiscard]] QString selectedDownloadId() const;
    void createActions();
    void createMenusAndToolbar();
    void createCentralLayout();
    void applyStyle();

    DownloadRepository *m_repository = nullptr;
    DownloadManager *m_manager = nullptr;
    DownloadTableModel *m_model = nullptr;
    QTableView *m_table = nullptr;
    QListWidget *m_categories = nullptr;
    QAction *m_addAction = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_startQueueAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_cancelAction = nullptr;
    QAction *m_removeAction = nullptr;
};

} // namespace oidm
