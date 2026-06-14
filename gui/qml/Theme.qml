pragma Singleton

import QtQuick

// Central design tokens for the Vaultium GUI.
// Professional slate theme with a green "success/run" accent, available in both
// dark and light modes. Toggle `Theme.dark` to switch; every token below derives
// from it so the whole app re-themes from one place.
QtObject {
    // The one switch that drives everything.
    property bool dark: true

    // -- Surfaces & background -------------------------------------------------
    readonly property color bg:          dark ? "#0F172A" : "#F1F5F9" // app background
    readonly property color surface:     dark ? "#172033" : "#FFFFFF" // cards / panels
    readonly property color surfaceAlt:  dark ? "#1E293B" : "#FFFFFF" // raised / sidebar
    readonly property color muted:       dark ? "#222C40" : "#E9EEF5" // subtle fills, hover
    readonly property color secondary:   dark ? "#334155" : "#E2E8F0" // secondary controls
    readonly property color secondaryHover: dark ? "#3E4D68" : "#CBD5E1"

    // -- Lines -----------------------------------------------------------------
    readonly property color border:       dark ? "#26324A" : "#E2E8F0" // hairline dividers
    readonly property color borderStrong: dark ? "#3A4763" : "#CBD5E1" // input borders

    // -- Text ------------------------------------------------------------------
    readonly property color fg:        dark ? "#F8FAFC" : "#0F172A" // primary text
    readonly property color fgMuted:   dark ? "#94A3B8" : "#475569" // secondary text
    readonly property color fgSubtle:  dark ? "#64748B" : "#94A3B8" // tertiary / placeholders

    // -- Brand & semantic ------------------------------------------------------
    readonly property color accent:       dark ? "#22C55E" : "#16A34A" // primary action / success
    readonly property color accentHover:  dark ? "#16A34A" : "#15803D"
    readonly property color accentSoft:   dark ? "#13351F" : "#DCFCE7" // accent-tinted fill
    readonly property color danger:       dark ? "#EF4444" : "#DC2626"
    readonly property color dangerHover:  dark ? "#DC2626" : "#B91C1C"
    readonly property color dangerSoft:   dark ? "#3A1A1D" : "#FEE2E2"
    readonly property color warning:      dark ? "#F59E0B" : "#D97706"
    readonly property color warningSoft:  dark ? "#3A2A12" : "#FEF3C7"
    readonly property color info:         dark ? "#38BDF8" : "#0284C7"
    readonly property color infoSoft:     dark ? "#12303F" : "#E0F2FE"

    // Text colors for use on top of accent/danger fills. (Avoid an "on" prefix:
    // QML treats `onX` identifiers as signal handlers.)
    readonly property color textOnAccent:  dark ? "#06210F" : "#FFFFFF"
    readonly property color textOnDanger:  "#FFFFFF"

    // Modal scrim (stronger in light mode so foreground stays legible).
    readonly property color scrim: dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.45)

    // -- Spacing scale (4 / 8) -------------------------------------------------
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32
    readonly property int space7: 48

    // -- Radii -----------------------------------------------------------------
    readonly property int radiusSm:   6
    readonly property int radius:     10
    readonly property int radiusLg:   16
    readonly property int radiusPill: 999

    // -- Type scale ------------------------------------------------------------
    readonly property int fsXs:   12
    readonly property int fsSm:    13
    readonly property int fsBase:  14
    readonly property int fsMd:    16
    readonly property int fsLg:    20
    readonly property int fsXl:    26
    readonly property int fs2xl:   32

    readonly property string fontFamily: "Inter"
    readonly property string monoFamily: "JetBrains Mono"

    // -- Motion ----------------------------------------------------------------
    readonly property int durFast: 120
    readonly property int durBase: 180
}
