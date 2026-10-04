import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/erd-poc'))
from export_density_warp import spread_axis, write_density_warp


class DensityWarpTests(unittest.TestCase):
    def test_dense_interval_expands_with_bounds_and_order_preserved(self):
        source = [0, 40, 45, 50, 55, 60, 100]
        actual = spread_axis(source, .5, 10)
        self.assertEqual((actual[0], actual[-1]), (0, 100))
        self.assertEqual(actual, sorted(actual))
        self.assertGreater(actual[-2] - actual[1], 20)
        self.assertLess(actual[1] - actual[0], 40)
        self.assertEqual(spread_axis([5, 5], .5, 10), [5, 5])

    def test_complete_repeatable_seed_export(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / 'in.tsv', Path(directory) / 'out.tsv'
            source.write_text('a\t0\t100\nb\t45\t50\nc\t50\t45\nd\t100\t0\n')
            report = write_density_warp(source, output, .5, 10)
            first = output.read_bytes()
            self.assertEqual(report['nodes'], 4)
            self.assertEqual([line.split('\t')[0] for line in output.read_text().splitlines()], ['a', 'b', 'c', 'd'])
            write_density_warp(source, output, .5, 10)
            self.assertEqual(first, output.read_bytes())
            source.write_text('a\t0\t0\na\t1\t1\n')
            with self.assertRaises(ValueError):
                write_density_warp(source, output, .5, 10)

    def test_rejects_invalid_limits(self):
        for mix, bandwidth in [(0, 10), (1.1, 10), (.5, 0), (.5, float('nan'))]:
            with self.assertRaises(ValueError):
                spread_axis([0, 1], mix, bandwidth)


if __name__ == '__main__':
    unittest.main()
