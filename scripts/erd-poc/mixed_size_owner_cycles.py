"""Source observations and deterministic partial moves across card sizes."""
from itertools import combinations
import numpy as np


def mixed_action(decoder, cycle, amplitudes):
    cycle = np.asarray(cycle, dtype=np.int32); amplitudes = np.asarray(amplitudes, dtype=float)
    assert cycle.shape == amplitudes.shape == (3,) and len(set(map(int, cycle))) == 3
    assert np.isfinite(amplitudes).all() and (abs(amplitudes) <= .4).all()
    assert (cycle >= 0).all() and (cycle < len(decoder.positions)).all()
    delta = np.zeros_like(decoder.positions)
    delta[cycle] = amplitudes[:, None]*(decoder.positions[np.roll(cycle, -1)]-decoder.positions[cycle])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    phases, _ = decoder.endpoint_offsets(delta)
    return np.concatenate([delta, phases])


def mixed(word, sizes):
    return any(not np.array_equal(sizes[word[0]], sizes[n]) for n in word[1:])


def support(decoder, nodes, kind, seed, count):
    assert 1 <= count <= 1024 and kind in ['local', 'small-global']
    active = np.any(nodes[:, 4:6] > 0, axis=1); triples = set(); before = set()
    if kind == 'local':
        indices = np.arange(len(nodes))
        for owner in indices:
            other = indices[indices != owner]
            distance = np.rint(np.sum((decoder.positions[other]-decoder.positions[owner])**2, axis=1)*1e6)
            nearby = other[np.lexsort((other, distance))[:8]]
            for left, right in combinations(map(int, nearby), 2):
                before.add(tuple(sorted((int(owner), left, right))))
    else:
        assert len(nodes) <= 128
        before = set(combinations(range(len(nodes)), 3))
    for word in before:
        if active[list(word)].sum() >= 2 and not decoder.irrelevant_isolate[list(word)].all() and mixed(word, decoder.sizes):
            triples.add(word)
    words = np.array(sorted(triples), dtype=np.int32); assert len(words) > 0
    requested = min(count, len(words)); rng = np.random.default_rng(seed)
    chosen = words[rng.choice(len(words), requested, replace=False)]
    cycles = np.empty((2*requested, 3), dtype=np.int32); cycles[::2] = chosen; cycles[1::2] = chosen[:, [0, 2, 1]]
    return cycles, {'observationVocabulary': kind+' mixed-size source triples', 'wordsBeforeGates': len(before),
        'eligibleMixedSizeTriples': len(words), 'sampledUnorderedTriples': requested, 'cycles': len(cycles),
        'minDirectlyConflictingOwnersInWord': 2, 'nearestNeighborsAcrossAllSizes': 8 if kind == 'local' else None,
        'allSelectedWordsHaveDifferentCardSizes': True, 'bothCycleDirections': True,
        'sourceGeometryAndInitialObservationsOnly': True, 'coordinatesProposedBySupportBuilder': False,
        'nativeMetricBasedSupportSelection': False, 'allHistoryWireExclusionClaimed': False}
