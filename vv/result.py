"""Uniform stage result type and summary rendering for the V&V gate."""
from dataclasses import dataclass, field

STATUSES = ("pass", "fail", "warn")


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
        return self.status != "fail"

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "status": self.status,
            "detail": self.detail,
            "items": self.items,
        }


def format_summary(results: list[StageResult]) -> str:
    """Render results as a fixed-width table."""
    width = max((len(r.name) for r in results), default=4)
    lines = [f"{'STAGE'.ljust(width)}  RESULT  DETAIL", "-" * (width + 30)]
    for r in results:
        lines.append(f"{r.name.ljust(width)}  {r.status.upper():6}  {r.detail}")
    return "\n".join(lines)
