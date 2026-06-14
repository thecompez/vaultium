#include "backup_controller.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTextStream>
#include <QUrl>

namespace vaultium::gui {

namespace {

[[nodiscard]] QString humanBytes(double bytes)
{
    static const char* units[] { "B", "KB", "MB", "GB", "TB" };
    int i = 0;
    while (bytes >= 1024.0 && i < 4) { bytes /= 1024.0; ++i; }
    return QStringLiteral("%1 %2").arg(bytes, 0, 'f', (i == 0 || bytes >= 100) ? 0 : 1).arg(units[i]);
}

[[nodiscard]] QString etaString(double seconds)
{
    if (seconds < 1) return QStringLiteral("<1s");
    const int s = static_cast<int>(seconds);
    if (s < 60) return QStringLiteral("%1s").arg(s);
    if (s < 3600) return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
}

} // namespace

BackupController::BackupController(QObject* parent)
    : QObject(parent)
{
    // Interleave the CLI's stdout/stderr so log lines stay in order.
    m_process.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_process, &QProcess::readyReadStandardOutput,
        this, &BackupController::handleProcessOutput);
    connect(&m_process, &QProcess::finished,
        this, &BackupController::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setBusy(false);
            setStatus(QStringLiteral("Engine not found"));
            emit operationFinished(false,
                QStringLiteral("Could not launch the Vaultium engine at %1.").arg(cliPath()));
        }
    });
}

QString BackupController::cliPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/vaultium");
}

QString BackupController::readExecutionMode(const QString& configPath) const
{
    QFile file { toLocalPath(configPath) };
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("local");
    }

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QStringLiteral("EXECUTION_MODE"))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq >= 0) {
                return line.mid(eq + 1).trimmed().toLower();
            }
        }
    }

    return QStringLiteral("local");
}

void BackupController::startCli(const QStringList& args, const QString& startStatus, const QString& okMessage)
{
    if (m_busy) {
        return; // one operation at a time
    }

    m_okMessage = okMessage;
    m_lastError.clear();
    setLastArtifact({});
    resetTransfer(QStringLiteral("pending"));

    setStatus(startStatus);
    setBusy(true);

    emit logMessage(QStringLiteral("$ vaultium %1").arg(args.join(QLatin1Char(' '))));

    m_process.start(cliPath(), args);
}

void BackupController::handleProcessOutput()
{
    const QString chunk = QString::fromUtf8(m_process.readAllStandardOutput());
    const auto lines = chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        // Machine-readable progress events: "@@PROGRESS|state|done|total".
        if (trimmed.startsWith(QStringLiteral("@@PROGRESS|"))) {
            const auto parts = trimmed.split(QLatin1Char('|'));
            if (parts.size() >= 4) {
                updateProgress(parts[2].toULongLong(), parts[3].toULongLong());
            }
            continue;
        }
        inferTransferState(trimmed);
        if (trimmed.contains(QStringLiteral("[ERROR]"))) {
            // Keep the message after the "[ERROR]" tag for the result toast.
            m_lastError = trimmed.section(QStringLiteral("[ERROR]"), 1).trimmed();
        }
        // Capture the produced artifact path for "Open / Reveal" success actions.
        for (const auto& marker : { QStringLiteral("Downloaded file:"), QStringLiteral("Backup file:") }) {
            const int idx = trimmed.indexOf(marker);
            if (idx >= 0) {
                setLastArtifact(trimmed.mid(idx + marker.length()).trimmed());
            }
        }
        emit logMessage(trimmed);
    }
}

void BackupController::cancel()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_cancelled = true;
        m_planActive = false;
        setStatus(QStringLiteral("Cancelling…"));
        m_process.kill();
    }
}

