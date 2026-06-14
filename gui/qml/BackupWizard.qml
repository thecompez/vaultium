pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import VaultiumUI

Item {
    id: wizard
    property var controller
    property var jobsModel
    property var appState
    property var serversVm
    signal done()
    signal manualMode()

    readonly property bool advanced: appState ? appState.advancedMode : false

    // ---- State --------------------------------------------------------------
    property int step: 0
    property int targetIndex: 0          // 0 = this computer, 1 = remote SSH
    readonly property bool remote: targetIndex === 1
    property bool connected: false

    property int sourceIndex: 0          // 0 apps, 1 files, 2 databases, 3 services

    property var applications: []
    property var selectedAppRoots: []

    property string currentPath: "/"
    property var entries: []
    property var selectedPaths: []
    property var sizeMap: ({})            // path -> bytes

    property var services: []
    property var selectedServices: []

    property var engines: []
    property string selectedEngine: ""
    property var databases: []
    property var selectedDatabases: []
    property string expandedDb: ""
    property var tablesByDb: ({})

    property string destDir: ""

    InventoryViewModel { id: inventory }

    function humanSize(bytes) {
        if (bytes === undefined) return "";
        if (bytes < 1024) return bytes + " B";
        var u = ["KB", "MB", "GB", "TB"]; var v = bytes / 1024; var i = 0;
        while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
        return v.toFixed(v < 10 ? 1 : 0) + " " + u[i];
    }

    Connections {
        target: inventory
        function onListingReady(path, list) {
            if (path === wizard.currentPath) {
                wizard.entries = list;
                for (var i = 0; i < list.length; ++i) inventory.requestSize(list[i].path);
            }
            wizard.connected = true;
        }
        function onSizeReady(path, bytes) {
            var m = wizard.sizeMap; m[path] = bytes; wizard.sizeMap = m;
        }
        function onServicesReady(list) { wizard.services = list; }
        function onApplicationsReady(list) { wizard.applications = list; wizard.connected = true; }
        function onEnginesReady(list) { wizard.engines = list; }
        function onDatabasesReady(engine, dbs) { wizard.databases = dbs; }
        function onTablesReady(db, tables) { var m = wizard.tablesByDb; m[db] = tables; wizard.tablesByDb = m; }
        function onError(message) {
            errorBanner.text = (message && message.length)
                ? message
                : qsTr("Discovery failed. Check the connection details and try again.");
            errorBanner.visible = true;
        }
        function onRefreshed() { errorBanner.visible = false; }
    }

    function parentOf(path) {
        if (path === "/" || path.length === 0) return "/";
        var t = path.replace(/\/+$/, ""); var i = t.lastIndexOf("/");
        return i <= 0 ? "/" : t.substring(0, i);
    }

    // Filesystem navigation with back/forward history.
    property var navStack: ["/"]
    property int navIndex: 0
    readonly property bool canBack: navIndex > 0
    readonly property bool canForward: navIndex < navStack.length - 1

    function _goto(path) { wizard.currentPath = path; wizard.entries = []; inventory.browse(path); }
    function navigate(path) {
        if (path === wizard.currentPath) return;
        var s = wizard.navStack.slice(0, wizard.navIndex + 1);
        s.push(path); wizard.navStack = s; wizard.navIndex = s.length - 1;
        _goto(path);
    }
    function goBack() { if (wizard.canBack) { wizard.navIndex--; _goto(wizard.navStack[wizard.navIndex]); } }
    function goForward() { if (wizard.canForward) { wizard.navIndex++; _goto(wizard.navStack[wizard.navIndex]); } }
    function refreshDir() { inventory.invalidate(wizard.currentPath); wizard.entries = []; inventory.browse(wizard.currentPath); }
    function breadcrumbs() {
        var parts = wizard.currentPath.split("/").filter(function (x) { return x.length > 0; });
        var crumbs = [{ name: "Computer", path: "/" }];
        var acc = "";
        for (var i = 0; i < parts.length; ++i) { acc += "/" + parts[i]; crumbs.push({ name: parts[i], path: acc }); }
        return crumbs;
    }

    function toggleInArray(arr, value) {
        var a = arr.slice(); var i = a.indexOf(value);
        if (i >= 0) a.splice(i, 1); else a.push(value);
        return a;
    }
    function isSelectedPath(p) { return selectedPaths.indexOf(p) >= 0; }
    function togglePath(p) { selectedPaths = toggleInArray(selectedPaths, p); }
    function isSelectedService(id) { return selectedServices.indexOf(id) >= 0; }
    function toggleService(id) { selectedServices = toggleInArray(selectedServices, id); }
    function isSelectedApp(root) { return selectedAppRoots.indexOf(root) >= 0; }
    function toggleApp(root) { selectedAppRoots = toggleInArray(selectedAppRoots, root); }
    function isSelectedDb(db) { return selectedDatabases.indexOf(db) >= 0; }
    function toggleDb(db) { selectedDatabases = toggleInArray(selectedDatabases, db); }
    function allDbsSelected() { return databases.length > 0 && selectedDatabases.length === databases.length; }
    function toggleAllDbs() { selectedDatabases = allDbsSelected() ? [] : databases.slice(); }
    // "all" when nothing/everything is picked; otherwise the custom subset.
    function databasesArg() {
        return (selectedDatabases.length === 0 || allDbsSelected()) ? "all" : selectedDatabases.join(",");
    }
    function chooseEngine(engine) {
        wizard.selectedEngine = (wizard.selectedEngine === engine) ? "" : engine;
        wizard.databases = []; wizard.selectedDatabases = []; wizard.expandedDb = ""; wizard.tablesByDb = ({});
        if (wizard.selectedEngine.length) inventory.loadDatabases(wizard.selectedEngine);
    }
    function toggleExpandDb(db) {
        if (wizard.expandedDb === db) { wizard.expandedDb = ""; return; }
        wizard.expandedDb = db;
        if (wizard.tablesByDb[db] === undefined) inventory.loadTables(wizard.selectedEngine, db);
    }

    // App database inclusion (default ON; track explicit exclusions).
    property var appDbExcluded: []
    function isAppDbIncluded(root) { return appDbExcluded.indexOf(root) < 0; }
    function setAppDb(root, included) {
        var a = appDbExcluded.slice(); var i = a.indexOf(root);
        if (included) { if (i >= 0) a.splice(i, 1); } else { if (i < 0) a.push(root); }
        appDbExcluded = a;
    }

    readonly property int selectionCount: selectedAppRoots.length + selectedPaths.length
        + selectedServices.length + (selectedEngine.length ? 1 : 0)

    property string savedServerId: ""   // when set, use a saved server instead of typed fields

    // Called from Main when launching the wizard for a specific saved server.
    function applyServer(id) {
        wizard.savedServerId = id;
        if (id.length > 0) {
            wizard.targetIndex = 1;
            wizard.connected = false;
        }
    }

    function targetFields() {
        var f = {};
        if (wizard.remote) {
            if (wizard.savedServerId.length > 0 && wizard.serversVm) {
                return wizard.serversVm.connectionFields(wizard.savedServerId);
            }
            f["EXECUTION_MODE"] = "remote_ssh";
            f["REMOTE_HOST"] = hostField.text;
            f["REMOTE_PORT"] = portField.text.length ? portField.text : "22";
            f["REMOTE_USER"] = userField.text;
            f["REMOTE_AUTH_METHOD"] = authCombo.currentText;
            if (authCombo.currentText === "key") {
                f["REMOTE_IDENTITY_FILE"] = identityField.text;
                f["REMOTE_IDENTITY_PASSPHRASE"] = passphraseField.text;
            } else {
                f["REMOTE_PASSWORD"] = passwordField.text;
            }
        } else {
            f["EXECUTION_MODE"] = "local";
        }
        return f;
    }

    function scan() {
        errorBanner.visible = false;
        if (wizard.remote && wizard.savedServerId.length > 0 && wizard.serversVm) {
            wizard.serversVm.markUsed(wizard.savedServerId);
        }
        var path = wizard.controller.writeTempConfig(targetFields(), "scan");
        if (!path.length) return;
        inventory.setTarget(path);
        wizard.selectedPaths = []; wizard.selectedAppRoots = [];
        wizard.currentPath = "/"; wizard.entries = []; wizard.sizeMap = ({});
        wizard.navStack = ["/"]; wizard.navIndex = 0;
        inventory.loadApplications();
        inventory.loadServices();
        inventory.loadDatabaseEngines();
        inventory.browse("/");
    }

    function baseFields() {
        var f = targetFields();
        f["BACKUP_DIR"] = wizard.destDir;
        if (wizard.remote) { f["REMOTE_DOWNLOAD_DIR"] = wizard.destDir; f["REMOTE_SERVER_BACKUP_DIR"] = "/tmp/vaultium_remote_backups"; }
        f["BACKUP_COMPRESS"] = compressSwitch.checked;
        f["BACKUP_CHECKSUM"] = checksumSwitch.checked;
        f["BACKUP_CLEANUP_ENABLED"] = true;
        f["BACKUP_RETENTION_DAYS"] = "7";
        f["BACKUP_INTERVAL_MINUTES"] = "1440";
        f["LOCK_FILE"] = "/tmp/vaultium.lock";
        f["TAR_PATH"] = "/usr/bin/tar";
        f["GZIP_PATH"] = "/usr/bin/gzip";
        return f;
    }

    function finish() {
        // Compile the multi-source selection into one config per source type.
        var fsPaths = wizard.selectedAppRoots.slice();
        for (var i = 0; i < wizard.selectedPaths.length; ++i)
            if (fsPaths.indexOf(wizard.selectedPaths[i]) < 0) fsPaths.push(wizard.selectedPaths[i]);

        var configs = [];
        var label = (wizard.remote ? hostField.text : "This computer");

        if (fsPaths.length) {
            var f = baseFields(); f["BACKUP_SOURCE"] = "filesystem"; f["BACKUP_PATHS"] = fsPaths.join(",");
            var p1 = wizard.controller.writeTempConfig(f, "files");
            configs.push(p1); wizard.jobsModel.addJob(label + " · files", "filesystem", p1);
        }
        if (wizard.selectedServices.length) {
            var s = baseFields(); s["BACKUP_SOURCE"] = "service-config"; s["BACKUP_SERVICES"] = wizard.selectedServices.join(",");
            var p2 = wizard.controller.writeTempConfig(s, "services");
            configs.push(p2); wizard.jobsModel.addJob(label + " · services", "service-config", p2);
        }
        // Database: explicit engine selection, or auto-included from a selected
        // application that has a database and a detected engine.
        var engine = wizard.selectedEngine;
        if (!engine.length && wizard.engines.length) {
            for (var j = 0; j < wizard.applications.length; ++j) {
                var a = wizard.applications[j];
                if (wizard.isSelectedApp(a.root) && a.usesDatabase && wizard.isAppDbIncluded(a.root)) {
                    engine = wizard.engines[0];
                    break;
                }
            }
        }
        if (engine.length) {
            var d = baseFields(); d["BACKUP_SOURCE"] = "database"; d["BACKUP_ENGINE"] = engine;
            d["BACKUP_DATABASES"] = wizard.databasesArg();
            var p3 = wizard.controller.writeTempConfig(d, "database");
            configs.push(p3); wizard.jobsModel.addJob(label + " · database", "database", p3);
        }

        if (configs.length) wizard.controller.runPlan(configs);
        wizard.resetForReuse();
        wizard.done();
    }

    // Returns the wizard to a fresh state (keeps the discovered server) so the
    // user can immediately start another backup.
    function resetForReuse() {
        wizard.step = 0;
        wizard.sourceIndex = 0;
        wizard.selectedPaths = [];
        wizard.selectedAppRoots = [];
        wizard.selectedServices = [];
        wizard.selectedEngine = "";
        wizard.savedServerId = "";
    }

    readonly property bool canAdvance: {
        if (step === 0) return connected;
        if (step === 1) return selectionCount > 0;
        if (step === 2) return destDir.length > 0;
        return true;
    }

    // Small reusable checkbox.
    component CheckBox: Rectangle {
        property bool checked: false
        signal toggled()
        implicitWidth: 20; implicitHeight: 20; radius: Theme.radiusSm - 2
        color: checked ? Theme.accent : "transparent"
        border.width: checked ? 0 : 1; border.color: Theme.borderStrong
        AppIcon { anchors.centerIn: parent; visible: parent.checked; name: "check"; size: 14; color: Theme.textOnAccent }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.toggled() }
    }

    WizardShell {
        id: shell
        anchors.fill: parent
        title: qsTr("Create a backup")
        steps: [qsTr("Server"), qsTr("Choose data"), qsTr("Destination"), qsTr("Review")]
        stepIcons: ["server", "jobs", "download", "check-circle"]
        stepDescriptions: [
            qsTr("Pick this computer or connect a remote server over SSH."),
            qsTr("Choose applications, files, databases or services to protect."),
            qsTr("Decide where the backup is saved."),
            qsTr("Confirm and run the backup.")
        ]
        currentStep: wizard.step
        canAdvance: wizard.canAdvance
        busy: wizard.controller && wizard.controller.busy
        onCancel: { wizard.resetForReuse(); wizard.done(); }
        onBack: if (wizard.step > 0) wizard.step--
        onNext: { if (wizard.step < 3) wizard.step++; else wizard.finish(); }

        StackLayout {
            anchors.fill: parent
            currentIndex: wizard.step

            // ===== Step 0: Server ============================================
            ScrollView {
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: shell.width - 240
                    spacing: Theme.space4
                    Item { implicitHeight: Theme.space5; Layout.fillWidth: true }

                    ColumnLayout {
                        Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6; spacing: Theme.space2
                        Text { text: qsTr("Where is the data?"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLg; font.weight: Font.Bold }
                        SegmentedControl {
                            options: [qsTr("This computer"), qsTr("Remote server (SSH)")]
                            currentIndex: wizard.targetIndex
                            onActivated: (i) => { wizard.targetIndex = i; wizard.connected = false; }
                        }
                    }

                    // Saved-server picker — choose a profile instead of re-typing.
                    Card {
                        visible: wizard.remote && wizard.serversVm && wizard.serversVm.servers.length > 0
                        Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                        RowLayout {
                            anchors.fill: parent; spacing: Theme.space3
                            AppIcon { name: "server"; size: 18; color: Theme.accent }
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                Text { text: qsTr("Saved server"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.Medium }
                                Text { text: qsTr("Reuse a profile from Servers — no need to re-enter details"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                            }
                            AppComboBox {
                                id: serverPicker
                                Layout.preferredWidth: 240
                                readonly property var ids: {
                                    var a = [""];
                                    if (wizard.serversVm) for (var i = 0; i < wizard.serversVm.servers.length; ++i) a.push(wizard.serversVm.servers[i].id);
                                    return a;
                                }
                                model: {
                                    var a = [qsTr("Manual entry")];
                                    if (wizard.serversVm) for (var i = 0; i < wizard.serversVm.servers.length; ++i) a.push(wizard.serversVm.servers[i].name);
                                    return a;
                                }
                                currentIndex: Math.max(0, ids.indexOf(wizard.savedServerId))
                                onActivated: (i) => { wizard.savedServerId = serverPicker.ids[i]; wizard.connected = false; }
                            }
                        }
                    }

                    Card {
                        visible: wizard.remote && wizard.savedServerId.length === 0
                        Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                        GridLayout {
                            anchors.fill: parent; columns: 2; columnSpacing: Theme.space5; rowSpacing: Theme.space3
                            ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Host"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: hostField; Layout.fillWidth: true; placeholderText: qsTr("server.example.com"); onTextChanged: wizard.connected = false }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Port"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: portField; Layout.fillWidth: true; text: "22" }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("User"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: userField; Layout.fillWidth: true; placeholderText: qsTr("root"); onTextChanged: wizard.connected = false }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Authentication"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppComboBox { id: authCombo; Layout.fillWidth: true; model: ["key", "password"] }
                            }
                            ColumnLayout { visible: authCombo.currentText === "key"; Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Private key file"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                RowLayout { Layout.fillWidth: true; spacing: Theme.space2
                                    AppTextField { id: identityField; Layout.fillWidth: true; placeholderText: qsTr("~/.ssh/id_ed25519") }
                                    AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: keyDialog.open() }
                                }
                            }
                            ColumnLayout { visible: authCombo.currentText === "key"; Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Key passphrase"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: passphraseField; Layout.fillWidth: true; echoMode: TextInput.Password }
                            }
                            ColumnLayout { visible: authCombo.currentText === "password"; Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Theme.space2
                                Text { text: qsTr("Password"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                AppTextField { id: passwordField; Layout.fillWidth: true; echoMode: TextInput.Password }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6; spacing: Theme.space3
                        AppButton {
                            text: wizard.connected ? qsTr("Re-scan") : qsTr("Connect & scan")
                            iconName: "search"; variant: "primary"
                            loading: inventory.busy && !wizard.connected
                            enabled: (!wizard.remote || wizard.savedServerId.length > 0 || (hostField.text.length && userField.text.length)) && !inventory.busy
                            onClicked: wizard.scan()
                        }
                        StatusPill { visible: wizard.connected; tone: "success"; text: qsTr("Scanned") }
                        Item { Layout.fillWidth: true }
                        AppButton { visible: wizard.advanced; text: qsTr("Manual setup"); variant: "ghost"; onClicked: wizard.manualMode() }
                    }

                    Banner { id: errorBanner; visible: false; tone: "danger"; Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6 }
                    Banner { visible: !wizard.connected; tone: "info"; Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                        text: qsTr("Connect to scan the machine. Vaultium finds your applications, files, databases and services so you can pick with checkboxes — no paths to type.") }
                    Item { Layout.fillHeight: true }
                }
            }

            // ===== Step 1: Choose data =======================================
            ColumnLayout {
                spacing: Theme.space3
                Item { implicitHeight: Theme.space5; Layout.fillWidth: true }

                RowLayout {
                    Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6; Layout.fillWidth: true
                    SegmentedControl {
                        options: [qsTr("Applications"), qsTr("Files"), qsTr("Databases"), qsTr("Services")]
                        currentIndex: wizard.sourceIndex
                        onActivated: (i) => wizard.sourceIndex = i
                    }
                    Item { Layout.fillWidth: true }
                    StatusPill { text: wizard.selectionCount + qsTr(" selected"); tone: wizard.selectionCount ? "success" : "neutral" }
                    IconButton { iconName: "history"; tip: qsTr("Refresh discovery"); onClicked: wizard.scan() }
                }

                // ---- Applications ----
                ScrollView {
                    id: appsScroll
                    visible: wizard.sourceIndex === 0
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: appsScroll.availableWidth
                        spacing: Theme.space2
                        Repeater {
                            model: wizard.applications
                            delegate: Card {
                                id: appCard
                                required property var modelData
                                readonly property bool appSelected: wizard.isSelectedApp(modelData.root)
                                Layout.fillWidth: true; padding: Theme.space4
                                ColumnLayout {
                                    anchors.fill: parent; spacing: Theme.space3
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: Theme.space3
                                        CheckBox { checked: appCard.appSelected; onToggled: wizard.toggleApp(appCard.modelData.root) }
                                        Rectangle { implicitWidth: 36; implicitHeight: 36; radius: Theme.radiusSm; color: Theme.accentSoft
                                            AppIcon { anchors.centerIn: parent; name: "folder"; size: 18; color: Theme.accent } }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                                            Text { text: appCard.modelData.name; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.DemiBold }
                                            Text { text: appCard.modelData.root; color: Theme.fgSubtle; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                        }
                                        StatusPill { visible: appCard.modelData.usesDatabase; tone: "info"; text: qsTr("has database") }
                                    }
                                    // Optional database inclusion (only when the app uses one and is selected).
                                    RowLayout {
                                        visible: appCard.modelData.usesDatabase && appCard.appSelected
                                        Layout.fillWidth: true; Layout.leftMargin: 32; spacing: Theme.space2
                                        AppSwitch {
                                            text: qsTr("Include database backup")
                                            checked: wizard.isAppDbIncluded(modelData.root)
                                            onToggled: wizard.setAppDb(modelData.root, checked)
                                        }
                                        Text { visible: wizard.engines.length === 0; text: qsTr("(no DB engine detected)"); color: Theme.warning; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                        Text { visible: wizard.applications.length === 0 && wizard.connected; text: qsTr("No applications detected. Try the Files tab to pick folders directly.")
                            color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
                    }
                }

                // ---- Files browser ----
                ColumnLayout {
                    visible: wizard.sourceIndex === 1
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6; spacing: Theme.space3
                    RowLayout { Layout.fillWidth: true; spacing: Theme.space1
                        IconButton { iconName: "back"; iconSize: 16; tip: qsTr("Back"); enabled: wizard.canBack; onClicked: wizard.goBack() }
                        IconButton { iconName: "forward"; iconSize: 16; tip: qsTr("Forward"); enabled: wizard.canForward; onClicked: wizard.goForward() }
                        IconButton { iconName: "up"; iconSize: 16; tip: qsTr("Up"); enabled: wizard.currentPath !== "/"; onClicked: wizard.navigate(wizard.parentOf(wizard.currentPath)) }
                        IconButton { iconName: "refresh"; iconSize: 16; tip: qsTr("Refresh"); onClicked: wizard.refreshDir() }

                        // Breadcrumb
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: 30
                            radius: Theme.radiusSm; color: Theme.bg; border.color: Theme.border; border.width: 1
                            clip: true
                            Flickable {
                                anchors.fill: parent; anchors.leftMargin: Theme.space3; anchors.rightMargin: Theme.space3
                                contentWidth: crumbRow.width; interactive: contentWidth > width; flickableDirection: Flickable.HorizontalFlick
                                Row {
                                    id: crumbRow
                                    height: parent.height; spacing: 2
                                    Repeater {
                                        model: wizard.breadcrumbs()
                                        delegate: Row {
                                            required property int index
                                            required property var modelData
                                            height: crumbRow.height; spacing: 2
                                            AppIcon { visible: index > 0; anchors.verticalCenter: parent.verticalCenter; name: "forward"; size: 12; color: Theme.fgSubtle }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: modelData.name
                                                color: index === wizard.breadcrumbs().length - 1 ? Theme.fg : Theme.info
                                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm
                                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: wizard.navigate(modelData.path) }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Card {
                        Layout.fillWidth: true; Layout.fillHeight: true; padding: Theme.space2
                        ColumnLayout { anchors.centerIn: parent; visible: inventory.busy && wizard.entries.length === 0; spacing: Theme.space2
                            Repeater { model: 5; delegate: Skeleton { implicitWidth: 360; implicitHeight: 18 } } }
                        ListView {
                            anchors.fill: parent; visible: wizard.entries.length > 0; model: wizard.entries; clip: true; spacing: 1
                            delegate: Rectangle {
                                id: fileRow
                                required property var modelData
                                readonly property bool sel: wizard.isSelectedPath(modelData.path)
                                width: ListView.view ? ListView.view.width : 0; implicitHeight: 40; radius: Theme.radiusSm
                                color: sel ? Theme.accentSoft : rowHover.hovered ? Theme.muted : "transparent"
                                border.width: sel ? 1 : 0
                                border.color: Theme.accent
                                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                                HoverHandler { id: rowHover }
                                // Double-click a folder to open it.
                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    onDoubleTapped: if (fileRow.modelData.isDir) wizard.navigate(fileRow.modelData.path)
                                }
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: Theme.space2; anchors.rightMargin: Theme.space3; spacing: Theme.space3
                                    CheckBox { checked: fileRow.sel; onToggled: wizard.togglePath(fileRow.modelData.path) }
                                    AppIcon { name: fileRow.modelData.isDir ? "folder" : "info"; size: 18; color: fileRow.modelData.isDir ? Theme.accent : Theme.fgMuted }
                                    Text { text: fileRow.modelData.name; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                    Text { text: wizard.humanSize(wizard.sizeMap[fileRow.modelData.path]); color: Theme.fgSubtle; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs }
                                    AppButton { visible: fileRow.modelData.isDir; text: qsTr("Open"); variant: "ghost"; onClicked: wizard.navigate(fileRow.modelData.path) }
                                }
                            }
                        }
                        EmptyState {
                            anchors.centerIn: parent
                            width: parent.width
                            visible: !inventory.busy && wizard.entries.length === 0
                            iconName: "folder"
                            title: qsTr("This folder is empty")
                            body: qsTr("There's nothing here, or it can't be read with the current account. Use the breadcrumb or Up to go back — you can still tick a folder above to back up everything inside it.")
                        }
                    }
                }

                // ---- Databases ----
                ScrollView {
                    id: dbScroll
                    visible: wizard.sourceIndex === 2
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: dbScroll.availableWidth
                        spacing: Theme.space3

                        Text { text: qsTr("Engine"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                        Repeater {
                            model: wizard.engines
                            delegate: Card {
                                required property var modelData
                                Layout.fillWidth: true; padding: Theme.space4
                                RowLayout { anchors.fill: parent; spacing: Theme.space3
                                    Rectangle { implicitWidth: 18; implicitHeight: 18; radius: 9
                                        color: wizard.selectedEngine === modelData ? Theme.accent : "transparent"
                                        border.width: wizard.selectedEngine === modelData ? 0 : 1; border.color: Theme.borderStrong
                                        AppIcon { anchors.centerIn: parent; visible: wizard.selectedEngine === modelData; name: "check"; size: 12; color: Theme.textOnAccent } }
                                    AppIcon { name: "database"; size: 18; color: Theme.fgMuted }
                                    Text { text: modelData; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; Layout.fillWidth: true }
                                }
                                TapHandler { onTapped: wizard.chooseEngine(modelData) }
                            }
                        }
                        Text { visible: wizard.engines.length === 0 && wizard.connected; text: qsTr("No database engines detected."); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }

                        // Databases of the chosen engine: all, or pick specific ones.
                        ColumnLayout {
                            visible: wizard.selectedEngine.length > 0
                            Layout.fillWidth: true; spacing: Theme.space2
                            Layout.topMargin: Theme.space2

                            RowLayout {
                                Layout.fillWidth: true; spacing: Theme.space3
                                Text { text: qsTr("Databases (%1)").arg(wizard.databases.length); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                RowLayout {
                                    visible: wizard.databases.length > 0
                                    spacing: Theme.space2
                                    CheckBox { checked: wizard.allDbsSelected(); onToggled: wizard.toggleAllDbs() }
                                    Text { text: qsTr("Select all"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm }
                                }
                            }
                            Text {
                                visible: wizard.databases.length > 0 && wizard.selectedDatabases.length === 0
                                text: qsTr("Nothing selected — all databases will be backed up.")
                                color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                            }
                            Banner {
                                visible: wizard.databases.length === 0
                                Layout.fillWidth: true; tone: "info"
                                text: qsTr("Couldn't list databases (the SSH user may lack DB access). All databases will be backed up.")
                            }
                            Repeater {
                                model: wizard.databases
                                delegate: Card {
                                    required property var modelData
                                    Layout.fillWidth: true; padding: Theme.space3
                                    ColumnLayout {
                                        anchors.fill: parent; spacing: Theme.space2
                                        RowLayout { Layout.fillWidth: true; spacing: Theme.space3
                                            CheckBox { checked: wizard.isSelectedDb(modelData); onToggled: wizard.toggleDb(modelData) }
                                            AppIcon { name: "database"; size: 16; color: Theme.fgMuted }
                                            Text { text: modelData; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; Layout.fillWidth: true }
                                            IconButton {
                                                iconName: "chevron"
                                                iconColor: Theme.fgMuted
                                                rotation: wizard.expandedDb === modelData ? 180 : 0
                                                Behavior on rotation { NumberAnimation { duration: Theme.durFast } }
                                                onClicked: wizard.toggleExpandDb(modelData)
                                            }
                                        }
                                        ColumnLayout {
                                            visible: wizard.expandedDb === modelData
                                            Layout.fillWidth: true; Layout.leftMargin: Theme.space6; spacing: 1
                                            Repeater {
                                                model: wizard.tablesByDb[modelData] !== undefined ? wizard.tablesByDb[modelData] : []
                                                delegate: RowLayout {
                                                    required property var modelData
                                                    spacing: Theme.space2
                                                    AppIcon { name: "info"; size: 13; color: Theme.fgSubtle }
                                                    Text { text: modelData; color: Theme.fgMuted; font.family: Theme.monoFamily; font.pixelSize: Theme.fsXs }
                                                }
                                            }
                                            Text {
                                                visible: wizard.tablesByDb[modelData] === undefined || wizard.tablesByDb[modelData].length === 0
                                                text: wizard.tablesByDb[modelData] === undefined ? qsTr("Loading tables…") : qsTr("No tables")
                                                color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ---- Services ----
                ScrollView {
                    id: svcScroll
                    visible: wizard.sourceIndex === 3
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                    contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: svcScroll.availableWidth
                        spacing: Theme.space2
                        Repeater {
                            model: wizard.services
                            delegate: Card {
                                required property var modelData
                                Layout.fillWidth: true; padding: Theme.space4; enabled: modelData.present; opacity: modelData.present ? 1 : 0.5
                                RowLayout { anchors.fill: parent; spacing: Theme.space3
                                    CheckBox { checked: wizard.isSelectedService(modelData.id); onToggled: if (modelData.present) wizard.toggleService(modelData.id) }
                                    AppIcon { name: "server"; size: 18; color: Theme.fgMuted }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 1
                                        Text { text: modelData.name; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsBase; font.weight: Font.Medium }
                                        Text { text: modelData.present ? modelData.paths.join(", ") : qsTr("not installed"); color: Theme.fgSubtle; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs; Layout.fillWidth: true; elide: Text.ElideRight }
                                    }
                                    StatusPill { tone: modelData.present ? "success" : "neutral"; text: modelData.present ? qsTr("Detected") : qsTr("Absent") }
                                }
                            }
                        }
                    }
                }
            }

            // ===== Step 2: Destination =======================================
            ColumnLayout {
                spacing: Theme.space4
                Item { implicitHeight: Theme.space5; Layout.fillWidth: true }
                Card {
                    Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                    ColumnLayout { anchors.fill: parent; spacing: Theme.space4
                        Text { text: qsTr("Where should the backup be saved?"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                        ColumnLayout { Layout.fillWidth: true; spacing: Theme.space2
                            Text { text: wizard.remote ? qsTr("Download folder on this computer") : qsTr("Backup folder"); color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsXs }
                            RowLayout { Layout.fillWidth: true; spacing: Theme.space2
                                AppTextField { id: destField; Layout.fillWidth: true; text: wizard.destDir; placeholderText: qsTr("/Users/me/Backups"); onTextChanged: wizard.destDir = text }
                                AppButton { text: qsTr("Browse"); iconName: "folder"; variant: "secondary"; onClicked: destDialog.open() }
                            }
                        }
                        RowLayout { Layout.fillWidth: true; spacing: Theme.space5
                            AppSwitch { id: compressSwitch; text: qsTr("Compress"); checked: true }
                            AppSwitch { id: checksumSwitch; text: qsTr("Verify with checksum"); checked: true }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // ===== Step 3: Review ============================================
            ScrollView {
                contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: shell.width - 240; spacing: Theme.space4
                    Item { implicitHeight: Theme.space5; Layout.fillWidth: true }
                    Card {
                        Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6
                        ColumnLayout { anchors.fill: parent; spacing: Theme.space3
                            Text { text: qsTr("Review"); color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsMd; font.weight: Font.DemiBold }
                            component ReviewRow: RowLayout {
                                Layout.fillWidth: true; spacing: Theme.space3
                                property string k: ""; property string v: ""
                                Text { text: k; color: Theme.fgMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; Layout.preferredWidth: 130 }
                                Text { text: v; color: Theme.fg; font.family: Theme.fontFamily; font.pixelSize: Theme.fsSm; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                            ReviewRow { k: qsTr("Source"); v: wizard.remote ? (qsTr("Remote: ") + hostField.text) : qsTr("This computer") }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                            ReviewRow { visible: wizard.selectedAppRoots.length; k: qsTr("Applications"); v: wizard.selectedAppRoots.join(", ") }
                            ReviewRow { visible: wizard.selectedPaths.length; k: qsTr("Files"); v: wizard.selectedPaths.join(", ") }
                            ReviewRow { visible: wizard.selectedServices.length; k: qsTr("Services"); v: wizard.selectedServices.join(", ") }
                            ReviewRow { visible: wizard.selectedEngine.length; k: qsTr("Database"); v: wizard.selectedEngine + " — " + (wizard.databasesArg() === "all" ? qsTr("all databases") : wizard.selectedDatabases.join(", ")) }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
                            ReviewRow { k: qsTr("Destination"); v: wizard.destDir }
                            ReviewRow { k: qsTr("Options"); v: (compressSwitch.checked ? qsTr("Compressed") : qsTr("Uncompressed")) + (checksumSwitch.checked ? qsTr(", checksum") : "") }
                        }
                    }
                    Banner { Layout.fillWidth: true; Layout.leftMargin: Theme.space6; Layout.rightMargin: Theme.space6; tone: "info"
                        text: qsTr("Finish runs the selected backups in sequence and saves them as jobs. Watch progress in the Console below.") }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    FolderDialog { id: destDialog; title: qsTr("Choose destination folder"); onAccepted: wizard.destDir = wizard.controller.toLocalPath(selectedFolder.toString()) }
    FileDialog { id: keyDialog; title: qsTr("Select SSH private key"); onAccepted: identityField.text = wizard.controller.toLocalPath(selectedFile.toString()) }
}
