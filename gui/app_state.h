#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace vaultium::gui {

/**
 * @brief Global, persisted application state shared across the GUI.
 *
 * Holds the Simple/Advanced disclosure mode and the theme preference
 * (System/Light/Dark). "System" follows the OS colour scheme live. Stored via
 * QSettings; a single instance is created in Main.qml.
 */
class AppState : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool advancedMode READ advancedMode WRITE setAdvancedMode NOTIFY advancedModeChanged)
    // "system" | "light" | "dark"
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeChanged)
    // Whether the OS is currently in dark mode.
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY themeChanged)
    // The dark/light value the app should actually render (resolves "system").
    Q_PROPERTY(bool effectiveDark READ effectiveDark NOTIFY themeChanged)

public:
    explicit AppState(QObject* parent = nullptr);

    [[nodiscard]] bool advancedMode() const;
    void setAdvancedMode(bool advanced);

    [[nodiscard]] QString themeMode() const;
    void setThemeMode(const QString& mode);
    [[nodiscard]] bool systemDark() const;
    [[nodiscard]] bool effectiveDark() const;

signals:
    void advancedModeChanged();
    void themeChanged();

private:
    void refreshSystemDark();

    bool m_advancedMode { false };
    QString m_themeMode { QStringLiteral("system") };
    bool m_systemDark { false };
};

} // namespace vaultium::gui
