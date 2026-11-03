import QtQuick
import QtWebEngine
import QtWebChannel
import "../src/ui/RendererLoss.js" as RendererLoss

Item {
    id: root
    width: 320; height: 240
    property int loads: 0
    property bool bridgeReady: false
    property string result: ""
    property QtObject webChannel: webClientFaultGuard.rendererLost ? null : (loader.item ? loader.item.webChannel : null)

    WebChannel {
        id: channel
        Component.onCompleted: registerObject("input", fixtureInput)
    }
    WebEngineProfile {
        id: fixtureProfile
        offTheRecord: true
        Component.onCompleted: userScripts.collection = [{
            name: "fixture-channel",
            sourceUrl: "qrc:///qtwebchannel/qwebchannel.js",
            injectionPoint: WebEngineScript.DocumentCreation,
            worldId: WebEngineScript.MainWorld
        }]
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
    function transportCount() { return channel.transports.length }
    function reloadPage() { loader.item.reload() }
    function registerComponents(player, settings) {
        channel.registerObject("player", player)
        channel.registerObject("settings", settings)
    }
    function connectBridge() {
        loader.item.runJavaScript("new QWebChannel(qt.webChannelTransport, function(c) { window.fixtureInput = c.objects.input; window.fixturePlayer = c.objects.player; window.fixtureSettings = c.objects.settings; window.fixtureReady = true; });")
    }
    function checkReady() {
        loader.item.runJavaScript("!!window.fixtureReady", function(value) { root.bridgeReady = !!value })
    }
    function evaluate(code) {
        root.result = "pending"
        loader.item.runJavaScript(code, function(value) { root.result = String(value) })
    }
    function loseRenderer() {
        RendererLoss.cutOff(loader.item, webClientFaultGuard, 0, 17)
    }
    function recreate() { loader.sourceComponent = null; loader.sourceComponent = webComponent }
}
