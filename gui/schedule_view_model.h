#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace vaultium::gui {

/**
 * @brief QML-facing wrapper over the `vaultium schedule` CLI.
 *
 * All scheduler logic (persistence, cron, OS triggers) lives in the C++ core and
 * is driven through the CLI; this view-model only marshals state/actions to QML.
 */
class ScheduleViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(QVariantList schedules READ schedules NOTIFY changed)

public:
    explicit ScheduleViewModel(QObject* parent = nullptr);

    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString backend() const;
    [[nodiscard]] bool supported() const;
    [[nodiscard]] QVariantList schedules() const;

public slots:
    void refresh();
    void save(const QVariantMap& fields);
    void remove(const QString& id);
    void setEnabled(const QString& id, bool enabled);
    void runNow(const QString& id);
    void repair(const QString& id);

signals:
    void busyChanged();
    void changed();
    void operationFinished(bool success, const QString& message);

private:
    void enqueue(const QString& tag, const QStringList& args);
    void startNext();
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    [[nodiscard]] QString cliPath() const;

    QProcess m_process;
    QList<QPair<QString, QStringList>> m_queue;
    QString m_currentTag;
    bool m_running { false };

    QString m_backend;
    bool m_supported { false };
    QVariantList m_schedules;
};

} // namespace vaultium::gui
