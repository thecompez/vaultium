#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace vaultium::gui {

/**
 * @brief Saved server profiles ("connect once, reuse everywhere").
 *
 * Non-secret fields are persisted to a JSON file; passwords / key passphrases go
 * to the OS keychain (macOS Keychain via `security`, Linux Secret Service via
 * `secret-tool`) — never written to disk in plain text. Test-connection shells
 * out to the `vaultium` CLI (QProcess), like the other view-models.
 */
class ServersViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList servers READ servers NOTIFY changed)
    Q_PROPERTY(bool keychainAvailable READ keychainAvailable CONSTANT)
    Q_PROPERTY(QString lastUsedId READ lastUsedId NOTIFY changed)
    Q_PROPERTY(QString testingId READ testingId NOTIFY testingChanged)

public:
    explicit ServersViewModel(QObject* parent = nullptr);

    [[nodiscard]] QVariantList servers() const;
    [[nodiscard]] bool keychainAvailable() const;
    [[nodiscard]] QString lastUsedId() const;
    [[nodiscard]] QString testingId() const;

public slots:
    /// Creates (empty id) or updates a server. If fields contains a non-empty
    /// "secret", it is stored in the keychain and stripped from the record.
    /// Returns the server id.
    QString saveServer(const QVariantMap& fields);

    void removeServer(const QString& id);
    QString duplicateServer(const QString& id);
    void setFavorite(const QString& id, bool favorite);
    void markUsed(const QString& id);

    /// Tests connectivity for a saved server (async → operationFinished).
    void testConnection(const QString& id);

    /// REMOTE_* config fields (incl. the keychain secret) for a saved server,
    /// ready to merge into a backup config.
    [[nodiscard]] QVariantMap connectionFields(const QString& id) const;

    /// Whether a stored secret exists for the server.
    [[nodiscard]] bool hasSecret(const QString& id) const;

signals:
    void changed();
    void testingChanged();
    void operationFinished(bool success, const QString& message);

private:
    struct Server {
        QString id, name, host, port { "22" }, user, authMethod { "password" };
        QString keyPath, notes, tags;
        bool favorite {};
        QString lastStatus { "unknown" };
        QString lastConnected;
        QString createdAt, updatedAt;
    };

    void load();
    void persist();
    [[nodiscard]] int indexOf(const QString& id) const;
    [[nodiscard]] QVariantMap toMap(const Server& s) const;
    [[nodiscard]] QString storePath() const;
    [[nodiscard]] QString cliPath() const;
    [[nodiscard]] QString writeTempConfig(const QString& id) const;

    QList<Server> m_servers;
    QString m_lastUsedId;
    QString m_testingId;
    QProcess m_testProcess;
};

} // namespace vaultium::gui