void BackupController::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    handleProcessOutput(); // drain anything buffered

    if (m_cancelled) {
        m_cancelled = false;
        m_planActive = false;
        m_transferState = QStringLiteral("cancelled");
        emit transferChanged();
        setStatus(QStringLiteral("Cancelled"));
        setBusy(false);
        emit operationFinished(false, QStringLiteral("Backup cancelled."));
        return;
    }

    const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0;
    m_transferState = ok ? QStringLiteral("completed") : QStringLiteral("failed");
    emit transferChanged();

    setStatus(ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
    setBusy(false);

    QString message;
    if (ok) {
        message = m_okMessage;
    } else if (!m_lastError.isEmpty()) {
        message = m_lastError;
    } else {
        message = QStringLiteral("Operation failed (exit code %1). See the log for details.").arg(exitCode);
    }

    // Advance a multi-source plan: continue while steps remain and succeed.
    if (m_planActive) {
        if (ok && m_planIndex + 1 < m_plan.size()) {
            ++m_planIndex;
            emit operationFinished(ok, message);
            run(m_plan.at(m_planIndex));
            return;
        }
        m_planActive = false;
    }

    emit operationFinished(ok, message);
}

QString BackupController::status() const
{
    return m_status;
}

bool BackupController::busy() const
{
    return m_busy;
}

void BackupController::setStatus(const QString& status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}

void BackupController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
        // transferActive tracks busy; notify its bindings too so the transfer bar
        // disappears as soon as the operation finishes.
        emit transferChanged();
    }
}

void BackupController::run(const QString& configPath)
{
    const QString mode = readExecutionMode(configPath);
    if (mode == QStringLiteral("remote_ssh") || mode == QStringLiteral("remote") || mode == QStringLiteral("ssh")) {
        runRemoteBackup(configPath);
    } else {
        runBackup(configPath);
    }
}

void BackupController::runPlan(const QStringList& configPaths)
{
    if (m_busy || configPaths.isEmpty()) {
        return;
    }
    m_plan = configPaths;
    m_planIndex = 0;
    m_planActive = true;
    run(m_plan.at(0));
}

void BackupController::runBackup(const QString& configPath)
{
    startCli(
        { QStringLiteral("backup"), QStringLiteral("--config"), toLocalPath(configPath) },
        QStringLiteral("Running backup…"),
        QStringLiteral("Backup completed successfully."));
}

void BackupController::runRemoteBackup(const QString& configPath)
{
    startCli(
        { QStringLiteral("remote"), QStringLiteral("--config"), toLocalPath(configPath) },
        QStringLiteral("Running remote backup…"),
        QStringLiteral("Remote backup downloaded to the client."));
}

void BackupController::testRemoteConnection(const QString& configPath)
{
    startCli(
        { QStringLiteral("remote-test"), QStringLiteral("--config"), toLocalPath(configPath) },
        QStringLiteral("Testing remote connection…"),
        QStringLiteral("Connected to the remote server and required tools are present."));
}

void BackupController::verifyArtifact(const QString& configPath, const QString& archive)
{
    startCli(
        { QStringLiteral("verify"),
          QStringLiteral("--config"), toLocalPath(configPath),
          QStringLiteral("--archive"), toLocalPath(archive) },
        QStringLiteral("Verifying…"),
        QStringLiteral("Artifact verified."));
}

void BackupController::restoreArtifact(
    const QString& configPath,
    const QString& archive,
    const QString& destination,
    bool overwrite)
{
    QStringList args {
        QStringLiteral("restore"),
        QStringLiteral("--config"), toLocalPath(configPath),
        QStringLiteral("--archive"), toLocalPath(archive)
    };
    if (!destination.isEmpty()) {
        args << QStringLiteral("--dest") << toLocalPath(destination);
    }
    args << (overwrite ? QStringLiteral("--overwrite") : QStringLiteral("--dry-run"));

    startCli(args,
        QStringLiteral("Restoring…"),
        overwrite ? QStringLiteral("Restore applied.")
                  : QStringLiteral("Dry run finished (enable overwrite to apply)."));
}

