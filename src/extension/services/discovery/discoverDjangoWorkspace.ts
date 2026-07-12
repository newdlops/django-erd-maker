import type { DjangoWorkspaceDiscoveryResult } from "./discoveryTypes";
import { discoverApps } from "./discoverApps";
import { discoverCandidateModules } from "./discoverCandidateModules";
import { selectWorkspaceRoot } from "./selectWorkspaceRoot";

export async function discoverDjangoWorkspace(
  workspacePath: string,
): Promise<DjangoWorkspaceDiscoveryResult> {
  const rootSelectionStart = Date.now();
  const rootSelection = await selectWorkspaceRoot(workspacePath);
  const rootSelectionMs = Date.now() - rootSelectionStart;
  const appDiscoveryStart = Date.now();
  const appDiscovery = await discoverApps(rootSelection.selectedRoot);
  const appDiscoveryMs = Date.now() - appDiscoveryStart;
  const candidateModulesStart = Date.now();
  const candidateModules = await discoverCandidateModules(
    rootSelection.selectedRoot,
    appDiscovery.apps,
  );
  const candidateModulesMs = Date.now() - candidateModulesStart;
  const candidateModelFiles = appDiscovery.apps.flatMap(
    (app) => app.candidateModelFiles,
  );

  return {
    apps: appDiscovery.apps,
    candidateModules,
    candidateModelFiles,
    diagnostics: [...rootSelection.diagnostics, ...appDiscovery.diagnostics],
    selectedRoot: rootSelection.selectedRoot,
    strategy: rootSelection.strategy,
    timings: {
      appDiscoveryMs,
      candidateModulesMs,
      rootSelectionMs,
    },
    workspacePath,
  };
}
