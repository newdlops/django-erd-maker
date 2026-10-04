"""Fixed graph inputs for a trained coordinate policy; no layout proposals."""
import time
import numpy as np


def laplacian_features(count, edges, channels):
    if not 0 < channels <= 32:
        raise ValueError('graph channels must be between one and 32')
    started = time.monotonic()
    edges = np.sort(np.asarray(edges, dtype=np.int32), axis=1)
    edges = edges[np.lexsort((edges[:, 1], edges[:, 0]))]
    # Keep O(nodes * channels + edges) storage. A dense N-by-N eigensolve
    # approached the workstation's 256 MiB process-group budget.
    neighbors = [[] for _ in range(count)]
    for source, target in edges:
        neighbors[source].append(target)
        neighbors[target].append(source)
    degree = np.array([len(row) for row in neighbors], dtype=float)
    inverse = np.zeros(count)
    inverse[degree > 0] = 1 / np.sqrt(degree[degree > 0])
    groups = np.full(count, -1, dtype=int)
    components = 0
    for node in range(count):
        if groups[node] >= 0:
            continue
        groups[node] = components
        stack = [node]
        while stack:
            for other in neighbors[stack.pop()]:
                if groups[other] < 0:
                    groups[other] = components
                    stack.append(other)
        components += 1
    width = min(channels + 8, count - components)
    if width <= 0:
        return np.zeros((count, channels)), {'kind': 'sparse Laplacian subspace iteration',
            'channels': channels, 'nonzeroChannels': 0, 'eigenvalues': [], 'maxEigenResidual': 0.,
            'isolatedNodes': count, 'usesLayoutCoordinates': False, 'proposesCoordinates': False,
            'denseNodeMatrixAllocated': False, 'passes': 0, 'seconds': time.monotonic()-started}
    square_root = np.sqrt(degree)
    denominators = np.maximum(1., np.bincount(groups, weights=degree, minlength=components))
    weights = inverse[edges[:, 0]] * inverse[edges[:, 1]]

    def normalized_adjacency(matrix):
        result = np.zeros_like(matrix)
        np.add.at(result, edges[:, 0], matrix[edges[:, 1]] * weights[:, None])
        np.add.at(result, edges[:, 1], matrix[edges[:, 0]] * weights[:, None])
        return result

    def remove_constant_modes(matrix):
        coefficients = np.zeros((components, matrix.shape[1]))
        np.add.at(coefficients, groups, square_root[:, None] * matrix)
        matrix = matrix - square_root[:, None] * (coefficients / denominators[:, None])[groups]
        matrix[degree == 0] = 0.
        return matrix

    basis = remove_constant_modes(np.random.default_rng(91347).normal(size=(count, width)))
    basis = np.linalg.qr(basis, mode='reduced')[0]
    for _ in range(64):
        # The positive shift also retains the Laplacian eigenvalue-two mode
        # in bipartite fixtures instead of annihilating that basis direction.
        basis = remove_constant_modes(.75 * basis + .25 * normalized_adjacency(basis))
        basis = np.linalg.qr(basis, mode='reduced')[0]
    transformed = basis - normalized_adjacency(basis)
    small = basis.T @ transformed
    values, vectors = np.linalg.eigh(.5 * (small + small.T))
    selected = np.flatnonzero(values > 1e-8)[:channels]
    encoding = basis @ vectors[:, selected]
    eigenvalues = values[selected].copy()
    residual = float(np.max(np.abs(encoding - normalized_adjacency(encoding)
                                  - encoding * eigenvalues), initial=0))
    for column in range(encoding.shape[1]):
        anchor = np.argmax(np.abs(encoding[:, column]))
        if encoding[anchor, column] < 0:
            encoding[:, column] *= -1
    encoding *= np.sqrt(count)
    if encoding.shape[1] < channels:
        encoding = np.pad(encoding, ((0, 0), (0, channels-encoding.shape[1])))
    assert np.isfinite(encoding).all()
    return encoding, {'kind': 'sparse Laplacian subspace iteration', 'channels': channels,
                      'nonzeroChannels': len(selected), 'eigenvalues': eigenvalues.tolist(),
                      'maxEigenResidual': residual, 'isolatedNodes': int(np.count_nonzero(degree == 0)),
                      'usesLayoutCoordinates': False, 'proposesCoordinates': False,
                      'denseNodeMatrixAllocated': False, 'passes': 64,
                      'seconds': time.monotonic()-started}


if __name__ == '__main__':
    edges = np.array([[0, 1], [1, 2], [2, 3], [3, 0], [3, 4], [5, 6], [6, 7]])
    first, report = laplacian_features(10, edges, 8)
    second, _ = laplacian_features(10, edges[::-1, ::-1], 8)
    np.testing.assert_allclose(first, second, rtol=1e-12, atol=1e-12)
    np.testing.assert_allclose(first[[8, 9]], 0., atol=1e-10)
    assert report['nonzeroChannels'] == 6 and report['isolatedNodes'] == 2
    assert report['maxEigenResidual'] < 1e-7 and not report['denseNodeMatrixAllocated']
    print({'graphFeatureChecks': 'pass', 'edgeOrderInvariant': True, 'isolatedRowsZero': True,
           'eigenResidual': report['maxEigenResidual']})
