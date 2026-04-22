export type DiscoveryDiagnosticCode =
  | "app_without_model_modules"
  | "models_package_missing_init"
  | "multiple_manage_py_roots"
  | "no_django_apps_found"
  | "no_manage_py_found";

export type DiscoveryDiagnosticSeverity = "info" | "warning";

export type WorkspaceRootStrategy = "manage_py" | "workspace_fallback";

export interface DiscoveryDiagnostic {
  code: DiscoveryDiagnosticCode;
  message: string;
  severity: DiscoveryDiagnosticSeverity;
}

export interface DiscoveredDjangoApp {
  appLabel: string;
  appPath: string;
  candidateModelFiles: string[];
  hasAppConfig: boolean;
  hasModelsPackage: boolean;
  hasModelsPy: boolean;
}

export interface DiscoveredCandidateModule {
  appLabel: string;
  filePath: string;
}

export interface WorkspaceRootSelection {
  diagnostics: DiscoveryDiagnostic[];
  selectedRoot: string;
  strategy: WorkspaceRootStrategy;
}

export interface DjangoWorkspaceDiscoveryResult {
  apps: DiscoveredDjangoApp[];
  candidateModules: DiscoveredCandidateModule[];
  candidateModelFiles: string[];
  diagnostics: DiscoveryDiagnostic[];
  selectedRoot: string;
  strategy: WorkspaceRootStrategy;
  workspacePath: string;
}
