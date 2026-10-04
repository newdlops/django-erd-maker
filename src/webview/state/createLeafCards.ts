import type { LeafBundle, Point, Size } from "../../shared/graph/layoutContract";
import type { ModelId } from "../../shared/domain/modelIdentity";

interface LeafCardTable {
  appLabel: string;
  modelId: ModelId;
  modelName: string;
  position: Point;
  size: Size;
}

export interface LeafCardRenderModel {
  appLabel: string;
  id: string;
  label: string;
  memberModelIds: ModelId[];
  parentModelId: ModelId;
  padding: number;
}

/** Visible group cards. Model identities remain distinct from their shared connection endpoints. */
export function createLeafCards(
  bundles: LeafBundle[],
  tables: LeafCardTable[],
): LeafCardRenderModel[] {
  const tableById = new Map(tables.map((table) => [table.modelId, table]));
  const owners = new Map<ModelId, number>();
  for (const bundle of bundles) {
    for (const id of bundle.leafModelIds) owners.set(id, (owners.get(id) ?? 0) + 1);
  }
  return bundles.flatMap((bundle, index) => {
    const parent = tableById.get(bundle.parentModelId);
    const ids = bundle.leafModelIds;
    if (!parent || ids.length < 2 || ids.includes(parent.modelId)
      || ids.some((id) => !tableById.has(id) || owners.get(id) !== 1)) return [];
    const members = ids.map((id) => tableById.get(id)!);
    if (members.some((node) => node.size.width <= 0 || node.size.height <= 0)) return [];
    const padding = 24;
    const left = Math.min(...members.map((node) => node.position.x)) - padding;
    const top = Math.min(...members.map((node) => node.position.y)) - padding;
    const right = Math.max(...members.map((node) => node.position.x + node.size.width)) + padding;
    const bottom = Math.max(...members.map((node) => node.position.y + node.size.height)) + padding;
    const memberArea = members.reduce((sum, node) => sum + node.size.width * node.size.height, 0);
    // Historical metadata may refer to leaves that have since dispersed. Do
    // not draw a giant filled container over unrelated cards in that case.
    if (![left, top, right, bottom, memberArea].every(Number.isFinite)
      || memberArea <= 0 || (right - left) * (bottom - top) > memberArea * 6) return [];
    const memberSet = new Set(ids);
    if (tables.some((node) => !memberSet.has(node.modelId)
      && node.position.x < right && node.position.x + node.size.width > left
      && node.position.y < bottom && node.position.y + node.size.height > top)) return [];
    return [{
      appLabel: parent.appLabel,
      id: `leaf-card:${index}`,
      label: parent.modelName,
      memberModelIds: [...ids],
      parentModelId: parent.modelId,
      padding,
    }];
  });
}
