import argparse
import json
import math
from pathlib import Path
import subprocess
import sys


def sample(runtime, source, channel, skin):
    args = [runtime, source, channel]
    if skin is not None:
        args.append(skin)
    return json.loads(subprocess.check_output(args))


def finite(values):
    return all(isinstance(value, (int, float)) and math.isfinite(value) for value in values)


def compare(reference, candidate, selected_bones=None, selected_clips=None):
    bones = selected_bones or list(reference["setup"])
    clips = selected_clips or list(reference["animations"])
    if not selected_clips and reference["animations"].keys() != candidate["animations"].keys():
        raise ValueError("Animation names differ")
    authored_order = [name for name in candidate["order"] if name in reference["order"]]
    result = {
        "versions": [reference["version"], candidate["version"]],
        "authored_order_matches": authored_order == reference["order"],
        "reference_order": reference["order"],
        "candidate_order": candidate["order"],
        "reference_updates": reference["updates"],
        "candidate_updates": candidate["updates"],
        "poses": {},
    }

    def summarize(frames):
        metrics = ("position", "rotation_degrees", "scale", "matrix")
        peaks = {key: {"error": 0.0, "time": None, "bone": None} for key in metrics}
        tested_bones = set()
        for time, expected, actual in frames:
            for name in bones:
                a, b = expected[name], actual[name]
                if a["active"] != b["active"]:
                    raise ValueError(f"Bone {name} activation differs at {time:.6g} s")
                if not a["active"]:
                    continue
                tested_bones.add(name)
                w, v = a["world"], b["world"]
                if not finite(w + v + a["matrix"] + b["matrix"]):
                    raise ValueError(f"Invalid world transform for {name} at {time:.6g} s")
                values = (
                    math.hypot(w[0] - v[0], w[1] - v[1]),
                    abs(math.remainder(w[2] - v[2], 360)),
                    max(abs(x - y) for x, y in zip(w[3:], v[3:])),
                    max(abs(x - y) for x, y in zip(a["matrix"], b["matrix"])),
                )
                for metric, error in zip(metrics, values):
                    if error > peaks[metric]["error"]:
                        peaks[metric] = {"error": error, "time": time, "bone": name}
        if not tested_bones:
            raise ValueError("The selected bones are inactive")
        return {"samples": len(frames), "active_bones": len(tested_bones), "max": peaks}

    result["poses"]["setup"] = summarize([(0, reference["setup"], candidate["setup"])])
    for clip in clips:
        expected, actual = reference["animations"][clip], candidate["animations"][clip]
        if len(expected) != len(actual):
            raise ValueError(f"{clip} sample counts differ")
        frames = []
        for a, b in zip(expected, actual):
            if abs(a["time"] - b["time"]) > 0.00001:
                raise ValueError(f"{clip} sample times differ")
            frames.append((a["time"], a["bones"], b["bones"]))
        result["poses"][clip] = summarize(frames)
    return result


def check(result, args, path):
    limits = {"position": args.position, "rotation_degrees": args.rotation,
              "scale": args.scale, "matrix": args.matrix}
    passed = result["authored_order_matches"]
    if not passed:
        print(f"{path} changed authored constraint order", file=sys.stderr)
    for clip, summary in result["poses"].items():
        for metric, peak in summary["max"].items():
            if peak["error"] > limits[metric]:
                passed = False
                print(f"{path} {clip} {peak['bone']} at {peak['time']:.6g} s: "
                      f"{metric} error {peak['error']:.6g} exceeds {limits[metric]:.6g}", file=sys.stderr)
    return passed


def main():
    parser = argparse.ArgumentParser(description="Compare world transforms sampled by the official Spine runtimes")
    parser.add_argument("--runtime38", type=Path, required=True)
    parser.add_argument("--runtime43", type=Path, required=True)
    parser.add_argument("--skin")
    parser.add_argument("--bone", action="append", help="Repeat to select bones. The default is all source bones.")
    parser.add_argument("--clip", action="append", help="Repeat to select clips. The default is all source animations.")
    parser.add_argument("--channel", choices=("all", "none", "rotate", "translate", "scale", "shear"), default="all")
    parser.add_argument("--position", type=float, default=0.001, help="Position tolerance in Spine units")
    parser.add_argument("--rotation", type=float, default=0.002, help="Rotation tolerance in degrees")
    parser.add_argument("--scale", type=float, default=0.0001)
    parser.add_argument("--matrix", type=float, default=0.0001)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--report-only", action="store_true", help="Report errors without enforcing tolerances")
    parser.add_argument("source", type=Path)
    parser.add_argument("outputs", type=Path, nargs="+")
    args = parser.parse_args()
    reference = sample(args.runtime38, args.source, args.channel, args.skin)
    results = {}
    passed = True
    for path in args.outputs:
        result = compare(reference, sample(args.runtime43, path, args.channel, args.skin), args.bone, args.clip)
        results[str(path)] = result
        if not args.report_only and not check(result, args, path):
            passed = False
    report = json.dumps(results, indent=2) + "\n"
    if args.report:
        args.report.write_text(report, encoding="utf-8")
    elif args.report_only:
        print(report)
    if passed and not args.report_only:
        print(f"Official Runtime poses match for {len(args.outputs)} output(s)")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
