import type {
  DiscoveredCandidateModule,
  DiscoveredDjangoApp,
} from "./discoveryTypes";

export async function discoverCandidateModules(
  _selectedRoot: string,
  apps: DiscoveredDjangoApp[],
): Promise<DiscoveredCandidateModule[]> {
  const modules = apps.flatMap((app) =>
    app.candidateModelFiles.map(
      (filePath) =>
        ({
          appLabel: app.appLabel,
          filePath,
        }) satisfies DiscoveredCandidateModule,
    ),
  );

  return dedupeAndSort(modules);
}

function dedupeAndSort(
  modules: DiscoveredCandidateModule[],
): DiscoveredCandidateModule[] {
  const seen = new Set<string>();
  const deduped = modules.filter((module) => {
    const key = `${module.appLabel}:${module.filePath}`;
    if (seen.has(key)) {
      return false;
    }

    seen.add(key);
    return true;
  });

  deduped.sort((left, right) => {
    const leftKey = `${left.appLabel}:${left.filePath}`;
    const rightKey = `${right.appLabel}:${right.filePath}`;
    return leftKey.localeCompare(rightKey);
  });

  return deduped;
}
