import argparse
import json
from pathlib import Path
from gameplay_metrics import summarize_presentmon

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
args = parser.parse_args()
protocol = json.loads((args.root / 'gameplay-protocol.json').read_text(encoding='utf-8-sig'))
resources = json.loads((args.root / 'replay-resources.json').read_text(encoding='utf-8-sig'))
if resources.get('valid') is not True:
    raise ValueError('Invalid capture resource interval')
print(json.dumps(summarize_presentmon(args.root / 'presentmon.csv', protocol), indent=2))
