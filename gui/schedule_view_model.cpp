#include "schedule_view_model.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace vaultium::gui {

ScheduleViewModel::ScheduleViewModel(QObject* parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::finished, this, &ScheduleViewModel::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_running = false;
            emit busyChanged();
            emit operationFinished(false, QStringLiteral("Could not launch the scheduler engine."));
        }
    });
}

bool ScheduleViewModel::busy() const { return m_running || !m_queue.isEmpty(); }
QString ScheduleViewModel::backend() const { return m_backend; }
bool ScheduleViewModel::supported() const { return m_supported; }
QVariantList ScheduleViewModel::schedules() const { return m_schedules; }

QString ScheduleViewModel::cliPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/vaultium");
}

void ScheduleViewModel::enqueue(const QString& tag, const QStringList& args)
{
    m_queue.append({ tag, args });
    emit busyChanged();
    if (!m_running) {
        startNext();
    }
}

void ScheduleViewModel::startNext()
{
    if (m_queue.isEmpty()) {
        return;
    }
    const auto job = m_queue.takeFirst();
    m_currentTag = job.first;
    m_running = true;
    emit busyChanged();
    m_process.start(cliPath(), job.second);
}

void ScheduleViewModel::refresh()
{
    enqueue(QStringLiteral("list"), { QStringLiteral("schedule"), QStringLiteral("list") });
}

void ScheduleViewModel::save(const QVariantMap& f)
{
    QStringList args { QStringLiteral("schedule"), QStringLiteral("save") };
    const auto add = [&args](const QString& flag, const QString& value) {
        if (!value.isEmpty()) { args << flag << value; }
    };
    add(QStringLiteral("--id"), f.value(QStringLiteral("id")).toString());
    add(QStringLiteral("--name"), f.value(QStringLiteral("name")).toString());
    add(QStringLiteral("--type"), f.value(QStringLiteral("type")).toString());
    add(QStringLiteral("--time"), f.value(QStringLiteral("time")).toString());
    add(QStringLiteral("--dow"), f.value(QStringLiteral("dow")).toString());
    add(QStringLiteral("--dom"), f.value(QStringLiteral("dom")).toString());
    add(QStringLiteral("--once"), f.value(QStringLiteral("once")).toString());
    add(QStringLiteral("--cron"), f.value(QStringLiteral("cron")).toString());
    add(QStringLiteral("--backup-config"), f.value(QStringLiteral("config")).toString());
    add(QStringLiteral("--backup-type"), f.value(QStringLiteral("backupType")).toString());
    add(QStringLiteral("--scope"), f.value(QStringLiteral("scope")).toString());
    args << QStringLiteral("--enabled") << (f.value(QStringLiteral("enabled"), true).toBool() ? "true" : "false");
    enqueue(QStringLiteral("save"), args);
}

void ScheduleViewModel::remove(const QString& id)
{
    enqueue(QStringLiteral("op"), { QStringLiteral("schedule"), QStringLiteral("remove"), QStringLiteral("--id"), id });
}

void ScheduleViewModel::setEnabled(const QString& id, bool enabled)
{
    enqueue(QStringLiteral("op"), { QStringLiteral("schedule"), QStringLiteral("set-enabled"),
        QStringLiteral("--id"), id, QStringLiteral("--enabled"), enabled ? "true" : "false" });
}

void ScheduleViewModel::runNow(const QString& id)
{
    enqueue(QStringLiteral("run"), { QStringLiteral("schedule"), QStringLiteral("run"), QStringLiteral("--id"), id });
}

void ScheduleViewModel::repair(const QString& id)
{
    enqueue(QStringLiteral("op"), { QStringLiteral("schedule"), QStringLiteral("repair"), QStringLiteral("--id"), id });
}

void ScheduleViewModel::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    const QByteArray out = m_process.readAllStandardOutput();
    const QString tag = m_currentTag;
    m_running = false;

    if (tag == QStringLiteral("list")) {
        const auto root = QJsonDocument::fromJson(out).object();
        m_backend = root.value(QStringLiteral("backend")).toString();
        m_supported = m_backend != QStringLiteral("unsupported");
        m_schedules.clear();
        for (const auto value : root.value(QStringLiteral("schedules")).toArray()) {
            m_schedules.append(value.toObject().toVariantMap());
        }
        emit changed();
    } else {
        const auto root = QJsonDocument::fromJson(out).object();
        const bool opOk = root.value(QStringLiteral("ok")).toBool(ok);
        QString message = root.value(QStringLiteral("message")).toString();
        if (tag == QStringLiteral("run")) {
            message = opOk ? QStringLiteral("Backup finished.") : QStringLiteral("Scheduled backup failed (see log).");
        } else if (message.isEmpty()) {
            message = opOk ? QStringLiteral("Done.") : QStringLiteral("Operation failed.");
        }
        emit operationFinished(opOk, message);
        // Always reflect the new state.
        if (m_queue.isEmpty() || m_queue.constFirst().first != QStringLiteral("list")) {
            m_queue.prepend({ QStringLiteral("list"), { QStringLiteral("schedule"), QStringLiteral("list") } });
        }
    }

    if (!m_queue.isEmpty()) {
        startNext();
    } else {
        emit busyChanged();
    }
}

} // namespace vaultium::gui
