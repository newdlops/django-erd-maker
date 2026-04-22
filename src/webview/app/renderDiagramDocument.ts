import type { DjangoWorkspaceDiscoveryResult } from "../../shared/protocol/discoveryContract";
import type { DiagramBootstrapPayload } from "../../shared/protocol/webviewContract";
import { getBrowserControllerScript } from "../interaction/browserController";
import { serializeJsonForScriptTag } from "../render/escapeHtml";
import { renderCanvasScene } from "../render/renderCanvasScene";
import { renderInspector } from "../render/renderInspector";
import { createDiagramRenderModel } from "../state/createDiagramRenderModel";
import { createDiagramInteractionState } from "../state/diagramInteractionState";
import { getDocumentStyles } from "../styles/documentStyles";

export function renderDiagramDocument(
  payload: DiagramBootstrapPayload,
  discovery?: DjangoWorkspaceDiscoveryResult,
): string {
  const viewModel = createDiagramRenderModel(payload, discovery);
  const initialStateJson = serializeJsonForScriptTag(
    createDiagramInteractionState(payload.view),
  );
  const nonce = createNonce();

  return `<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';" />
    <title>Django ERD</title>
    <style>${getDocumentStyles()}</style>
  </head>
  <body>
    <main
      class="erd-shell"
      data-erd-root
    >
      <script id="erd-initial-state" type="application/json">${initialStateJson}</script>
      ${renderInspector(viewModel)}
      ${renderCanvasScene(viewModel)}
    </main>
    ${getBrowserControllerScript(nonce)}
  </body>
</html>`;
}

function createNonce(): string {
  return Math.random().toString(36).slice(2) + Math.random().toString(36).slice(2);
}
