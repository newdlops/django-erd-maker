import type { ExplorerModel, ExplorerRelationship } from "./relationshipExplorer";

interface RelatedLeafCard {id: string; label: string; memberModelIds: string[]}
interface RelatedOptions {page?: number; pageSize?: number; edgeId?: string}
export interface RelatedPeer {
  key: string;
  label: string;
  modelIds: string[];
  relationships: ExplorerRelationship[];
  grouped: boolean;
  available: boolean;
}
interface RelatedBox {x: number; y: number; width: number; height: number}

/** The local graph contains semantic relationships, never saved overview positions. */
export function createRelatedDiagramTools() {
  function build(model: ExplorerModel, relationships: ExplorerRelationship[], models: Map<string, ExplorerModel>, cards: RelatedLeafCard[], options: RelatedOptions = {}) {
    const owners = new Map<string, RelatedLeafCard | null>();
    for (const card of cards) for (const id of card.memberModelIds) owners.set(id, owners.has(id) ? null : card);
    const peersByKey = new Map<string, RelatedPeer>();
    const self: ExplorerRelationship[] = [];
    const seen = new Set<string>();
    for (const relationship of relationships) {
      if (seen.has(relationship.edgeId)) continue;
      seen.add(relationship.edgeId);
      if (relationship.otherModelId === model.modelId) {self.push(relationship); continue;}
      const other = relationship.otherModelId, card = owners.get(other);
      const key = card ? 'group:' + card.id : 'model:' + other;
      let peer = peersByKey.get(key);
      if (!peer) {
        peer = {key, label: card ? card.label + ' leaves' : models.get(other)?.modelName || other,
          modelIds: [], relationships: [], grouped: Boolean(card), available: models.has(other)};
        peersByKey.set(key, peer);
      }
      if (!peer.modelIds.includes(other)) peer.modelIds.push(other);
      peer.relationships.push(relationship);
      peer.available = peer.available && models.has(other);
    }
    const peers = [...peersByKey.values()].sort((a, b) => a.label.localeCompare(b.label) || a.key.localeCompare(b.key));
    const pageSize = Math.max(1, Math.floor(options.pageSize || 12));
    const pageCount = Math.max(1, Math.ceil(peers.length / pageSize));
    const edgeIndex = options.edgeId ? peers.findIndex(peer => peer.relationships.some(r => r.edgeId === options.edgeId)) : -1;
    const page = edgeIndex >= 0 ? Math.floor(edgeIndex / pageSize) : Math.max(0, Math.min(pageCount - 1, Math.floor(options.page || 0)));
    return {model, peers, self, page, pageSize, pageCount, relationshipCount: seen.size,
      modelCount: new Set(relationships.map(r => r.otherModelId).concat(model.modelId)).size,
      visiblePeers: peers.slice(page * pageSize, (page + 1) * pageSize)};
  }

  function direction(peer: RelatedPeer): string {
    const incoming = peer.relationships.some(r => r.direction === 'incoming');
    const outgoing = peer.relationships.some(r => r.direction === 'outgoing');
    const inheritance = peer.relationships.every(r => r.kind === 'inheritance');
    const mixedInheritance = !inheritance && peer.relationships.some(r => r.kind === 'inheritance');
    return incoming && outgoing ? 'Both directions' : inheritance
      ? incoming ? 'Inherited by' : 'Inherits from' : mixedInheritance
        ? incoming ? 'Referenced / inherited by' : 'References / inherits from' : incoming ? 'Referenced by' : 'References';
  }

  function color(peer: RelatedPeer): string {
    const kinds = [...new Set(peer.relationships.map(r => r.kind.replace('reverse_', '')))];
    if (kinds.length !== 1) return '#b6e7d9';
    return ({foreign_key: '#72e0b1', one_to_one: '#82bdff', many_to_many: '#ffd07a', inheritance: '#c6a4ff'} as Record<string, string>)[kinds[0]] || '#b6e7d9';
  }

  function links(peers: RelatedPeer[], boxes: Map<string, RelatedBox>, center: RelatedBox) {
    const sides = [peers.filter(p => (boxes.get(p.key)?.x ?? 0) < center.x), peers.filter(p => (boxes.get(p.key)?.x ?? 0) >= center.x)];
    return sides.flatMap((side, sideIndex) => side.slice().sort((a, b) => (boxes.get(a.key)?.y ?? 0) - (boxes.get(b.key)?.y ?? 0)).flatMap((peer, index) => {
      const box = boxes.get(peer.key);
      if (!box) return [];
      const from = {x: center.x + (sideIndex ? center.width : 0), y: center.y + center.height * (index + 1) / (side.length + 1)};
      const to = {x: box.x + (sideIndex ? 0 : box.width), y: box.y + box.height / 2};
      return [{key: peer.key, from, to, color: color(peer),
        towardCenter: peer.relationships.some(r => r.direction === 'incoming'),
        towardPeer: peer.relationships.some(r => r.direction === 'outgoing')}];
    }));
  }
  return {build, direction, color, links};
}
