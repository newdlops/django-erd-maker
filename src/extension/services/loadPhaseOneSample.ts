import phaseOneDiagramFixture from "../../shared/protocol/fixtures/phase1-diagram.json";
import { decodeDiagramBootstrapPayload } from "../../shared/protocol/decodeDiagramBootstrap";
import type { DiagramBootstrapPayload } from "../../shared/protocol/webviewContract";

export function loadPhaseOneSample(): DiagramBootstrapPayload {
  return decodeDiagramBootstrapPayload(phaseOneDiagramFixture);
}
