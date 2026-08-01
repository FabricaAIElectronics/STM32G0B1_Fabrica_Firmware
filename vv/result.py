"""Uniform stage result type and summary rendering for the V&V gate.

Four statuses, and the distinction between the last two is the whole point of
being portable:

    pass   the check ran and the code is good
    warn   the check ran, found something worth knowing, not a blocker
    fail   the check ran and THE CODE IS WRONG
    skip   the check could NOT run on this machine - a tool is missing

A fresh machine without STM32CubeIDE should report `skip` for the build stage,
not `fail`. Conflating the two is what turns "you need to install something"
into "the firmware is broken", and it is why a gate that has only ever run on
one box is not a gate.

Use --strict to turn every skip into a failure. That is the right setting for an
actual release, where "we could not check it" must not pass.
"""
from dataclasses import dataclass, field

STATUSES = ("pass", "fail", "warn", "skip")


@dataclass
class StageResult:
    name: str
    status: str
    detail: str
    items: list[dict] = field(default_factory=list)

    def __post_init__(self):
        if self.status not in STATUSES:
            raise ValueError(f"bad status {self.status!r}, expected one of {STATUSES}")

    @property
    def ok(self) -> bool:
        """True unless the code itself is wrong. A skip is not a failure."""
        return self.status != "fail"

    @property
    def ran(self) -> bool:
        return self.status != "skip"

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "status": self.status,
            "detail": self.detail,
            "items": self.items,
        }


def format_summary(results: list[StageResult], strict: bool = False) -> str:
    """Render results as a fixed-width table, with a verdict line."""
    width = max((len(r.name) for r in results), default=4)
    lines = [f"{'STAGE'.ljust(width)}  RESULT  DETAIL", "-" * (width + 30)]
    for r in results:
        lines.append(f"{r.name.ljust(width)}  {r.status.upper():6}  {r.detail}")

    failed = [r for r in results if r.status == "fail"]
    skipped = [r for r in results if r.status == "skip"]
    lines.append("")
    if failed:
        lines.append(f"VERDICT: FAIL - {len(failed)} stage(s) found problems: "
                     f"{', '.join(r.name for r in failed)}")
    elif skipped and strict:
        lines.append(f"VERDICT: FAIL (--strict) - {len(skipped)} stage(s) could "
                     f"not run: {', '.join(r.name for r in skipped)}")
    elif skipped:
        lines.append(
            f"VERDICT: PASS with {len(skipped)} stage(s) SKIPPED - "
            f"{', '.join(r.name for r in skipped)}")
        lines.append("         Skipped stages checked nothing. Install the "
                     "missing tools, or re-run with --strict to treat")
        lines.append("         a skip as a failure before an actual release.")
    else:
        lines.append("VERDICT: PASS - every stage ran")
    return "\n".join(lines)
