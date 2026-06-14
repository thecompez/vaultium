#include "servers_view_model.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace vaultium::gui {

namespace {

constexpr auto kService = "vaultium";

[[nodiscard]] QString account(const QString& id)
{
    return QStringLiteral("server.") + id;
}

// -- OS keychain (macOS `security`, Linux `secret-tool`) --------------------

[[nodiscard]] bool keychainSupported()
{
#if defined(Q_OS_MACOS)
    return true;
#elif defined(Q_OS_LINUX)
    return QProcess::execute(QStringLiteral("sh"),
        { QStringLiteral("-c"), QStringLiteral("command -v secret-tool >/dev/null 2>&1") }) == 0;
#else
    return false;
#endif
}

void keychainStore(const QString& id, const QString& secret)
{
#if defined(Q_OS_MACOS)
    QProcess p;
    p.start(QStringLiteral("security"),
        { QStringLiteral("add-generic-password"), QStringLiteral("-U"),
          QStringLiteral("-a"), QString::fromLatin1(kService),
          QStringLiteral("-s"), QString::fromLatin1(kService) + QStringLiteral(".") + account(id),
          QStringLiteral("-w"), secret });
    p.waitForFinished(5000);
#elif defined(Q_OS_LINUX)
    QProcess p;
    p.start(QStringLiteral("secret-tool"),
        { QStringLiteral("store"), QStringLiteral("--label=Vaultium ") + id,
          QStringLiteral("service"), QString::fromLatin1(kService),
          QStringLiteral("account"), account(id) });
    if (p.waitForStarted(3000)) {
        p.write(secret.toUtf8());
        p.closeWriteChannel();
        p.waitForFinished(5000);
    }
#else
    Q_UNUSED(id) Q_UNUSED(secret)
#endif
}

[[nodiscard]] QString keychainGet(const QString& id)
{
#if defined(Q_OS_MACOS)
    QProcess p;
    p.start(QStringLiteral("security"),
        { QStringLiteral("find-generic-password"),
          QStringLiteral("-a"), QString::fromLatin1(kService),
          QStringLiteral("-s"), QString::fromLatin1(kService) + QStringLiteral(".") + account(id),
          QStringLiteral("-w") });
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
#elif defined(Q_OS_LINUX)
    QProcess p;
    p.start(QStringLiteral("secret-tool"),
        { QStringLiteral("lookup"), QStringLiteral("service"), QString::fromLatin1(kService),
          QStringLiteral("account"), account(id) });
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
#else
    Q_UNUSED(id)
    return {};
#endif
}

void keychainDelete(const QString& id)
{
#if defined(Q_OS_MACOS)
    QProcess::execute(QStringLiteral("security"),
        { QStringLiteral("delete-generic-password"),
          QStringLiteral("-a"), QString::fromLatin1(kService),
          QStringLiteral("-s"), QString::fromLatin1(kService) + QStringLiteral(".") + account(id) });
#elif defined(Q_OS_LINUX)
    QProcess::execute(QStringLiteral("secret-tool"),
        { QStringLiteral("clear"), QStringLiteral("service"), QString::fromLatin1(kService),
          QStringLiteral("account"), account(id) });
#else
    Q_UNUSED(id)
#endif
}

} // namespace

ServersViewModel::ServersViewModel(QObject* parent)
    : QObject(parent)
{
    connect(&m_testProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        const QString id = m_testingId;
        const int i = indexOf(id);
        if (i >= 0) {
            m_servers[i].lastStatus = ok ? QStringLiteral("ok") : QStringLiteral("failed");
            if (ok) {
                m_servers[i].lastConnected = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            }
            persist();
            emit changed();
        }
        QString err = QString::fromUtf8(m_testProcess.readAllStandardError())
                          .section(QStringLiteral("[ERROR]"), 1).trimmed();
        m_testingId.clear();
        emit testingChanged();
        emit operationFinished(ok, ok ? QStringLiteral("Connection succeeded.")
                                      : (err.isEmpty() ? QStringLiteral("Connection failed.") : err));
    });
    load();
}

QVariantList ServersViewModel::servers() const
{
    QVariantList out;
    for (const auto& s : m_servers) {
        out.append(toMap(s));
    }
    return out;
}

