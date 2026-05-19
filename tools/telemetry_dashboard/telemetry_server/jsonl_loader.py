import json

from .config import JSONL_PATH


def load_jsonl_records():
    """Load all records from the JSONL history file."""
    if not JSONL_PATH.exists():
        return []

    records = []
    with JSONL_PATH.open('r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except Exception:
                continue
            records.append(obj)
    return records


def jsonl_runs_by_id():
    """Return a dict of run_id → run record from JSONL."""
    runs = {}
    for record in load_jsonl_records():
        if record.get('type') != 'run':
            continue
        run_id = record.get('run_id')
        if not run_id:
            continue
        runs[run_id] = record
    return runs


def jsonl_run_detail(run_id):
    """Return a dict with run detail, frames, phases, and counters from JSONL."""
    detail = None
    frames = []
    phases = []
    counters = []

    for record in load_jsonl_records():
        if record.get('run_id') != run_id:
            continue
        record_type = record.get('type')
        if record_type == 'run':
            detail = record
        elif record_type == 'frame':
            frames.append(record)
        elif record_type == 'phase':
            phases.append(record)
        elif record_type == 'counter':
            counters.append(record)

    if detail is None:
        return None

    frames.sort(key=lambda row: row.get('frame_number', 0))
    phases.sort(key=lambda row: row.get('duration_ms', 0.0), reverse=True)
    counters.sort(key=lambda row: row.get('counter_name', ''))

    return {
        'run': detail,
        'frames': frames,
        'phases': phases,
        'counters': counters,
    }
