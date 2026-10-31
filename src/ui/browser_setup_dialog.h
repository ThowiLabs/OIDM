#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

namespace oidm {

// Browser extensions are installed by Chrome/Edge with user consent.
// This assistant automates the trusted local setup without changing browser
// preferences, policies or unrelated extensions.
class BrowserSetupDialog final : public QDialog {
    Q_OBJECT
public:
    explicit BrowserSetupDialog(QWidget *parent = nullptr);

private:
    void prepareIntegration();
    void openBrowser(const QString &browser);
    void showExtensionFolder();
    void copyExtensionPath();

    QString m_extensionDirectory;
    QLabel *m_status{};
    QPushButton *m_chromeButton{};
    QPushButton *m_edgeButton{};
};

} // namespace oidm
