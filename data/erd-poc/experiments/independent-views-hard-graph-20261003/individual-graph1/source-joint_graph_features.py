"""Fixed graph inputs for a trained coordinate policy; no layout proposals."""
import time
import numpy as np


def laplacian_features(count, edges, channels):
    if not 0 < channels <= 32:
        raise ValueError('graph channels must be between one and 32')
    started = time.monotonic()
    adjacency = np.zeros((count, count))
    np.add.at(adjacency, (edges[:, 0], edges[:, 1]), 1.)
    np.add.at(adjacency, (edges[:, 1], edges[:, 0]), 1.)
    degree = adjacency.sum(1)
    inverse = np.zeros(count)
    inverse[degree > 0] = 1 / np.sqrt(degree[degree > 0])
    laplacian = -adjacency * inverse[:, None] * inverse[None, :]
    laplacian[np.arange(count), np.arange(count)] = (degree > 0).astype(float)
    del adjacency
    values, vectors = np.linalg.eigh(laplacian)
    selected = np.flatnonzero(values > 1e-8)[:channels]
    encoding = vectors[:, selected].copy()
    eigenvalues = values[selected].copy()
    residual = float(np.max(np.abs(laplacian @ encoding - encoding * eigenvalues), initial=0))
    assert residual < 1e-7
    for column in range(encoding.shape[1]):
        anchor = np.argmax(np.abs(encoding[:, column]))
        if encoding[anchor, column] < 0:
            encoding[:, column] *= -1
    encoding *= np.sqrt(count)
    if encoding.shape[1] < channels:
        encoding = np.pad(encoding, ((0, 0), (0, channels-encoding.shape[1])))
    assert np.isfinite(encoding).all()
    return encoding, {'kind': 'normalized graph Laplacian eigenvectors', 'channels': channels,
                      'nonzeroChannels': len(selected), 'eigenvalues': eigenvalues.tolist(),
                      'maxEigenResidual': residual, 'isolatedNodes': int(np.count_nonzero(degree == 0)),
                      'usesLayoutCoordinates': False, 'proposesCoordinates': False,
                      'seconds': time.monotonic()-started}


if __name__ == '__main__':
    edges = np.array([[0, 1], [1, 2], [2, 3], [3, 0], [3, 4], [5, 6], [6, 7]])
    first, report = laplacian_features(10, edges, 8)
    second, _ = laplacian_features(10, edges[::-1, ::-1], 8)
    np.testing.assert_array_equal(first, second)
    np.testing.assert_allclose(first[[8, 9]], 0., atol=1e-10)
    assert report['nonzeroChannels'] == 6 and report['isolatedNodes'] == 2
    print({'graphFeatureChecks': 'pass', 'edgeOrderInvariant': True, 'isolatedRowsZero': True,
           'eigenResidual': report['maxEigenResidual']})
