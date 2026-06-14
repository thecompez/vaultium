pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VaultiumUI

Item {
    id: screen
    property var scheduleVm
    property var jobsModel
    signal createBackup()

    property var jobNames: {
        var a = [];
        if (screen.jobsModel) for (var i = 0; i < screen.jobsModel.count; ++i) a.push(screen.jobsModel.nameAt(i));
        return a;
    }

    Component.onCompleted: if (scheduleVm) scheduleVm.refresh()

    function statusTone(s) {
        if (s === "completed") return "success";
        if (s === "failed") return "danger";
        if (s === "working") return "warning";
        return "neutral";
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space6
        spacing: Theme.space5

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3
            ColumnLayout {
                spacing: Theme.space1
                Layout.alignment: Qt.AlignVCenter
                Text { text: qsTr("Schedules"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXl; font.weight: Font.Bold }
                Text { text: qsTr("Recurring backups that run automatically — even when Vaultium is closed"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase }
            }
            Item { Layout.fillWidth: true }
            StatusPill {
                visible: screen.scheduleVm
                tone: screen.scheduleVm && screen.scheduleVm.supported ? "success" : "danger"
                text: screen.scheduleVm ? (screen.scheduleVm.supported ? screen.scheduleVm.backend : qsTr("unsupported")) : ""
            }
            IconButton { iconName: "history"; tip: qsTr("Refresh"); onClicked: if (screen.scheduleVm) screen.scheduleVm.refresh() }
            AppButton {
                text: qsTr("New schedule"); iconName: "create"; variant: "primary"
                enabled: screen.jobNames.length > 0 && screen.scheduleVm && screen.scheduleVm.supported
                onClicked: form.openNew()
            }
        }

        Banner {
            visible: screen.scheduleVm && !screen.scheduleVm.supported
            Layout.fillWidth: true; tone: "warning"
            text: qsTr("Automatic scheduling is not available on this platform yet. The schedule list is read-only.")
        }
        Banner {
            visible: screen.jobNames.length === 0
            Layout.fillWidth: true; tone: "info"
            text: qsTr("Create a backup first — schedules run one of your saved backup jobs.")
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: Theme.space2

            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: !screen.scheduleVm || screen.scheduleVm.schedules.length === 0
                iconName: "clock"
                title: qsTr("No schedules yet")
                body: qsTr("Schedule a saved backup to run daily, weekly, monthly, once, or on a custom cron.")
            }

            ListView {
                anchors.fill: parent
                visible: screen.scheduleVm && screen.scheduleVm.schedules.length > 0
                model: screen.scheduleVm ? screen.scheduleVm.schedules : []
                clip: true
                spacing: Theme.space2

                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    implicitHeight: 84
                    radius: Theme.radiusSm
                    color: rowHover.hovered ? Theme.muted : "transparent"
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.space4
                        anchors.rightMargin: Theme.space4
                        spacing: Theme.space4

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                spacing: Theme.space2
                                Text { text: modelData.name; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.DemiBold }
                                StatusPill { tone: screen.statusTone(modelData.lastStatus); text: modelData.lastStatus }
                                StatusPill { visible: modelData.scope === "system"; tone: "info"; text: qsTr("system") }
                                StatusPill { visible: modelData.enabled && !modelData.installed; tone: "danger"; text: qsTr("trigger missing") }
                            }
                            Text { text: modelData.summary; color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
                            Text {
                                text: qsTr("Next: ") + modelData.nextRun + "   •   " + qsTr("Last: ") + modelData.lastRun
                                    + (modelData.lastError ? ("   •   " + modelData.lastError) : "")
                                color: modelData.lastError ? Theme.danger : Theme.fgSubtle
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }

                        AppButton {
                            visible: modelData.enabled && !modelData.installed
                            text: qsTr("Repair"); iconName: "restore"; variant: "secondary"
                            onClicked: screen.scheduleVm.repair(modelData.id)
                        }
                        AppSwitch {
                            checked: modelData.enabled
                            onToggled: screen.scheduleVm.setEnabled(modelData.id, checked)
                        }
                        IconButton { iconName: "play"; tip: qsTr("Run now"); onClicked: screen.scheduleVm.runNow(modelData.id) }
                        IconButton { iconName: "settings"; tip: qsTr("Edit"); onClicked: form.openEdit(modelData) }
                        IconButton { iconName: "trash"; tip: qsTr("Delete"); onClicked: { screen.pendingDelete = modelData.id; screen.pendingDeleteName = modelData.name; confirmDelete.open(); } }
                    }
                }
            }
        }
    }

    property string pendingDelete: ""
    property string pendingDeleteName: ""

    ConfirmDialog {
        id: confirmDelete
        danger: true
        title: qsTr("Delete schedule?")
        message: qsTr("Remove “%1” and its OS trigger? Future runs will stop.").arg(screen.pendingDeleteName)
        confirmText: qsTr("Delete")
        onConfirmed: screen.scheduleVm.remove(screen.pendingDelete)
    }

    // ---- Create / edit form -------------------------------------------------
    Popup {
        id: form
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        width: 520
        padding: 0
        closePolicy: Popup.CloseOnEscape

        property string editId: ""
        property int jobIndex: 0
        property int typeIndex: 1   // 0 once,1 daily,2 weekly,3 monthly,4 cron
        property int scopeIndex: 0  // 0 user, 1 system
        readonly property var typeKeys: ["once", "daily", "weekly", "monthly", "cron"]

        function openNew() {
            editId = ""; nameField.text = ""; jobIndex = 0; typeIndex = 1; scopeIndex = 0;
            timeField.text = "02:00"; dowCombo.currentIndex = 1; domField.text = "1";
            onceField.text = ""; cronField.text = "0 2 * * *"; enabledSwitch.checked = true;
            open();
        }
        function openEdit(s) {
            editId = s.id; nameField.text = s.name;
            typeIndex = Math.max(0, typeKeys.indexOf(s.type));
            scopeIndex = (s.scope === "system") ? 1 : 0;
            cronField.text = s.cron && s.cron.length ? s.cron : "0 2 * * *";
            onceField.text = s.onceAt;
            enabledSwitch.checked = s.enabled;
            open();
        }
        function save() {
            var f = {
                "id": form.editId,
                "name": nameField.text,
                "config": screen.jobsModel.configPathAt(form.jobIndex),
                "backupType": screen.jobsModel.sourceTypeAt(form.jobIndex),
                "type": form.typeKeys[form.typeIndex],
                "scope": form.scopeIndex === 1 ? "system" : "user",
                "time": timeField.text,
                "dow": "" + dowCombo.currentIndex,
                "dom": domField.text,
                "once": onceField.text,
                "cron": cronField.text,
                "enabled": enabledSwitch.checked
            };
            screen.scheduleVm.save(f);
            form.close();
        }

        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { radius: Theme.radiusLg; color: Theme.surfaceAlt; border.color: Theme.borderStrong; border.width: 1 }

        contentItem: ColumnLayout {
            spacing: Theme.space4

            Text {
                Layout.margins: Theme.space5; Layout.bottomMargin: 0
                text: form.editId ? qsTr("Edit schedule") : qsTr("New schedule")
                color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLg; font.weight: Font.Bold
            }

            GridLayout {
                Layout.fillWidth: true; Layout.leftMargin: Theme.space5; Layout.rightMargin: Theme.space5
                columns: 2; columnSpacing: Theme.space4; rowSpacing: Theme.space3

                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Name"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: nameField; Layout.fillWidth: true; placeholderText: qsTr("Nightly site backup") }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Backup job"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppComboBox { Layout.fillWidth: true; model: screen.jobNames; currentIndex: form.jobIndex; onActivated: (i) => form.jobIndex = i }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Frequency"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppComboBox { Layout.fillWidth: true; model: [qsTr("Once"), qsTr("Daily"), qsTr("Weekly"), qsTr("Monthly"), qsTr("Custom cron")]; currentIndex: form.typeIndex; onActivated: (i) => form.typeIndex = i }
                }
                ColumnLayout { visible: form.typeIndex >= 1 && form.typeIndex <= 3; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Time (HH:MM)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: timeField; Layout.fillWidth: true; text: "02:00" }
                }
                ColumnLayout { visible: form.typeIndex === 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Day of week"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppComboBox { id: dowCombo; Layout.fillWidth: true; model: [qsTr("Sunday"), qsTr("Monday"), qsTr("Tuesday"), qsTr("Wednesday"), qsTr("Thursday"), qsTr("Friday"), qsTr("Saturday")]; currentIndex: 1 }
                }
                ColumnLayout { visible: form.typeIndex === 3; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Day of month"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: domField; Layout.fillWidth: true; text: "1" }
                }
                ColumnLayout { visible: form.typeIndex === 0; Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Run once at (YYYY-MM-DD HH:MM)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: onceField; Layout.fillWidth: true; placeholderText: qsTr("2026-07-01 03:00") }
                }
                ColumnLayout { visible: form.typeIndex === 4; Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Cron expression (min hour dom month dow)"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    AppTextField { id: cronField; Layout.fillWidth: true; text: "0 2 * * *" }
                }
                ColumnLayout { Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                    Text { text: qsTr("Run for"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                    SegmentedControl {
                        options: [qsTr("Me (when logged in)"), qsTr("All users — system")]
                        currentIndex: form.scopeIndex
                        onActivated: (i) => form.scopeIndex = i
                    }
                    Text {
                        visible: form.scopeIndex === 1
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("System schedules run even when you're logged out. Installing one asks for an administrator password.")
                        color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                    }
                }
                RowLayout { Layout.columnSpan: 2; Layout.fillWidth: true
                    AppSwitch { id: enabledSwitch; text: qsTr("Enabled"); checked: true }
                    Item { Layout.fillWidth: true }
                }
            }

            RowLayout {
                Layout.fillWidth: true; Layout.margins: Theme.space5; Layout.topMargin: 0
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: form.close() }
                AppButton { text: qsTr("Save schedule"); iconName: "check"; variant: "primary"; enabled: nameField.text.length > 0; onClicked: form.save() }
            }
        }
    }
}
