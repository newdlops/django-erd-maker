import type { DjangoWorkspaceDiscoveryResult } from "./discoveryTypes";
import { discoverApps } from "./discoverApps";
import { discoverCandidateModules } from "./discoverCandidateModules";
import { selectWorkspaceRoot } from "./selectWorkspaceRoot";

export async function discoverDjangoWorkspace(
  workspacePath: string,
): Promise<DjangoWorkspaceDiscoveryResult> {
  const rootSelection = await selectWorkspaceRoot(workspacePath);
  const appDiscovery = await discoverApps(rootSelection.selectedRoot);
  const candidateModules = await discoverCandidateModules(
    rootSelection.selectedRoot,
    appDiscovery.apps,
  );
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
    workspacePath,
  };
}
