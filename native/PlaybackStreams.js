(function() {
    function byIndex(streams, index) {
        for (const stream of streams) {
            if (stream.Index == index) return stream;
        }
        return null;
    }

    function relativeIndex(streams, index, type) {
        let relative = 1;
        for (const stream of streams) {
            if (stream.Type != type || stream.IsExternal) continue;
            if (stream.Index == index) return relative;
            relative += 1;
        }
        return null;
    }

    window._PlaybackStreams = Object.freeze({ byIndex, relativeIndex });
})();