bool ServersViewModel::keychainAvailable() const { return keychainSupported(); }
QString ServersViewModel::lastUsedId() const { return m_lastUsedId; }
QString ServersViewModel::testingId() const { return m_testingId; }

QVariantMap ServersViewModel::toMap(const Server& s) const
{
    QVariantMap m;
    m[QStringLiteral("id")] = s.id;
    m[QStringLiteral("name")] = s.name;
    m[QStringLiteral("host")] = s.host;
    m[QStringLiteral("port")] = s.port;
    m[QStringLiteral("user")] = s.user;
    m[QStringLiteral("authMethod")] = s.authMethod;
    m[QStringLiteral("keyPath")] = s.keyPath;
    m[QStringLiteral("notes")] = s.notes;
    m[QStringLiteral("tags")] = s.tags;
    m[QStringLiteral("favorite")] = s.favorite;
    m[QStringLiteral("lastStatus")] = s.lastStatus;
    m[QStringLiteral("lastConnected")] = s.lastConnected.isEmpty() ? QStringLiteral("—") : s.lastConnected;
    return m;
}

int ServersViewModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers[i].id == id) {
            return i;
        }
    }
    return -1;
}

QString ServersViewModel::storePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/servers.json");
}

QString ServersViewModel::cliPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/vaultium");
}

void ServersViewModel::load()
{
    QFile file { storePath() };
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    m_lastUsedId = root.value(QStringLiteral("lastUsedId")).toString();
    m_servers.clear();
    for (const auto value : root.value(QStringLiteral("servers")).toArray()) {
        const auto o = value.toObject();
        Server s;
        s.id = o.value(QStringLiteral("id")).toString();
        s.name = o.value(QStringLiteral("name")).toString();
        s.host = o.value(QStringLiteral("host")).toString();
        s.port = o.value(QStringLiteral("port")).toString(QStringLiteral("22"));
        s.user = o.value(QStringLiteral("user")).toString();
        s.authMethod = o.value(QStringLiteral("authMethod")).toString(QStringLiteral("password"));
        s.keyPath = o.value(QStringLiteral("keyPath")).toString();
        s.notes = o.value(QStringLiteral("notes")).toString();
        s.tags = o.value(QStringLiteral("tags")).toString();
        s.favorite = o.value(QStringLiteral("favorite")).toBool();
        s.lastStatus = o.value(QStringLiteral("lastStatus")).toString(QStringLiteral("unknown"));
        s.lastConnected = o.value(QStringLiteral("lastConnected")).toString();
        s.createdAt = o.value(QStringLiteral("createdAt")).toString();
        s.updatedAt = o.value(QStringLiteral("updatedAt")).toString();
        if (!s.id.isEmpty()) {
            m_servers.append(s);
        }
    }
}

