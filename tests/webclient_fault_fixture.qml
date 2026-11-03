import QtQuick
import QtWebEngine
import QtWebChannel

Item {
    id: root
    width: 320; height: 240
    property int loads: 0
    property bool bridgeReady: false
    property string result: ""
    property bool channelPresent: loader.item ? loader.item.webChannel !== null : false

    WebChannel {
        id: channel
        Component.onCompleted: registerObject("input", fixtureInput)
    }
    WebEngineScript {
        id: channelScript
        name: "fixture-channel"
        sourceUrl: "qrc:///qtwebchannel/qwebchannel.js"
        injectionPoint: WebEngineScript.DocumentCreation
        worldId: WebEngineScript.MainWorld
    }
    WebEngineProfile {
        id: fixtureProfile
        offTheRecord: true
        userScripts.collection: [channelScript]
    }
    Component {
        id: webComponent
        WebEngineView {
            webChannel: webClientFaultGuard.rendererLost ? null : channel
            profile: fixtureProfile
            onLoadingChanged: function(info) {
                if (info.status === WebEngineView.LoadSucceededStatus) root.loads++
            }
        }
    }
    Loader { id: loader; anchors.fill: parent; sourceComponent: webComponent }

    function loadPage() { loader.item.url = "data:text/html,<html><body>fixture</body></html>" }
    function reloadPage() { loader.item.reload() }
    function connectBridge() {
        loader.item.runJavaScript("new QWebChannel(qt.webChannelTransport, function(c) { window.fixtureInput = c.objects.input; window.fixtureReady = true; });")
    }
    function checkReady() {
        loader.item.runJavaScript("!!window.fixtureReady", function(value) { root.bridgeReady = !!value })
    }
    function evaluate(code) {
        root.result = "pending"
        loader.item.runJavaScript(code, function(value) { root.result = String(value) })
    }
    function loseRenderer() {
        loader.item.webChannel = null
        webClientFaultGuard.observeRendererLoss(0, 17)
    }
    function recreate() { loader.sourceComponent = null; loader.sourceComponent = webComponent }
}
