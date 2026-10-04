"""Uniform conditional source vocabulary across all card sizes and distances."""
import numpy as np
from mixed_size_owner_cycles import mixed, support as local_support


def support(decoder, nodes, kind, seed, count):
    if kind == 'local': return local_support(decoder, nodes, kind, seed, count)
    assert kind == 'global' and 1 <= count <= 1024
    active = np.any(nodes[:, 4:6] > 0, axis=1); isolate = decoder.irrelevant_isolate
    a = np.flatnonzero(active); b = np.flatnonzero(~active)
    choose2 = lambda n: n*(n-1)//2; choose3 = lambda n: n*(n-1)*(n-2)//6
    eligible = lambda aa, bb: choose2(aa)*bb+choose3(aa)
    two = choose2(len(a))*len(b); three = choose3(len(a)); raw = two+three
    isolated = eligible(int((active & isolate).sum()), int((~active & isolate).sum()))
    groups = {}
    for index, size in enumerate(decoder.sizes): groups.setdefault(tuple(size), []).append(index)
    same = same_isolated = 0
    for group in groups.values():
        group = np.array(group); selected = active[group]; iso = isolate[group]
        same += eligible(int(selected.sum()), int((~selected).sum()))
        same_isolated += eligible(int((selected & iso).sum()), int((~selected & iso).sum()))
    total = raw-isolated-same+same_isolated; assert total > 0
    requested = min(count, total); rng = np.random.default_rng(seed); seen = set(); words = []; draws = 0
    while len(words) < requested and draws < requested*1000:
        if int(rng.integers(raw)) < two:
            word = tuple(sorted([*map(int, rng.choice(a, 2, replace=False)), int(rng.choice(b))]))
        else: word = tuple(sorted(map(int, rng.choice(a, 3, replace=False))))
        draws += 1
        if word in seen or isolate[list(word)].all() or not mixed(word, decoder.sizes): continue
        seen.add(word); words.append(word)
    assert len(words) == requested
    words = np.array(words, dtype=np.int32); cycles = np.empty((2*requested, 3), dtype=np.int32)
    cycles[::2] = words; cycles[1::2] = words[:, [0, 2, 1]]
    return cycles, {'observationVocabulary': 'global mixed-size source triples', 'eligibleMixedSizeTriples': total,
        'sampledUnorderedTriples': requested, 'cycles': len(cycles), 'draws': draws,
        'minDirectlyConflictingOwnersInWord': 2, 'allSelectedWordsHaveDifferentCardSizes': True,
        'bothCycleDirections': True, 'uniformConditionalTripleSampling': True,
        'sourceGeometryAndInitialObservationsOnly': True, 'coordinatesProposedBySupportBuilder': False,
        'proposalMeasurementsUsedForSupport': False, 'allHistoryWireExclusionClaimed': False}
