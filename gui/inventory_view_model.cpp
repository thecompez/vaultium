#include "inventory_view_model.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace vaultium::gui {

InventoryViewModel::InventoryViewModel(QObject* parent)
    : QObject(parent)
{
    connect(&m_session, &QProcess::readyReadStandardOutput, this, &InventoryViewModel::handleStdout);
    connect(&m_session, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit error(QStringLiteral("Could not launch the discovery engine."));
            setOutstanding(0);
        }
    });
    connect(&m_session, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_intentionalStop) {
            m_intentionalStop = false;
            setOutstanding(0);
            return; // restarting the session, not a real failure
        }
        if (m_outstanding > 0 || (status == QProcess::CrashExit) || code != 0) {
            const QString err = QString::fromUtf8(m_session.readAllStandardError()).trimmed();
            emit error(err.isEmpty()
                ? QStringLiteral("Discovery session ended unexpectedly.")
                : err.section(QStringLiteral("[ERROR]"), 1).trimmed());
        }
        setOutstanding(0);
    });
}

InventoryViewModel::~InventoryViewModel()
{
    stopSession();
}

bool InventoryViewModel::busy() const
{
    return m_outstanding > 0;
}

void InventoryViewModel::setOutstanding(int value)
{
    const bool was = m_outstanding > 0;
    m_outstanding = value < 0 ? 0 : value;
    if ((m_outstanding > 0) != was) {
        emit busyChanged();
    }
}

QString InventoryViewModel::cliPath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/vaultium");
}

void InventoryViewModel::stopSession()
{
    if (m_session.state() != QProcess::NotRunning) {
        m_intentionalStop = true;
        m_session.closeWriteChannel();
        m_session.terminate();
        if (!m_session.waitForFinished(1500)) {
            m_session.kill();
            m_session.waitForFinished(500);
        }
    }
}

void InventoryViewModel::startSession()
{
    stopSession();
    m_buffer.clear();
    m_intentionalStop = false;
    setOutstanding(0);
    m_session.start(cliPath(),
        { QStringLiteral("inspect"), QStringLiteral("--config"), m_configPath, QStringLiteral("--session") });
}

void InventoryViewModel::setTarget(const QString& configPath)
{
    m_configPath = configPath;
    m_dirCache.clear();
    m_sizeCache.clear();
    m_services.clear();
    m_applications.clear();
    m_engines.clear();
    m_servicesCached = m_appsCached = m_enginesCached = false;
    startSession();
}

void InventoryViewModel::send(const QString& command)
{
    if (m_configPath.isEmpty()) {
        emit error(QStringLiteral("No server selected for discovery."));
        return;
    }
    if (m_session.state() == QProcess::NotRunning) {
        startSession();
    }
    setOutstanding(m_outstanding + 1);
    m_session.write(command.toUtf8() + '\n');
}

void InventoryViewModel::browse(const QString& path)
{
    const QString p = path.isEmpty() ? QStringLiteral("/") : path;
    if (m_dirCache.contains(p)) {
        emit listingReady(p, m_dirCache.value(p));
        return;
    }
    send(QStringLiteral("dir ") + p);
}

void InventoryViewModel::invalidate(const QString& path)
{
    m_dirCache.remove(path.isEmpty() ? QStringLiteral("/") : path);
}

void InventoryViewModel::requestSize(const QString& path)
{
    if (m_sizeCache.contains(path)) {
        emit sizeReady(path, m_sizeCache.value(path));
        return;
    }
    send(QStringLiteral("size ") + path);
}

void InventoryViewModel::loadServices()
{
    if (m_servicesCached) {
        emit servicesReady(m_services);
        return;
    }
    send(QStringLiteral("services"));
}

void InventoryViewModel::loadApplications()
{
    if (m_appsCached) {
        emit applicationsReady(m_applications);
        return;
    }
    send(QStringLiteral("apps"));
}

void InventoryViewModel::loadDatabaseEngines()
{
    if (m_enginesCached) {
        emit enginesReady(m_engines);
        return;
    }
    send(QStringLiteral("dbengines"));
}

void InventoryViewModel::loadDatabases(const QString& engine)
{
    send(QStringLiteral("databases ") + engine);
}

