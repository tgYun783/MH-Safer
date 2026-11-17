#!/usr/bin/env python3
"""Score libmtd JSONL logs according to docs/interface.md."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


class ScoreError(ValueError):
    pass


@dataclass
class WatchedField:
    name: str
    start: int
    end: int
    watch_ns: int


@dataclass
class Allocation:
    base: int
    size: int
    alloc_ns: int
    free_ns: int | None = None
    fields: list[WatchedField] = field(default_factory=list)


@dataclass(frozen=True)
class Event:
    name: str
    times: tuple[int, ...]
    kinds: tuple[str, ...]


def _uint(value: Any, where: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ScoreError(f"{where}: expected a nonnegative integer")
    return value


def _address(value: Any, where: str) -> int:
    if not isinstance(value, str) or not value.startswith("0x"):
        raise ScoreError(f"{where}: expected a 0x-prefixed address")
    try:
        result = int(value[2:], 16)
    except ValueError as exc:
        raise ScoreError(f"{where}: invalid address {value!r}") from exc
    if result < 0:
        raise ScoreError(f"{where}: invalid address {value!r}")
    return result


def _jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        with path.open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ScoreError(f"{path}:{line_number}: invalid JSON: {exc.msg}") from exc
                if not isinstance(value, dict):
                    raise ScoreError(f"{path}:{line_number}: expected a JSON object")
                records.append(value)
    except OSError as exc:
        raise ScoreError(f"cannot read {path}: {exc}") from exc
    return records


def _required_string(record: dict[str, Any], key: str, where: str) -> str:
    value = record.get(key)
    if not isinstance(value, str) or not value:
        raise ScoreError(f"{where}.{key}: expected a non-empty string")
    return value


def _read_fields(
    record: dict[str, Any], allocation: Allocation, watch_ns: int, where: str
) -> list[WatchedField]:
    fields = record.get("fields")
    if not isinstance(fields, list):
        raise ScoreError(f"{where}.fields: expected an array")
    parsed: list[WatchedField] = []
    for index, value in enumerate(fields):
        item_where = f"{where}.fields[{index}]"
        if not isinstance(value, dict):
            raise ScoreError(f"{item_where}: expected an object")
        name = _required_string(value, "name", item_where)
        offset = _uint(value.get("offset"), f"{item_where}.offset")
        size = _uint(value.get("size"), f"{item_where}.size")
        if size == 0 or offset + size > allocation.size:
            raise ScoreError(f"{item_where}: field range is outside its allocation")
        parsed.append(WatchedField(name, allocation.base + offset,
                                   allocation.base + offset + size, watch_ns))
    return parsed


def _allocations(records: Iterable[dict[str, Any]]) -> list[Allocation]:
    allocations: list[Allocation] = []
    active_by_base: dict[int, list[Allocation]] = {}
    previous_ns = -1
    for index, record in enumerate(records):
        where = f"address log record {index}"
        ns = _uint(record.get("ns"), f"{where}.ns")
        if ns < previous_ns:
            raise ScoreError(f"{where}.ns: address log timestamps are not ordered")
        previous_ns = ns
        event = _required_string(record, "event", where)
        base = _address(record.get("addr"), f"{where}.addr")
        if event == "state_alloc":
            size = _uint(record.get("size"), f"{where}.size")
            if size == 0:
                raise ScoreError(f"{where}.size: allocation size must be positive")
            allocation = Allocation(base, size, ns)
            allocation.fields.extend(_read_fields(record, allocation, ns, where))
            allocations.append(allocation)
            active_by_base.setdefault(base, []).append(allocation)
        elif event == "field_watch":
            active = next(
                (item for item in reversed(active_by_base.get(base, []))
                 if item.free_ns is None),
                None,
            )
            if active is None:
                raise ScoreError(f"{where}: field_watch without an active state_alloc")
            active.fields.extend(_read_fields(record, active, ns, where))
        elif event == "state_free":
            active = next(
                (item for item in reversed(active_by_base.get(base, []))
                 if item.free_ns is None),
                None,
            )
            if active is None:
                raise ScoreError(f"{where}: state_free without an active state_alloc")
            active.free_ns = ns
        else:
            raise ScoreError(f"{where}.event: unknown event {event!r}")
    return allocations


def _is_stale(label: dict[str, Any], allocations: list[Allocation]) -> bool:
    address = _address(label.get("addr"), "label.addr")
    inject_ns = _uint(label.get("inject_ns"), "label.inject_ns")
    name = _required_string(label, "name", "label")
    for allocation in allocations:
        if not allocation.base <= address < allocation.base + allocation.size:
            continue
        if inject_ns < allocation.alloc_ns:
            continue
        if allocation.free_ns is not None and inject_ns >= allocation.free_ns:
            continue
        if any(
            item.name == name and item.start <= address < item.end and
            item.watch_ns <= inject_ns
            for item in allocation.fields
        ):
            return False
    return True


def _parse_inputs(
    labels: list[dict[str, Any]],
    alerts: list[dict[str, Any]],
    addresses: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[Allocation]]:
    allocations = _allocations(addresses)
    parsed_labels: list[dict[str, Any]] = []
    for index, record in enumerate(labels):
        where = f"label {index}"
        _uint(record.get("inject_ns"), f"{where}.inject_ns")
        _required_string(record, "name", where)
        _address(record.get("addr"), f"{where}.addr")
        hidden = record.get("hidden")
        if not isinstance(hidden, bool):
            raise ScoreError(f"{where}.hidden: expected a boolean")
        parsed_labels.append({**record, "stale": _is_stale(record, allocations)})

    parsed_alerts: list[dict[str, Any]] = []
    for index, record in enumerate(alerts):
        where = f"alert {index}"
        _uint(record.get("detect_ns"), f"{where}.detect_ns")
        _required_string(record, "name", where)
        kind = _required_string(record, "kind", where)
        if kind not in ("behavior", "prediction"):
            raise ScoreError(f"{where}.kind: expected 'behavior' or 'prediction'")
        parsed_alerts.append(record)
    return parsed_labels, parsed_alerts, allocations


def _events(alerts: list[dict[str, Any]], strategy: str, window_ns: int) -> list[Event]:
    selected = [
        Event(item["name"], (item["detect_ns"],), (item["kind"],))
        for item in alerts
        if strategy == "OR" or item["kind"] == strategy
    ]
    if strategy != "AND":
        return selected

    behavior: dict[str, list[int]] = {}
    prediction: dict[str, list[int]] = {}
    for alert in alerts:
        target = behavior if alert["kind"] == "behavior" else prediction
        target.setdefault(alert["name"], []).append(alert["detect_ns"])
    combined: list[Event] = []
    for name in behavior.keys() & prediction.keys():
        left = sorted(behavior[name])
        right = sorted(prediction[name])
        i = j = 0
        while i < len(left) and j < len(right):
            if abs(left[i] - right[j]) <= window_ns:
                combined.append(Event(name, (left[i], right[j]), ("behavior", "prediction")))
                i += 1
                j += 1
            elif left[i] < right[j]:
                i += 1
            else:
                j += 1
    return combined


def _matches(event: Event, label: dict[str, Any], window_ns: int) -> bool:
    start = label["inject_ns"]
    end = start + window_ns
    return event.name == label["name"] and all(start <= ns <= end for ns in event.times)


def _percentile(values: list[int], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * percentile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def _metrics(
    labels: list[dict[str, Any]],
    events: list[Event],
    window_ns: int,
    legal_scopes: int | None,
    elapsed_seconds: float | None,
) -> dict[str, Any]:
    active_labels = [item for item in labels if not item["stale"]]
    regular_labels = [item for item in active_labels if not item["hidden"]]
    matched_events: set[int] = set()
    detected: set[int] = set()
    hidden_detected: set[int] = set()
    delays: list[int] = []
    for label_index, label in enumerate(active_labels):
        matches = [
            (event_index, event)
            for event_index, event in enumerate(events)
            if _matches(event, label, window_ns)
        ]
        if not matches:
            continue
        matched_events.update(event_index for event_index, _ in matches)
        if label["hidden"]:
            hidden_detected.add(label_index)
        else:
            detected.add(label_index)
            delays.append(min(max(event.times) - label["inject_ns"] for _, event in matches))

    tp = len(detected)
    fn = len(regular_labels) - tp
    fp = len(events) - len(matched_events)
    positive = len(regular_labels)
    return {
        "tp": tp,
        "fn": fn,
        "fp": fp,
        "stale": sum(item["stale"] for item in labels),
        "hidden_labels": sum(item["hidden"] and not item["stale"] for item in labels),
        "hidden_tp": len(hidden_detected),
        "hidden_detection_rate": (
            len(hidden_detected) / sum(item["hidden"] and not item["stale"] for item in labels)
            if any(item["hidden"] and not item["stale"] for item in labels) else None
        ),
        "detection_rate": tp / positive if positive else None,
        "false_positive_rate": fp / legal_scopes if legal_scopes else None,
        "false_positives_per_10min": (
            fp * 600.0 / elapsed_seconds if elapsed_seconds and elapsed_seconds > 0 else None
        ),
        "latency_ns": {
            "median": _percentile(delays, 0.5),
            "p95": _percentile(delays, 0.95),
            "max": max(delays) if delays else None,
        },
    }


def score(
    labels: list[dict[str, Any]],
    alerts: list[dict[str, Any]],
    addresses: list[dict[str, Any]],
    window_ms: int = 200,
    legal_scopes: int | None = None,
    elapsed_seconds: float | None = None,
) -> dict[str, Any]:
    if window_ms <= 0:
        raise ScoreError("window_ms must be positive")
    parsed_labels, parsed_alerts, _ = _parse_inputs(labels, alerts, addresses)
    window_ns = window_ms * 1_000_000
    strategies = {
        "OR": "OR",
        "AND": "AND",
        "behavior_only": "behavior",
        "prediction_only": "prediction",
    }
    comparison = {
        name: _metrics(parsed_labels, _events(parsed_alerts, strategy, window_ns),
                       window_ns, legal_scopes, elapsed_seconds)
        for name, strategy in strategies.items()
    }
    return {
        "window_ms": window_ms,
        "counts": {key: comparison["OR"][key] for key in
                   ("tp", "fn", "fp", "stale", "hidden_labels", "hidden_tp")},
        "rates": {key: comparison["OR"][key] for key in
                  ("detection_rate", "false_positive_rate", "false_positives_per_10min")},
        "latency_ns": comparison["OR"]["latency_ns"],
        "by_kind": {
            "behavior": comparison["behavior_only"],
            "prediction": comparison["prediction_only"],
        },
        "fusion_comparison": comparison,
    }


def _load_optional_json(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {}
    try:
        with path.open(encoding="utf-8") as source:
            value = json.load(source)
    except (OSError, json.JSONDecodeError) as exc:
        raise ScoreError(f"cannot read JSON file {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ScoreError(f"{path}: expected a JSON object")
    return value


def _print_table(result: dict[str, Any]) -> None:
    print("strategy           TP   FN   FP  stale  detection")
    for name, values in result["fusion_comparison"].items():
        rate = values["detection_rate"]
        rate_text = "n/a" if rate is None else f"{100 * rate:.2f}%"
        print(f"{name:<18} {values['tp']:>3}  {values['fn']:>3}  "
              f"{values['fp']:>3}  {values['stale']:>5}  {rate_text:>9}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--labels", required=True, type=Path)
    parser.add_argument("--alerts", required=True, type=Path)
    parser.add_argument("--addr-log", required=True, type=Path)
    parser.add_argument("--stats", type=Path, help="game stats JSON for mtd_end_count and elapsed_sec")
    parser.add_argument("--meta", type=Path, help="experiment meta JSON for duration_sec")
    parser.add_argument("--window-ms", type=int, default=200)
    parser.add_argument("--output", type=Path, help="score JSON path (default: labels directory/score.json)")
    args = parser.parse_args(argv)
    try:
        stats = _load_optional_json(args.stats)
        meta = _load_optional_json(args.meta)
        scope_value = stats.get("mtd_end_count")
        legal_scopes = None if scope_value is None else _uint(scope_value, "stats.mtd_end_count")
        elapsed = stats.get("elapsed_sec", meta.get("duration_sec"))
        if elapsed is not None and (
            isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or elapsed < 0
        ):
            raise ScoreError("elapsed duration must be a nonnegative number")
        result = score(
            _jsonl(args.labels),
            _jsonl(args.alerts),
            _jsonl(args.addr_log),
            args.window_ms,
            legal_scopes,
            float(elapsed) if elapsed is not None else None,
        )
        output = args.output or args.labels.parent / "score.json"
        try:
            output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        except OSError as exc:
            raise ScoreError(f"cannot write {output}: {exc}") from exc
        _print_table(result)
        print(f"score: {output}")
        return 0
    except ScoreError as exc:
        print(f"score error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
