"""Tests for the board registry."""
import pytest
from vv.boards import BOARDS, REPO_ROOT, board_by_id


def test_five_boards_declared():
    assert {b.id for b in BOARDS} == {
        "kincodrive", "powerstage", "leddriver", "buttonboard", "knob"}


@pytest.mark.parametrize("board", BOARDS, ids=lambda b: b.id)
def test_declared_paths_exist(board):
    for rel in (board.app_dir, board.boot_dir, *board.headers):
        assert (REPO_ROOT / rel).exists(), f"{board.id}: missing {rel}"
    if board.dbc is not None:
        assert (REPO_ROOT / board.dbc).is_file(), f"{board.id}: missing {board.dbc}"


def test_bootloader_ids_are_unique():
    ids = [b.blt_rx for b in BOARDS] + [b.blt_tx for b in BOARDS]
    assert len(ids) == len(set(ids)), "bootloader CAN ids collide"


def test_knob_is_the_only_exempt_board():
    assert [b.id for b in BOARDS if b.address_plan_exempt] == ["knob"]
    # The knob now HAS a DBC (authored from the firmware and validated against
    # real captured frames), but it is still absent from Docs/CAN_Bus.md and
    # still exempt from the address plan.
    assert board_by_id("knob").dbc is not None
    assert board_by_id("knob").in_bus_doc is False


def test_board_by_id_rejects_unknown():
    with pytest.raises(KeyError):
        board_by_id("nosuchboard")