QString BackupController::toLocalPath(const QString& urlOrPath) const
{
    const QUrl url { urlOrPath };
    return url.isLocalFile() ? url.toLocalFile() : urlOrPath;
}

void BackupController::copyToClipboard(const QString& text) const
{
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString BackupController::lastArtifact() const
{
    return m_lastArtifact;
}

void BackupController::setLastArtifact(const QString& path)
{
    if (m_lastArtifact != path) {
        m_lastArtifact = path;
        emit lastArtifactChanged();
    }
}

void BackupController::resetTransfer(const QString& state)
{
    m_transferState = state;
    m_transferFile.clear();
    m_done = 0;
    m_total = 0;
    m_speedBps = 0;
    m_speedLastBytes = 0;
    m_speedTimer.invalidate();
    emit transferChanged();
}

void BackupController::updateProgress(qulonglong done, qulonglong total)
{
    if (!m_speedTimer.isValid()) {
        m_speedTimer.start();
        m_speedLastBytes = done;
    } else if (const qint64 ms = m_speedTimer.elapsed(); ms >= 400) {
        const double inst = static_cast<double>(done - m_speedLastBytes) * 1000.0 / static_cast<double>(ms);
        m_speedBps = (m_speedBps <= 0) ? inst : (0.55 * m_speedBps + 0.45 * inst);
        m_speedLastBytes = done;
        m_speedTimer.restart();
    }
    m_done = done;
    m_total = total;
    m_transferState = QStringLiteral("downloading");
    emit transferChanged();
}

void BackupController::inferTransferState(const QString& line)
{
    QString next;
    if (line.contains(QStringLiteral("Connecting to remote")) || line.contains(QStringLiteral("Creating remote backup"))) {
        next = QStringLiteral("preparing");
    } else if (line.contains(QStringLiteral("Provisioning")) || line.contains(QStringLiteral("Uploading"))) {
        next = QStringLiteral("uploading");
    } else if (line.contains(QStringLiteral("Downloading backup"))) {
        next = QStringLiteral("downloading");
        // "...Downloading backup to: /path/to/files_….tar.gz" → show the file name.
        const int colon = line.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            const QString path = line.mid(colon + 1).trimmed();
            const int slash = path.lastIndexOf(QLatin1Char('/'));
            m_transferFile = slash >= 0 ? path.mid(slash + 1) : path;
        }
    } else if (line.contains(QStringLiteral("Removing remote temporary"))) {
        next = QStringLiteral("cleaningup");
    } else if (line.contains(QStringLiteral("Starting backup"))) {
        next = QStringLiteral("working");
    }
    if (!next.isEmpty() && next != m_transferState && m_transferState != QStringLiteral("downloading")) {
        m_transferState = next;
        emit transferChanged();
    }
}

bool BackupController::transferActive() const { return m_busy; }
QString BackupController::transferState() const { return m_transferState; }
QString BackupController::transferFile() const { return m_transferFile; }

double BackupController::transferPercent() const
{
    if (m_total > 0) {
        return static_cast<double>(m_done) / static_cast<double>(m_total);
    }
    return m_transferState == QStringLiteral("completed") ? 1.0 : 0.0;
}

QString BackupController::transferDetail() const
{
    if (m_total > 0) {
        return humanBytes(static_cast<double>(m_done)) + QStringLiteral(" / ") + humanBytes(static_cast<double>(m_total));
    }
    return m_done > 0 ? humanBytes(static_cast<double>(m_done)) : QString {};
}

QString BackupController::transferSpeed() const
{
    return (m_transferState == QStringLiteral("downloading") && m_speedBps > 0)
        ? humanBytes(m_speedBps) + QStringLiteral("/s")
        : QString {};
}

QString BackupController::transferEta() const
{
    if (m_transferState == QStringLiteral("downloading") && m_speedBps > 0 && m_total > m_done) {
        return etaString(static_cast<double>(m_total - m_done) / m_speedBps);
    }
    return {};
}

