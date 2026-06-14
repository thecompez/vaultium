#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace vaultium::gui {

/**
 * @brief Drives server discovery via a single long-lived `vaultium inspect
 * --session` process (one connection, many queries) with in-memory caching.
 *
 * Running the CLI keeps libssh2/fork out of the GUI process; the persistent
 * session avoids reconnecting per query. Results are cached and served instantly
 * on repeat requests; refresh() drops the cache and reconnects.
 */
class InventoryViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit InventoryViewModel(QObject* parent = nullptr);
    ~InventoryViewModel() override;

    [[nodiscard]] bool busy() const;

public slots:
    /// Selects the target (a config describing local or remote SSH) and starts
    /// a fresh discovery session.
    void setTarget(const QString& configPath);

    void browse(const QString& path);
    void invalidate(const QString& path); // drop a single directory from the cache
    void requestSize(const QString& path);
    void loadServices();
    void loadApplications();
    void loadDatabaseEngines();
    void loadDatabases(const QString& engine);
    void loadTables(const QString& engine, const QString& database);

    /// Clears caches, reconnects, and signals listeners to re-scan.
    void refresh();

signals:
    void busyChanged();
    void listingReady(const QString& path, const QVariantList& entries);
    void sizeReady(const QString& path, qulonglong bytes);
    void servicesReady(const QVariantList& services);
    void applicationsReady(const QVariantList& applications);
    void enginesReady(const QStringList& engines);
    void databasesReady(const QString& engine, const QStringList& databases);
    void tablesReady(const QString& database, const QStringList& tables);
    void refreshed();
    void error(const QString& message);

private:
    void startSession();
    void stopSession();
    void send(const QString& command);
    void handleStdout();
    void dispatchLine(const QByteArray& line);
    void setOutstanding(int value);
    [[nodiscard]] QString cliPath() const;

    QProcess m_session;
    QString m_configPath;
    QByteArray m_buffer;
    int m_outstanding { 0 };

    QHash<QString, QVariantList> m_dirCache;
    QHash<QString, qulonglong> m_sizeCache;
    QVariantList m_services;
    QVariantList m_applications;
    QStringList m_engines;
    bool m_servicesCached { false };
    bool m_appsCached { false };
    bool m_enginesCached { false };
    bool m_intentionalStop { false }; // suppress the "session ended" error on restart
};

} // namespace vaultium::gui
