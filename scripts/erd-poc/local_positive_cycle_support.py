"""Local observation vocabulary; the neural critic alone selects geometry."""
from itertools import combinations
import numpy as np


def local_cycles(decoder, nodes, measured_cycles, seed, count):
    assert 1<=count<=4096
    active = np.any(nodes[:, 4:6]>0, axis=1)
    excluded = {tuple(sorted(map(int, word))) for word in measured_cycles}
    triples = set(); neighborhood_words = set()
    for group in decoder.groups:
        if len(group)<3:
            continue
        for owner in group:
            other = group[group!=owner]
            distance = np.sum((decoder.positions[other]-decoder.positions[owner])**2, axis=1)
            # Quantization is only a deterministic observation tie-break.
            distance = np.rint(distance*1e6)
            order = np.lexsort((other, distance)); nearby = other[order[:8]]
            for left, right in combinations(map(int, nearby), 2):
                word = tuple(sorted((int(owner), left, right)))
                neighborhood_words.add(word)
                if word in excluded or active[list(word)].sum()<2 or decoder.irrelevant_isolate[list(word)].all():
                    continue
                triples.add(word)
    words = np.array(sorted(triples), dtype=np.int32); assert len(words)>0
    requested = min(count, len(words)); rng = np.random.default_rng(seed)
    selected = words[rng.choice(len(words), requested, replace=False)]
    vocabulary = np.empty((2*requested, 3), dtype=np.int32)
    vocabulary[::2] = selected; vocabulary[1::2] = selected[:, [0, 2, 1]]
    assert (active[vocabulary].sum(1)>=2).all()
    return vocabulary, {'observationVocabulary': 'triangles from each owner and two of its eight nearest equal-size cards',
        'neighborhoodWordsBeforeGates': len(neighborhood_words), 'remainingEligibleLocalTriples': len(words),
        'sampledUnorderedTriples': requested, 'cycles': len(vocabulary),
        'nearestSameSizeNeighbors': 8, 'minDirectlyConflictingOwners': 2,
        'bothCycleDirections': True, 'previouslyMeasuredCyclesSubmitted': 0,
        'distancesUseSourceGeometryOnly': True, 'nativeMetricBasedSupportSelection': False,
        'coordinatesProposedBySupportBuilder': False}