void BackupController::openPath(const QString& path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(toLocalPath(path)));
}

void BackupController::revealInFolder(const QString& path) const
{
    const QString local = toLocalPath(path);
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-R"), local });
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer"),
        { QStringLiteral("/select,") + QDir::toNativeSeparators(local) });
#else
    // Linux/other: open the containing folder.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(local).absolutePath()));
#endif
}

bool BackupController::deleteArtifact(const QString& path)
{
    const QString local = toLocalPath(path);
    bool ok = QFile::remove(local);
    QFile::remove(local + QStringLiteral(".sha256"));
    QFile::remove(local + QStringLiteral(".meta.json"));
    return ok;
}

bool BackupController::renameArtifact(const QString& path, const QString& newName)
{
    const QString local = toLocalPath(path);
    const QFileInfo info { local };
    const QString clean = newName.trimmed();
    if (!info.exists() || clean.isEmpty() || clean.contains(QLatin1Char('/'))) {
        emit operationFinished(false, QStringLiteral("Enter a valid file name."));
        return false;
    }
    const QString target = info.absolutePath() + QLatin1Char('/') + clean;
    if (QFileInfo::exists(target)) {
        emit operationFinished(false, QStringLiteral("A file with that name already exists."));
        return false;
    }
    if (!QFile::rename(local, target)) {
        emit operationFinished(false, QStringLiteral("Rename failed."));
        return false;
    }
    QFile::rename(local + QStringLiteral(".sha256"), target + QStringLiteral(".sha256"));
    QFile::rename(local + QStringLiteral(".meta.json"), target + QStringLiteral(".meta.json"));
    emit operationFinished(true, QStringLiteral("Renamed to %1.").arg(clean));
    return true;
}

bool BackupController::exportMetadata(const QString& path, const QString& destPath)
{
    const QString local = toLocalPath(path);
    const QFileInfo info { local };
    if (!info.exists()) {
        emit operationFinished(false, QStringLiteral("Artifact not found."));
        return false;
    }

    QJsonObject meta;
    meta[QStringLiteral("name")] = info.fileName();
    meta[QStringLiteral("path")] = info.absoluteFilePath();
    meta[QStringLiteral("sizeBytes")] = static_cast<qint64>(info.size());
    meta[QStringLiteral("modified")] = info.lastModified().toString(Qt::ISODate);

    QFile sha { local + QStringLiteral(".sha256") };
    if (sha.open(QIODevice::ReadOnly | QIODevice::Text)) {
        meta[QStringLiteral("sha256")] = QString::fromUtf8(sha.readLine()).trimmed().section(QLatin1Char(' '), 0, 0);
    }
    QFile sidecar { local + QStringLiteral(".meta.json") };
    if (sidecar.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(sidecar.readAll());
        if (doc.isObject()) {
            meta[QStringLiteral("source")] = doc.object();
        }
    }

    QFile out { toLocalPath(destPath) };
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit operationFinished(false, QStringLiteral("Could not write metadata file."));
        return false;
    }
    out.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
    emit operationFinished(true, QStringLiteral("Metadata exported."));
    return true;
}

bool BackupController::writeFields(const QVariantMap& fields, const QString& localPath)
{
    QFile file { localPath };
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream out { &file };
    out << "# Generated by Vaultium GUI\n";

    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const QString key = it.key();
        const QVariant value = it.value();

        QString rendered;
        if (value.typeId() == QMetaType::Bool) {
            rendered = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (value.typeId() == QMetaType::QStringList) {
            rendered = value.toStringList().join(QStringLiteral(","));
        } else {
            rendered = value.toString();
        }

        if (!rendered.isEmpty()) {
            out << key << "=" << rendered << "\n";
        }
    }

    file.close();
    return true;
}

