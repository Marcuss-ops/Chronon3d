#!/usr/bin/env python3
"""Run from Chronon3d: python3 docs/evidence/camera-closure-final/verify_artifacts.py."""
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
CONTRACT = json.loads((ROOT / 'measurement-contract.json').read_text())


def run(args):
    return subprocess.run(args, check=True, capture_output=True)


def energy(rgb, roi):
    x0, y0, x1, y1 = roi
    crop = rgb[y0:y1, x0:x1].astype(np.float64)
    gray = crop @ np.array([.2126, .7152, .0722])
    return float(np.mean(np.diff(gray, axis=0)**2) + np.mean(np.diff(gray, axis=1)**2))


def main():
    results = {}
    sentinels = {}
    all_pass = True
    for mode in ('dof', 'sharp', 'shutter'):
        path = ROOT / f'optical_{mode}_1080p.mp4'
        probe = json.loads(run(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0',
            '-show_entries', 'stream=codec_name,width,height,r_frame_rate,nb_read_frames', '-of', 'json', str(path)]).stdout)
        stream = probe['streams'][0]
        decoded = run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'null', '-'])
        black = run(['ffmpeg', '-hide_banner', '-i', str(path), '-vf',
            'blackdetect=d=0.033:pix_th=0.10:pic_th=0.98', '-an', '-f', 'null', '-'])
        (ROOT / f'{mode}-blackdetect.log').write_bytes(black.stderr)
        black_intervals = [line for line in black.stderr.decode().splitlines() if 'black_start:' in line]
        passed = (stream['width'] == 1920 and stream['height'] == 1080 and
                  stream['r_frame_rate'] == '30/1' and int(stream['nb_read_frames']) >= 150 and
                  stream['codec_name'] == 'h264' and not decoded.stderr and not black_intervals)
        all_pass &= passed
        result = {'stream': stream, 'bytes': path.stat().st_size,
                  'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                  'black_intervals': black_intervals, 'decode_errors': decoded.stderr.decode(),
                  'output_gate': 'PASS' if passed else 'FAIL', 'sentinels': {}}
        for frame in (0, 30, 75, 149):
            data = run(['ffmpeg', '-v', 'error', '-i', str(path), '-vf', f'select=eq(n\\,{frame})',
                        '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-']).stdout
            rgb = np.frombuffer(data, np.uint8).reshape(1080, 1920, 3)
            Image.fromarray(rgb).save(ROOT / f'{mode}-frame-{frame:03}.png')
            sentinels[mode, frame] = rgb
            result['sentinels'][str(frame)] = {'rgb_sha256': hashlib.sha256(data).hexdigest(),
                'mean_rgb': float(rgb.mean()), 'roi_energy': {name: energy(rgb, roi)
                    for name, roi in CONTRACT['roi_xyxy'].items()}}
        results[mode] = result
    ratios = {}
    for frame, focused in ((30, 'near'), (75, 'middle'), (149, 'far')):
        values = {}
        for plane, roi in CONTRACT['roi_xyxy'].items():
            denominator = energy(sentinels['sharp', frame], roi)
            numerator = energy(sentinels['dof', frame], roi)
            ratio = numerator / denominator if denominator > 0 else None
            passed = ratio is not None and (ratio >= .70 if plane == focused else ratio <= .90)
            values[plane] = {'on_off_energy_ratio': ratio, 'expected_focused': plane == focused,
                             'gate': 'PASS' if passed else 'FAIL'}
        ratios[str(frame)] = values
    report = {'videos': results, 'focus_metrics_lossy_diagnostic': ratios,
              'note': 'MP4 measurements are diagnostic, not raw pixel parity or human visual approval.',
              'output_gate': 'PASS' if all_pass else 'FAIL'}
    (ROOT / 'video-metrics.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'output_gate': report['output_gate'], 'focus': ratios}, indent=2))
    return 0 if all_pass else 1


if __name__ == '__main__':
    raise SystemExit(main())
