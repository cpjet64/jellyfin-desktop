/* Test-only support for the real web player and QWebChannel player proxy. */
(function () {
    const errors = [];
    const actions = [];
    let overflow = false;
    function fail(value) {
        if (errors.length < 16) errors.push(String(value).slice(0, 512));
        else overflow = true;
    }
    let pending = 0;
    window.addEventListener('error', event => fail('error:' + String(event.message)));
    window.addEventListener('unhandledrejection', event => fail('rejection:' + String(event.reason)));
    window.api = { player: window.fixturePlayer };
    window.jmpInfo = { userAgent: 'fixture', settings: { video: { default_playback_speed: 1 } } };
    const player = new window._mpvVideoPlayer({
        events: { trigger(_target, name) { if (name === 'error') fail('player error'); } },
        loading: { show() {}, hide() {} }, appRouter: { showVideoOsd() {} },
        globalize: {}, appHost: {}, appSettings: { get() { return 1; }, set() {} },
        confirm() { fail('unexpected prompt'); return Promise.reject(new Error('unexpected prompt')); },
        dashboard: { default: { setBackdropTransparency() {} } }
    });
    function options(url) {
        return { url, fullscreen: false, item: { Id: url },
            mediaSource: { MediaStreams: [], DefaultAudioStreamIndex: -1 } };
    }
    function start(url) {
        ++pending;
        const result = player.play(options(url));
        result.then(() => { --pending; }, error => { --pending; fail('play:' + String(error)); });
    }
    window.fixtureStartA = function (url) { actions.push('playA'); start(url); return 'sent'; };
    window.fixturePlayBStop = function (url) {
        actions.push('playB'); start(url);
        actions.push('stop'); player.stop();
        return 'sent';
    };
    window.fixtureDispatchState = function () { return JSON.stringify({ errors, actions, pending, overflow }); };
}());