bool BackupController::writeConfig(const QVariantMap& fields, const QString& path)
{
    const auto localPath = toLocalPath(path);

    if (!writeFields(fields, localPath)) {
        emit operationFinished(false, QStringLiteral("Could not write config: %1").arg(localPath));
        return false;
    }

    emit operationFinished(true, QStringLiteral("Saved config to %1").arg(localPath));
    return true;
}

QString BackupController::writeTempConfig(const QVariantMap& fields, const QString& name)
{
    // Fill defaults for fields a connection test does not exercise, so config
    // validation passes even before the user has filled in destinations.
    QVariantMap m = fields;
    const QString temp = QDir::tempPath();

    const auto ensure = [&m](const QString& key, const QString& fallback) {
        if (m.value(key).toString().isEmpty()) {
            m[key] = fallback;
        }
    };
    ensure(QStringLiteral("BACKUP_DIR"), temp);
    ensure(QStringLiteral("REMOTE_DOWNLOAD_DIR"), temp);
    ensure(QStringLiteral("REMOTE_SERVER_BACKUP_DIR"), QStringLiteral("/tmp/vaultium_remote_backups"));

    const QString fileName = name.isEmpty()
        ? QStringLiteral("vaultium_test.conf")
        : QStringLiteral("vaultium_%1.conf").arg(name);
    const QString path = QDir(temp).filePath(fileName);
    if (!writeFields(m, path)) {
        emit operationFinished(false, QStringLiteral("Could not write temporary config."));
        return {};
    }

    return path;
}

QVariantList BackupController::listHistory(const QString& backupDir)
{
    QVariantList result;

    const QDir dir { toLocalPath(backupDir) };
    if (!dir.exists()) {
        return result;
    }

    static const QStringList prefixes {
        QStringLiteral("mysql_"), QStringLiteral("postgresql_"),
        QStringLiteral("sqlite_"), QStringLiteral("files_"),
        QStringLiteral("service_"), QStringLiteral("database_")
    };

    const auto entries = dir.entryInfoList(QDir::Files, QDir::Time);

    for (const QFileInfo& info : entries) {
        const QString name = info.fileName();

        if (name.endsWith(QStringLiteral(".sha256"))
            || name.endsWith(QStringLiteral(".meta.json"))
            || name.endsWith(QStringLiteral(".tmp"))) {
            continue;
        }

        bool known = false;
        for (const QString& prefix : prefixes) {
            if (name.startsWith(prefix)) {
                known = true;
                break;
            }
        }
        if (!known) {
            continue;
        }

        QVariantMap entry;
        entry[QStringLiteral("name")] = name;
        entry[QStringLiteral("path")] = info.absoluteFilePath();
        entry[QStringLiteral("size")] = static_cast<qlonglong>(info.size());
        entry[QStringLiteral("modified")] =
            info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

        // Checksum sidecar (first whitespace-delimited token).
        QFile sha { info.absoluteFilePath() + QStringLiteral(".sha256") };
        entry[QStringLiteral("verified")] = sha.exists();
        if (sha.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString line = QString::fromUtf8(sha.readLine()).trimmed();
            entry[QStringLiteral("checksum")] = line.section(QLatin1Char(' '), 0, 0);
        }

        // Metadata sidecar.
        QString source = QStringLiteral("—");
        QString detail;
        QFile meta { info.absoluteFilePath() + QStringLiteral(".meta.json") };
        if (meta.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(meta.readAll());
            if (doc.isObject()) {
                const auto obj = doc.object();
                source = obj.value(QStringLiteral("source")).toString(source);
                detail = obj.value(QStringLiteral("detail")).toString();
            }
        } else {
            if (name.startsWith(QStringLiteral("files_"))) {
                source = QStringLiteral("filesystem");
            } else if (name.startsWith(QStringLiteral("service_"))) {
                source = QStringLiteral("service-config");
            } else {
                source = QStringLiteral("database");
            }
        }
        entry[QStringLiteral("source")] = source;
        entry[QStringLiteral("detail")] = detail;

        result.append(entry);
    }

    return result;
}

} // namespace vaultium::gui
