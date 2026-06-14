#include "backup_jobs_model.h"

namespace vaultium::gui {

BackupJobsModel::BackupJobsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BackupJobsModel::count() const
{
    return static_cast<int>(m_jobs.size());
}

int BackupJobsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(m_jobs.size());
}

QVariant BackupJobsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size()) {
        return {};
    }

    const auto& job = m_jobs.at(index.row());

    switch (role) {
    case NameRole:
        return job.name;
    case SourceTypeRole:
        return job.sourceType;
    case ConfigPathRole:
        return job.configPath;
    default:
        return {};
    }
}

QHash<int, QByteArray> BackupJobsModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { SourceTypeRole, "sourceType" },
        { ConfigPathRole, "configPath" }
    };
}

void BackupJobsModel::addJob(const QString& name, const QString& sourceType, const QString& configPath)
{
    beginInsertRows({}, static_cast<int>(m_jobs.size()), static_cast<int>(m_jobs.size()));
    m_jobs.push_back(Job { name, sourceType, configPath });
    endInsertRows();
    emit countChanged();
}

void BackupJobsModel::removeJob(int row)
{
    if (row < 0 || row >= m_jobs.size()) {
        return;
    }

    beginRemoveRows({}, row, row);
    m_jobs.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

QString BackupJobsModel::nameAt(int row) const
{
    return (row >= 0 && row < m_jobs.size()) ? m_jobs.at(row).name : QString {};
}

QString BackupJobsModel::sourceTypeAt(int row) const
{
    return (row >= 0 && row < m_jobs.size()) ? m_jobs.at(row).sourceType : QString {};
}

QString BackupJobsModel::configPathAt(int row) const
{
    return (row >= 0 && row < m_jobs.size()) ? m_jobs.at(row).configPath : QString {};
}

} // namespace vaultium::gui