void ServersViewModel::persist()
{
    QJsonArray arr;
    for (const auto& s : m_servers) {
        QJsonObject o;
        o[QStringLiteral("id")] = s.id;
        o[QStringLiteral("name")] = s.name;
        o[QStringLiteral("host")] = s.host;
        o[QStringLiteral("port")] = s.port;
        o[QStringLiteral("user")] = s.user;
        o[QStringLiteral("authMethod")] = s.authMethod;
        o[QStringLiteral("keyPath")] = s.keyPath;
        o[QStringLiteral("notes")] = s.notes;
        o[QStringLiteral("tags")] = s.tags;
        o[QStringLiteral("favorite")] = s.favorite;
        o[QStringLiteral("lastStatus")] = s.lastStatus;
        o[QStringLiteral("lastConnected")] = s.lastConnected;
        o[QStringLiteral("createdAt")] = s.createdAt;
        o[QStringLiteral("updatedAt")] = s.updatedAt;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("servers")] = arr;
    root[QStringLiteral("lastUsedId")] = m_lastUsedId;

    QFile file { storePath() };
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

QString ServersViewModel::saveServer(const QVariantMap& f)
{
    const QString now = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    QString id = f.value(QStringLiteral("id")).toString();

    int i = indexOf(id);
    if (id.isEmpty() || i < 0) {
        Server s;
        s.id = QUuid::createUuid().toString(QUuid::Id128);
        s.createdAt = now;
        m_servers.append(s);
        i = m_servers.size() - 1;
        id = s.id;
    }

    Server& s = m_servers[i];
    s.name = f.value(QStringLiteral("name")).toString();
    s.host = f.value(QStringLiteral("host")).toString();
    s.port = f.value(QStringLiteral("port"), QStringLiteral("22")).toString();
    s.user = f.value(QStringLiteral("user")).toString();
    s.authMethod = f.value(QStringLiteral("authMethod"), QStringLiteral("password")).toString();
    s.keyPath = f.value(QStringLiteral("keyPath")).toString();
    s.notes = f.value(QStringLiteral("notes")).toString();
    s.tags = f.value(QStringLiteral("tags")).toString();
    if (f.contains(QStringLiteral("favorite"))) {
        s.favorite = f.value(QStringLiteral("favorite")).toBool();
    }
    s.updatedAt = now;

    const QString secret = f.value(QStringLiteral("secret")).toString();
    if (!secret.isEmpty()) {
        keychainStore(id, secret);
    }

    persist();
    emit changed();
    return id;
}

void ServersViewModel::removeServer(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    keychainDelete(id);
    m_servers.removeAt(i);
    if (m_lastUsedId == id) {
        m_lastUsedId.clear();
    }
    persist();
    emit changed();
}

QString ServersViewModel::duplicateServer(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0) {
        return {};
    }
    Server copy = m_servers[i];
    copy.id = QUuid::createUuid().toString(QUuid::Id128);
    copy.name = copy.name + QStringLiteral(" (copy)");
    copy.favorite = false;
    copy.lastStatus = QStringLiteral("unknown");
    copy.lastConnected.clear();
    copy.createdAt = copy.updatedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_servers.append(copy);
    // Copy the secret too, if present.
    const QString secret = keychainGet(id);
    if (!secret.isEmpty()) {
        keychainStore(copy.id, secret);
    }
    persist();
    emit changed();
    return copy.id;
}

void ServersViewModel::setFavorite(const QString& id, bool favorite)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    m_servers[i].favorite = favorite;
    persist();
    emit changed();
}

void ServersViewModel::markUsed(const QString& id)
{
    if (indexOf(id) < 0 || m_lastUsedId == id) {
        return;
    }
    m_lastUsedId = id;
    persist();
    emit changed();
}

bool ServersViewModel::hasSecret(const QString& id) const
{
    return !keychainGet(id).isEmpty();
}

QVariantMap ServersViewModel::connectionFields(const QString& id) const
{
    QVariantMap f;
    const int i = indexOf(id);
    if (i < 0) {
        return f;
    }
    const Server& s = m_servers[i];
    f[QStringLiteral("EXECUTION_MODE")] = QStringLiteral("remote_ssh");
    f[QStringLiteral("REMOTE_HOST")] = s.host;
    f[QStringLiteral("REMOTE_PORT")] = s.port;
    f[QStringLiteral("REMOTE_USER")] = s.user;
    f[QStringLiteral("REMOTE_AUTH_METHOD")] = s.authMethod;
    const QString secret = keychainGet(id);
    if (s.authMethod == QStringLiteral("key")) {
        f[QStringLiteral("REMOTE_IDENTITY_FILE")] = s.keyPath;
        if (!secret.isEmpty()) {
            f[QStringLiteral("REMOTE_IDENTITY_PASSPHRASE")] = secret;
        }
    } else {
        f[QStringLiteral("REMOTE_PASSWORD")] = secret;
    }
    return f;
}

QString ServersViewModel::writeTempConfig(const QString& id) const
{
    const auto fields = connectionFields(id);
    if (fields.isEmpty()) {
        return {};
    }
    const QString path = QDir::tempPath() + QStringLiteral("/vaultium_servertest.conf");
    QFile file { path };
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return {};
    }
    file.write("# Vaultium server test\n");
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const QString v = it.value().toString();
        if (!v.isEmpty()) {
            file.write((it.key() + "=" + v + "\n").toUtf8());
        }
    }
    return path;
}

void ServersViewModel::testConnection(const QString& id)
{
    if (!m_testingId.isEmpty()) {
        return;
    }
    const QString path = writeTempConfig(id);
    if (path.isEmpty()) {
        emit operationFinished(false, QStringLiteral("Could not prepare the connection test."));
        return;
    }
    m_testingId = id;
    emit testingChanged();
    m_testProcess.start(cliPath(), { QStringLiteral("remote-test"), QStringLiteral("--config"), path });
}

} // namespace vaultium::gui
