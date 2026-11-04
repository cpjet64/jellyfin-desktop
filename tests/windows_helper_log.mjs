export function cleanHelperLog(log, helperExit) {
  const markers = [/^app wait 0 exit 0$/gm, /^waitResult 0 waitError 0 exitKnown true exitError 0$/gm,
    /^activeBeforeKnown true activeBefore 0 activeBeforeError 0$/gm,
    /^terminationRequested false terminationSucceeded false terminationError 0$/gm,
    /^activeAfterKnown true activeAfter 0 activeAfterError 0 overall pass$/gm,
    /^forcedCleanup false; job active processes 0$/gm];
  const normalizedLog = log.replace(/\r\n/g, '\n');
  const prefixes = ['app wait ', 'waitResult ', 'activeBeforeKnown ', 'terminationRequested ',
    'activeAfterKnown ', 'forcedCleanup '];
  return helperExit === 0 && markers.every(marker => [...normalizedLog.matchAll(marker)].length === 1) &&
    prefixes.every(prefix => normalizedLog.split('\n').filter(line => line.startsWith(prefix)).length === 1);
}
