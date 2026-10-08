import csv
import tempfile
import unittest
from pathlib import Path
from gameplay_metrics import summarize_presentmon, percentile


class GameplayMetricsTests(unittest.TestCase):
    def test_percentile(self):
        self.assertEqual(percentile([1, 2, 3], .5), 2)
        self.assertAlmostEqual(percentile([1, 3], .95), 2.9)

    def test_matched_phases_and_pid_filter(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'events.csv'
            fields = ['ProcessID', 'SwapChainAddress', 'CPUStartQPC', 'TimeInQPC', 'PresentMode']
            phases = []
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                for index, (name, step) in enumerate((('source-before', 10), ('source-with-capture', 20), ('source-after', 10))):
                    start = index * 10000
                    phases.append({'name': name, 'start_qpc': start, 'end_qpc': start + 150 * step})
                    for frame in range(150):
                        at = start + frame * step
                        writer.writerow(dict(zip(fields, (7, 'main', at, at, 'Hardware: Independent Flip'))))
                        writer.writerow(dict(zip(fields, (99, 'other', at, at, 'Composed: Flip'))))
            protocol = {'valid': True, 'source_pid': 7, 'qpc_frequency': 1000, 'phases': phases}
            result = summarize_presentmon(path, protocol)
            self.assertEqual(result['bracketed_baseline_present_rate'], 100)
            self.assertEqual(result['capture_present_rate'], 50)
            self.assertEqual(result['present_rate_loss_percent'], 50)
            self.assertEqual(result['phases']['source-with-capture']['p99_present_interval_ms'], 20)
            self.assertEqual(result['baseline_drift_percent'], 0)
            protocol['valid'] = False
            with self.assertRaises(ValueError):
                summarize_presentmon(path, protocol)
            protocol['valid'] = True
            protocol['phases'][-1]['end_qpc'] += 2000
            with self.assertRaises(ValueError):
                summarize_presentmon(path, protocol)
            protocol['phases'][-1]['end_qpc'] -= 2000
            protocol['source_pid'] = 8
            with self.assertRaises(ValueError):
                summarize_presentmon(path, protocol)


if __name__ == '__main__':
    unittest.main()
