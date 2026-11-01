#include "ui/scheduler_dialog.h"
#include <QCheckBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>

namespace oidm {
SchedulerDialog::SchedulerDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("schedulerDialog"));
    setWindowTitle(tr("Programador de descargas — OIDM"));
    setMinimumWidth(510);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(17, 15, 17, 12);
    layout->setSpacing(11);
    QSettings settings;
    m_enabled = new QCheckBox(tr("Iniciar automáticamente la cola en la fecha indicada"), this);
    m_enabled->setObjectName(QStringLiteral("scheduleEnabled"));
    m_enabled->setChecked(settings.value(QStringLiteral("scheduler/enabled"), false).toBool());
    layout->addWidget(m_enabled);
    auto *form = new QFormLayout();
    m_start = new QDateTimeEdit(this);
    m_start->setObjectName(QStringLiteral("scheduleStart"));
    m_start->setCalendarPopup(true);
    m_start->setDisplayFormat(QStringLiteral("dd/MM/yyyy HH:mm"));
    m_start->setDateTime(settings.value(QStringLiteral("scheduler/start"),
        QDateTime::currentDateTime().addSecs(3600)).toDateTime());
    form->addRow(tr("Iniciar cola:"), m_start);
    layout->addLayout(form);
    m_hasStop = new QCheckBox(tr("Detener automáticamente la cola a la hora siguiente"), this);
    m_hasStop->setObjectName(QStringLiteral("scheduleHasStop"));
    m_hasStop->setChecked(settings.value(QStringLiteral("scheduler/hasStop"), false).toBool());
    layout->addWidget(m_hasStop);
    auto *stopForm = new QFormLayout();
    m_stop = new QDateTimeEdit(this);
    m_stop->setObjectName(QStringLiteral("scheduleStop"));
    m_stop->setDisplayFormat(QStringLiteral("dd/MM/yyyy HH:mm"));
    m_stop->setCalendarPopup(true);
    m_stop->setDateTime(settings.value(QStringLiteral("scheduler/stop"),
        m_start->dateTime().addSecs(3600)).toDateTime());
    stopForm->addRow(tr("Detener cola:"), m_stop);
    layout->addLayout(stopForm);
    m_pauseAtStop = new QCheckBox(tr("También pausar las transferencias activas al detener la cola"), this);
    m_pauseAtStop->setObjectName(QStringLiteral("schedulePauseAtStop"));
    m_pauseAtStop->setChecked(settings.value(QStringLiteral("scheduler/pauseAtStop"), false).toBool());
    layout->addWidget(m_pauseAtStop);
    const auto update = [this] {
        m_start->setEnabled(m_enabled->isChecked());
        m_hasStop->setEnabled(m_enabled->isChecked());
        m_stop->setEnabled(m_enabled->isChecked() && m_hasStop->isChecked());
        m_pauseAtStop->setEnabled(m_enabled->isChecked() && m_hasStop->isChecked());
    };
    connect(m_enabled, &QCheckBox::toggled, this, update);
    connect(m_hasStop, &QCheckBox::toggled, this, update);
    update();
    auto *description = new QLabel(tr("El programador se ejecuta mientras OIDM está abierto. "
        "Las descargas recibidas desde Chrome que no hayas confirmado nunca se iniciarán por horario. "
        "No se apagará el equipo ni se cerrará ninguna aplicación."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SchedulerDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);
}

void SchedulerDialog::save()
{
    if (m_enabled->isChecked() && m_hasStop->isChecked()
        && m_stop->dateTime() <= m_start->dateTime()) {
        QMessageBox::warning(this, tr("Horario incorrecto"),
            tr("La hora de detener debe ser posterior a la de inicio."));
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("scheduler/enabled"), m_enabled->isChecked());
    settings.setValue(QStringLiteral("scheduler/start"), m_start->dateTime());
    settings.setValue(QStringLiteral("scheduler/hasStop"), m_hasStop->isChecked());
    settings.setValue(QStringLiteral("scheduler/stop"), m_stop->dateTime());
    settings.setValue(QStringLiteral("scheduler/pauseAtStop"), m_pauseAtStop->isChecked());
    settings.remove(QStringLiteral("scheduler/startFired"));
    settings.remove(QStringLiteral("scheduler/stopFired"));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        QMessageBox::warning(this, tr("No se pudo guardar"), tr("Revisa los permisos de configuración."));
        return;
    }
    accept();
}
} // namespace oidm
