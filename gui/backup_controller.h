#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace vaultium::gui {

/**
 * @brief QML-facing bridge over the Vaultium engine.
 *
 * Heavy operations (backup / remote backup / verify / restore) are executed by
 * launching the `vaultium` CLI as a child process via QProcess. This keeps the
 * GUI responsive (QProcess is event-loop driven, no worker threads), avoids
 * fork()/libssh2 running inside the Qt process (which is unsafe on macOS), and
 * lets us stream the engine's log output live. Lightweight helpers (config
 * writing, history listing) run in-process since they only use Qt.
 */
class BackupController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastArtifact READ lastArtifact NOTIFY lastArtifactChanged)

    // Live transfer progress (populated during remote downloads).
    Q_PROPERTY(bool transferActive READ transferActive NOTIFY transferChanged)
    Q_PROPERTY(QString transferState READ transferState NOTIFY transferChanged)
    Q_PROPERTY(QString transferFile READ transferFile NOTIFY transferChanged)
    Q_PROPERTY(double transferPercent READ transferPercent NOTIFY transferChanged)
    Q_PROPERTY(QString transferDetail READ transferDetail NOTIFY transferChanged)
    Q_PROPERTY(QString transferSpeed READ transferSpeed NOTIFY transferChanged)
    Q_PROPERTY(QString transferEta READ transferEta NOTIFY transferChanged)

public:
    explicit BackupController(QObject* parent = nullptr);

    [[nodiscard]] QString status() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString lastArtifact() const;
    [[nodiscard]] bool transferActive() const;
    [[nodiscard]] QString transferState() const;
    [[nodiscard]] QString transferFile() const;
    [[nodiscard]] double transferPercent() const;
    [[nodiscard]] QString transferDetail() const;
    [[nodiscard]] QString transferSpeed() const;
    [[nodiscard]] QString transferEta() const;

public slots:
    /// Runs a job, auto-selecting local or remote based on the config's mode.
    void run(const QString& configPath);

    /// Opens a file with the OS default handler.
    void openPath(const QString& path) const;

    /// Reveals a file in the system file manager (Finder / Explorer / files app).
    void revealInFolder(const QString& path) const;

    /// Deletes an artifact and its .sha256/.meta.json sidecars. Returns true on ok.
    bool deleteArtifact(const QString& path);

    /// Renames an artifact (and its sidecars) to newName within the same folder.
    bool renameArtifact(const QString& path, const QString& newName);

    /// Writes a metadata JSON (name/size/date/checksum/source) for an artifact to destPath.
    bool exportMetadata(const QString& path, const QString& destPath);

    /// Runs several backup configs in sequence (the multi-source plan). Stops on
    /// the first failure. Each config auto-selects local/remote.
    void runPlan(const QStringList& configPaths);

    /// Cancels the running operation: terminates the engine process and reports a
    /// Cancelled result. Remote temp files may be left on the server.
    void cancel();

    /// Forces a local backup.
    void runBackup(const QString& configPath);

    /// Forces a remote (SSH) backup that downloads to this client.
    void runRemoteBackup(const QString& configPath);

    /// Tests SSH connectivity and required remote tools without backing up.
    void testRemoteConnection(const QString& configPath);

    /// Verifies an artifact's checksum/structure (result via operationFinished).
    void verifyArtifact(const QString& configPath, const QString& archive);

    /// Restores an artifact. overwrite=false performs a dry run.
    void restoreArtifact(
        const QString& configPath,
        const QString& archive,
        const QString& destination,
        bool overwrite);

    /// Writes a KEY=VALUE config file from the given fields. Returns true on ok.
    bool writeConfig(const QVariantMap& fields, const QString& path);

    /// Writes the fields to a throwaway temp config (with safe defaults) and
    /// returns its path. Lets "Test connection" run before a save location is set.
    QString writeTempConfig(const QVariantMap& fields, const QString& name = {});

    /// Lists backup artifacts in a directory, enriched with checksum/metadata.
    QVariantList listHistory(const QString& backupDir);

    /// Strips a leading file:// URL scheme so QML FileDialog URLs work as paths.
    QString toLocalPath(const QString& urlOrPath) const;

    /// Copies text to the system clipboard (used by the Console copy button).
    void copyToClipboard(const QString& text) const;

signals:
    void statusChanged();
    void busyChanged();
    void lastArtifactChanged();
    void transferChanged();
    void logMessage(const QString& line);
    void operationFinished(bool success, const QString& message);

private:
    void setStatus(const QString& status);
    void setBusy(bool busy);

    bool writeFields(const QVariantMap& fields, const QString& localPath);

    /// Launches `vaultium <args>` as a child process and tracks it.
    void startCli(const QStringList& args, const QString& startStatus, const QString& okMessage);
    [[nodiscard]] QString cliPath() const;
    [[nodiscard]] QString readExecutionMode(const QString& configPath) const;

    void handleProcessOutput();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void setLastArtifact(const QString& path);

    void resetTransfer(const QString& state);
    void updateProgress(qulonglong done, qulonglong total);
    void inferTransferState(const QString& logLine);

    QProcess m_process;
    QString m_okMessage;
    QString m_lastError;
    QString m_lastArtifact;

    // Transfer progress.
    QString m_transferState { QStringLiteral("idle") };
    QString m_transferFile;
    qulonglong m_done { 0 };
    qulonglong m_total { 0 };
    double m_speedBps { 0 };
    QElapsedTimer m_speedTimer;
    qulonglong m_speedLastBytes { 0 };
    QString m_status { QStringLiteral("Idle") };
    bool m_busy { false };
    bool m_cancelled { false };

    // Multi-source plan execution.
    QStringList m_plan;
    int m_planIndex { 0 };
    bool m_planActive { false };
};

} // namespace vaultium::gui
