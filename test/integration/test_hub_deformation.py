import importlib.util
from pathlib import Path
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / 'scripts/erd-poc/export_hub_deformation.py'
SPEC = importlib.util.spec_from_file_location('hub_deformation', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class HubDeformationTest(unittest.TestCase):
    def test_swaps_real_hubs_and_interpolates_followers_without_changing_other_anchors(self):
        positions = {'a': (0., 0.), 'b': (100., 60.), 'middle': (50., 30.),
                     'leaf': (0., 10.), 'fixed': (200., 100.), 'x': (190., 110.),
                     'y': (210., 110.), 'isolate': (400., 400.)}
        adjacency = {n: [] for n in positions}
        for a, b in [('a', 'middle'), ('middle', 'b'), ('a', 'leaf'),
                     ('a', 'fixed'), ('fixed', 'x'), ('fixed', 'y')]:
            adjacency[a].append(b)
            adjacency[b].append(a)
        original = positions.copy()
        result, stats = MODULE.harmonic_swap(positions, adjacency, 'a', 'b', 3)
        self.assertEqual(positions, original)
        self.assertEqual(result['a'], original['b'])
        self.assertEqual(result['b'], original['a'])
        self.assertEqual(result['leaf'], (100., 70.))
        for n in ('middle', 'fixed', 'x', 'y', 'isolate'):
            self.assertEqual(result[n], original[n])
        self.assertLess(stats['residual'], 1e-8)
        self.assertEqual(set(result), set(positions))
        self.assertEqual((result, stats), MODULE.harmonic_swap(positions, adjacency, 'a', 'b', 3))

    def test_export_retains_all_original_rows_and_is_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            nodes, edges, output = [directory / name for name in ('nodes.tsv', 'edges.tsv', 'out.tsv')]
            nodes.write_text('a\t0\t0\nb\t100\t0\nleaf\t0\t10\nisolate\t5\t5\n')
            edges.write_text('ab\ta\tb\nleaf-a\tleaf\ta\n')
            MODULE.write_hub_swap(nodes, edges, output, 'a', 'b')
            first = output.read_bytes()
            MODULE.write_hub_swap(nodes, edges, output, 'a', 'b')
            self.assertEqual(first, output.read_bytes())
            self.assertEqual(output.read_text().splitlines()[-1], 'isolate\t5.000000000\t5.000000000')

    def test_rejects_invalid_hub_ids(self):
        for first, second in [('a', 'a'), ('a', 'missing')]:
            with self.assertRaises(ValueError):
                MODULE.harmonic_swap({'a': (0, 0)}, {'a': []}, first, second)


if __name__ == '__main__':
    unittest.main()
