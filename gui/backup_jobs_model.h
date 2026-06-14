#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace vaultium::gui {

/**
 * @brief List model of configured backup jobs for the GUI.
 *
 * A job is a named pointer to a configuration file plus its source type. The
 * model is intentionally thin for this stage: it holds jobs in memory and is
 * the seam where persistence (scanning a config directory, a jobs database, …)
 * will later plug in. It mirrors the core's source-based model: every job has
 * one of the supported source types (database / filesystem / service-config).
 */
class BackupJobsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        SourceTypeRole,
        ConfigPathRole
    };

    explicit BackupJobsModel(QObject* parent = nullptr);

    [[nodiscard]] int count() const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Adds a job to the list. sourceType is "database" | "filesystem" | "service-config".
    Q_INVOKABLE void addJob(const QString& name, const QString& sourceType, const QString& configPath);

    /// Removes the job at the given row.
    Q_INVOKABLE void removeJob(int row);

    /// Field accessors by row (for forms that pick a job).
    [[nodiscard]] Q_INVOKABLE QString nameAt(int row) const;
    [[nodiscard]] Q_INVOKABLE QString sourceTypeAt(int row) const;
    [[nodiscard]] Q_INVOKABLE QString configPathAt(int row) const;

signals:
    void countChanged();

private:
    struct Job {
        QString name;
        QString sourceType;
        QString configPath;
    };

    QList<Job> m_jobs;
};

} // namespace vaultium::gui
