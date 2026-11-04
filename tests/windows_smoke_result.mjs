import { cleanHelperLog } from './windows_helper_log.mjs';

// Keep the concise result display separate from the full lifecycle evidence.
export function caseResult(spec, cacheDir, outcome) {
  return { case: spec.name, cacheDir, observation: outcome.observation,
    helperExit: outcome.closed?.code ?? null, helperSignal: outcome.closed?.signal ?? null,
    spawnError: outcome.closed?.spawnError, log: '' };
}

export function finalizeCase(spec, result, fullLog, mode) {
  result.log = fullLog.split(/\r?\n/).filter(line =>
    /app pid|app wait|job active|window actions|window bounds|Failed to create temporary log|ERROR:/.test(line)).join('\n');
  result.failures = checkCase(spec, result, fullLog, mode);
  return result;
}

function checkCase(spec, result, fullLog, mode) {
  const failures = [];
  const { observation, helperExit } = result;
  if (!cleanHelperLog(fullLog, helperExit)) failures.push('unclean exit');
  if (observation.error) return [...failures, observation.error];
  if (observation.cleanupError || result.spawnError || result.helperSignal)
    failures.push(observation.cleanupError || result.spawnError || `helper signal ${result.helperSignal}`);
  if (observation.actionError) failures.push(observation.actionError);
  const { page, gpu, exitRequested } = observation;
  if (page.title !== 'Jellyfin' || !page.text.includes('Server Address') ||
      !page.text.includes('CONNECT') || !exitRequested) failures.push('initial page or native exit');
  const disabled = spec.arg === '--disable-gpu' || spec.flags === '--disable-gpu';
  if (mode !== '--baseline' && gpu.featureStatus.gpu_compositing !== (disabled ? 'disabled_software' : 'enabled'))
    failures.push('unexpected GPU compositing');
  if (mode !== '--baseline' && disabled && !gpu.commandLine.includes('--disable-gpu')) failures.push('missing disable-gpu');
  if (mode === '--baseline' && (gpu.featureStatus.gpu_compositing !== 'enabled' ||
      (disabled && gpu.commandLine.includes('--disable-gpu')))) failures.push('unexpected baseline backend');
  if (spec.disablePinch && !gpu.commandLine.includes('--disable-pinch')) failures.push('missing disable-pinch');
  if (spec.name === 'quoted-flags' && !gpu.commandLine.includes('--user-agent="Desktop Smoke Test"'))
    failures.push('quoted argument not preserved');
  if (spec.logFailure && !fullLog.includes('Failed to create temporary log file')) failures.push('missing log failure diagnostic');
  return failures;
}
