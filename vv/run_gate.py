"""Entry point for the firmware V&V gate."""
import argparse
import json
import sys
import traceback
from pathlib import Path

# Allow both `python vv/run_gate.py` and `python -m vv.run_gate`. Run as a
# script, sys.path[0] is vv/ rather than the repo root, so `import vv.*` would
# fail; put the repo root on the path before importing anything from vv.
if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from vv.result import StageResult, format_summary  # noqa: E402


def run_stages(stages, continue_on_fail: bool) -> list[StageResult]:
    """Run each stage in order. Stop after the first failure unless told not to."""
    results: list[StageResult] = []
    for stage in stages:
        try:
            result = stage()
        except Exception as exc:  # a crashing stage is a failing stage
            name = getattr(stage, "stage_name", getattr(stage, "__name__", "unknown"))
            result = StageResult(
                name=name,
                status="fail",
                detail=f"stage raised {type(exc).__name__}: {exc}",
                items=[{"traceback": traceback.format_exc()}],
            )
        results.append(result)
        if result.status == "fail" and not continue_on_fail:
            break
    return results


def build_stage_list(only: str | None):
    """Import stage callables lazily so a partial checkout can still run --help."""
    from vv.checks import preflight, static, build, size, memmap, conformance
    from vv.unit import runner as unit_runner

    ordered = [
        ("preflight", preflight.run),
        ("static", static.run),
        ("unit", unit_runner.run),
        ("build", build.run),
        ("size", size.run),
        ("memmap", memmap.run),
        ("conformance", conformance.run),
    ]
    if only:
        names = [n for n, _ in ordered]
        if only not in names:
            raise SystemExit(f"unknown stage {only!r}; choose from {', '.join(names)}")
        ordered = [(n, f) for n, f in ordered if n == only]
    return [f for _, f in ordered]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Firmware pre-release V&V gate")
    parser.add_argument("--continue", dest="cont", action="store_true",
                        help="run every stage instead of stopping at the first failure")
    parser.add_argument("--stage", help="run a single stage by name")
    parser.add_argument("--json", dest="json_path", help="write structured results here")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite vv/baseline.txt from the current warnings")
    parser.add_argument("--stage-artifacts", action="store_true",
                        help="on a green gate, copy .srec files and write the manifest")
    parser.add_argument("--strict", action="store_true",
                        help="treat a skipped stage as a failure. Use this for an "
                             "actual release: 'we could not check it' must not pass")
    args = parser.parse_args(argv)

    if args.update_baseline:
        from vv.checks import static
        count = static.update_baseline()
        print(f"baseline rewritten with {count} warnings")
        return 0

    # --strict implies --continue: if a skip is going to fail the run, you want
    # to see every stage that could not run, not just the first one.
    results = run_stages(build_stage_list(args.stage),
                         continue_on_fail=args.cont or args.strict)
    print(format_summary(results, strict=args.strict))

    if args.json_path:
        with open(args.json_path, "w", encoding="utf-8") as fh:
            json.dump([r.to_dict() for r in results], fh, indent=2)

    passed = all(r.ok for r in results)
    if args.strict:
        passed = passed and all(r.ran for r in results)

    if args.stage_artifacts:
        fully_ran = all(r.ran for r in results)
        if passed and fully_ran:
            from vv.stage import stage_artifacts
            manifest = stage_artifacts(gate_passed=True)
            print(f"\nstaged {len(manifest['boards'])} boards to "
                  f"Tools/fabrica/firmware/ (git {manifest['git_sha'][:8]}"
                  f"{', DIRTY' if manifest['git_dirty'] else ''})")
        elif not fully_ran:
            skipped = [r.name for r in results if r.status == "skip"]
            print(f"\nartifacts NOT staged: {len(skipped)} stage(s) could not "
                  f"run here ({', '.join(skipped)}).")
            print("Staging an image the gate never checked would defeat the "
                  "point of the gate.")
        else:
            print("\ngate failed; artifacts not staged")

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
