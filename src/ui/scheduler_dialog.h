#pragma once
#include <QDialog>

class QCheckBox;
class QDateTimeEdit;

namespace oidm {
class SchedulerDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SchedulerDialog(QWidget *parent = nullptr);
private:
    void save();
    QCheckBox *m_enabled{};
    QDateTimeEdit *m_start{};
    QCheckBox *m_hasStop{};
    QDateTimeEdit *m_stop{};
    QCheckBox *m_pauseAtStop{};
};
} // namespace oidm
