.pragma library

function cutOff(view, guard, status, exitCode) {
    view.webChannel = null;
    guard.observeRendererLoss(status, exitCode);
}
