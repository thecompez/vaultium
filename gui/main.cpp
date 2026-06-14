#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QtQuickControls2/QQuickStyle>

// Entry point for the Vaultium Qt/QML GUI. The C++ view-model types
// (BackupController, BackupJobsModel) are registered via QML_ELEMENT and live in
// the "VaultiumUI" QML module; all backup logic comes from vaultium_core.
auto main(int argc, char* argv[]) -> int
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Vaultium"));
    app.setOrganizationName(QStringLiteral("Vaultium"));

    // Use the Basic style so the app's custom dark theme fully applies instead
    // of the native platform style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("VaultiumUI", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // Dev aid: VAULTIUM_GRAB=<png> renders the window once and exits (used with
    // QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software for headless UI checks).
    const QByteArray grabPath = qgetenv("VAULTIUM_GRAB");
    if (!grabPath.isEmpty()) {
        const QByteArray grabIndex = qgetenv("VAULTIUM_GRAB_INDEX");
        if (!grabIndex.isEmpty() && !engine.rootObjects().isEmpty()) {
            engine.rootObjects().first()->setProperty("currentIndex", grabIndex.toInt());
        }
        QTimer::singleShot(1800, [&engine, grabPath]() {
            const auto objects = engine.rootObjects();
            if (!objects.isEmpty()) {
                if (auto* window = qobject_cast<QQuickWindow*>(objects.first())) {
                    window->grabWindow().save(QString::fromUtf8(grabPath));
                }
            }
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
