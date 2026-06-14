#include "app_state.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

namespace vaultium::gui {

namespace {
constexpr auto kAdvancedModeKey = "ui/advancedMode";
constexpr auto kThemeModeKey = "ui/themeMode";
}

AppState::AppState(QObject* parent)
    : QObject(parent)
{
    QSettings settings;
    m_advancedMode = settings.value(QString::fromLatin1(kAdvancedModeKey), false).toBool();
    m_themeMode = settings.value(QString::fromLatin1(kThemeModeKey), QStringLiteral("system")).toString();

    refreshSystemDark();

    // Follow the OS colour scheme live (Qt 6.5+).
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            const bool was = m_systemDark;
            refreshSystemDark();
            if (m_systemDark != was) {
                emit themeChanged();
            }
        });
    }
}

void AppState::refreshSystemDark()
{
    if (auto* hints = QGuiApplication::styleHints()) {
        m_systemDark = hints->colorScheme() == Qt::ColorScheme::Dark;
    }
}

bool AppState::advancedMode() const
{
    return m_advancedMode;
}

void AppState::setAdvancedMode(bool advanced)
{
    if (m_advancedMode == advanced) {
        return;
    }
    m_advancedMode = advanced;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kAdvancedModeKey), advanced);
    emit advancedModeChanged();
}

QString AppState::themeMode() const
{
    return m_themeMode;
}

void AppState::setThemeMode(const QString& mode)
{
    const QString normalized = (mode == QStringLiteral("light") || mode == QStringLiteral("dark"))
        ? mode : QStringLiteral("system");
    if (m_themeMode == normalized) {
        return;
    }
    m_themeMode = normalized;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kThemeModeKey), normalized);
    emit themeChanged();
}

bool AppState::systemDark() const
{
    return m_systemDark;
}

bool AppState::effectiveDark() const
{
    if (m_themeMode == QStringLiteral("light")) {
        return false;
    }
    if (m_themeMode == QStringLiteral("dark")) {
        return true;
    }
    return m_systemDark; // "system"
}

} // namespace vaultium::gui