void InventoryViewModel::loadTables(const QString& engine, const QString& database)
{
    send(QStringLiteral("tables ") + engine + QLatin1Char(' ') + database);
}

void InventoryViewModel::refresh()
{
    m_dirCache.clear();
    m_sizeCache.clear();
    m_servicesCached = m_appsCached = m_enginesCached = false;
    startSession();
    emit refreshed();
}

void InventoryViewModel::handleStdout()
{
    m_buffer += m_session.readAllStandardOutput();
    int newline = -1;
    while ((newline = m_buffer.indexOf('\n')) != -1) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (!line.trimmed().isEmpty()) {
            dispatchLine(line);
        }
    }
}

void InventoryViewModel::dispatchLine(const QByteArray& line)
{
    setOutstanding(m_outstanding - 1);

    const auto doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        return;
    }
    const auto root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("dir")) {
        const QString path = root.value(QStringLiteral("path")).toString();
        QVariantList entries;
        for (const auto value : root.value(QStringLiteral("entries")).toArray()) {
            const auto o = value.toObject();
            QVariantMap entry;
            entry[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
            entry[QStringLiteral("path")] = o.value(QStringLiteral("path")).toString();
            entry[QStringLiteral("isDir")] = o.value(QStringLiteral("isDir")).toBool();
            entries.append(entry);
        }
        m_dirCache.insert(path, entries);
        emit listingReady(path, entries);
    } else if (type == QStringLiteral("size")) {
        const QString path = root.value(QStringLiteral("path")).toString();
        const auto bytes = static_cast<qulonglong>(root.value(QStringLiteral("bytes")).toDouble());
        m_sizeCache.insert(path, bytes);
        emit sizeReady(path, bytes);
    } else if (type == QStringLiteral("services")) {
        m_services.clear();
        for (const auto value : root.value(QStringLiteral("services")).toArray()) {
            const auto o = value.toObject();
            QVariantMap service;
            service[QStringLiteral("id")] = o.value(QStringLiteral("id")).toString();
            service[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
            service[QStringLiteral("present")] = o.value(QStringLiteral("present")).toBool();
            QStringList paths;
            for (const auto a : o.value(QStringLiteral("assets")).toArray()) {
                for (const auto p : a.toObject().value(QStringLiteral("paths")).toArray()) {
                    paths << p.toString();
                }
            }
            service[QStringLiteral("paths")] = paths;
            m_services.append(service);
        }
        m_servicesCached = true;
        emit servicesReady(m_services);
    } else if (type == QStringLiteral("apps")) {
        m_applications.clear();
        for (const auto value : root.value(QStringLiteral("apps")).toArray()) {
            const auto o = value.toObject();
            QVariantMap app;
            app[QStringLiteral("app")] = o.value(QStringLiteral("app")).toString();
            app[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
            app[QStringLiteral("root")] = o.value(QStringLiteral("root")).toString();
            app[QStringLiteral("usesDatabase")] = o.value(QStringLiteral("usesDatabase")).toBool();
            QStringList paths;
            for (const auto p : o.value(QStringLiteral("paths")).toArray()) {
                paths << p.toString();
            }
            app[QStringLiteral("paths")] = paths;
            QStringList services;
            for (const auto s : o.value(QStringLiteral("services")).toArray()) {
                services << s.toString();
            }
            app[QStringLiteral("services")] = services;
            m_applications.append(app);
        }
        m_appsCached = true;
        emit applicationsReady(m_applications);
    } else if (type == QStringLiteral("databases")) {
        QStringList dbs;
        for (const auto value : root.value(QStringLiteral("databases")).toArray()) {
            dbs << value.toString();
        }
        emit databasesReady(root.value(QStringLiteral("engine")).toString(), dbs);
    } else if (type == QStringLiteral("tables")) {
        QStringList tbls;
        for (const auto value : root.value(QStringLiteral("tables")).toArray()) {
            tbls << value.toString();
        }
        emit tablesReady(root.value(QStringLiteral("db")).toString(), tbls);
    } else if (type == QStringLiteral("dbengines")) {
        m_engines.clear();
        for (const auto value : root.value(QStringLiteral("engines")).toArray()) {
            m_engines << value.toString();
        }
        m_enginesCached = true;
        emit enginesReady(m_engines);
    }
}

} // namespace vaultium::gui
