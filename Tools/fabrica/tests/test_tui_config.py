"""Config screen logic: rendering, editing and key handling, no curses drawing.

This file has never been executed on the machine the tool was written on, so
everything that can be tested without a terminal is: the table the screen
renders, the numeric editor, and what each key does.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

_PKG_ROOT = Path(__file__).resolve().parents[1]
if str(_PKG_ROOT) not in sys.path:
    sys.path.insert(0, str(_PKG_ROOT))

import curses  # noqa: E402

from fabrica import config, params, tui  # noqa: E402


class FakeApp:
    """Only the state the config screen touches."""

    def __init__(self, board_id="powerstage"):
        self.cfg_params = [(g, p)
                           for g in params.for_board(board_id).groups
                           for p in g.params]
        self.cfg_state = config.BoardState(board_id)
        self.cfg_sel = 0
        self.cfg_edits = {}
        self.cfg_editing = False
        self.cfg_buf = ""
        self.cfg_allow_actuate = False
        self.cfg_confirm_defaults = False
        self.busy = False
        self.said = []
        self.calls = []
        self.board = type("B", (), {"id": board_id,
                                    "name": board_id.title()})()

    def say(self, level, text):
        self.said.append((level, text))

    # Real implementations under test.
    cfg_current = tui.App.cfg_current
    cfg_begin_edit = tui.App.cfg_begin_edit
    cfg_commit_edit = tui.App.cfg_commit_edit
    cfg_type = tui.App.cfg_type
    cfg_backspace = tui.App.cfg_backspace

    # Actions are recorded rather than run: they need a bus.
    def config_write(self, all_edits=False):
        self.calls.append(("write", all_edits))

    def config_save(self):
        self.calls.append(("save", None))

    def config_defaults(self):
        self.calls.append(("defaults", None))

    def config_refresh(self):
        self.calls.append(("refresh", None))

    def leave_config(self):
        self.calls.append(("leave", None))


def key(app, ch=None, k=""):
    tui._handle_config_key(app, ch if ch is not None else ord(k or " "), k)


def seen(value, live=None, stored=None):
    return config.ParamValue(value, live, stored,
                             live is not None, stored is not None)


# ------------------------------------------------------------ rendering --


def test_table_has_a_row_per_parameter_and_a_header_per_group():
    app = FakeApp()
    text = "\n".join(t for _, t, _ in tui.config_lines(app))
    assert "CMD_FAN" in text
    assert "Fan_Duty" in text
    assert "desired" in text and "live" in text and "stored" in text


def test_live_and_stored_render_in_their_own_columns():
    app = FakeApp()
    app.cfg_state.values["Fan_Duty"] = seen("Fan_Duty", live=70, stored=40)
    row = next(t for _, t, _ in tui.config_lines(app) if "Fan_Duty" in t)
    assert "70" in row and "40" in row


def test_a_value_never_seen_renders_as_a_dash_not_a_zero():
    """Unknown and zero are different facts and must not look the same."""
    app = FakeApp()
    row = next(t for _, t, _ in tui.config_lines(app) if "Fan_Duty" in t)
    assert "0" not in row.split("Fan_Duty")[1]
    assert "-" in row


def test_an_edited_value_is_marked_as_pending():
    app = FakeApp()
    app.cfg_edits["Fan_Duty"] = 65
    row = next(t for _, t, _ in tui.config_lines(app) if "Fan_Duty" in t)
    assert "*65" in row


def test_the_editor_shows_what_is_being_typed():
    app = FakeApp()
    app.cfg_editing = True
    app.cfg_buf = "6"
    row = tui.config_lines(app)[2][1]
    assert "6_" in row


def test_actuating_parameters_are_flagged_in_the_table():
    app = FakeApp()
    row = next(t for _, t, _ in tui.config_lines(app) if "HS_Cmd_DRIVE" in t)
    assert row.rstrip().endswith("!")


def test_the_cursor_marks_the_selected_row():
    app = FakeApp()
    rows = [t for _, t, _ in tui.config_lines(app) if t.startswith((">", " "))]
    assert sum(1 for r in rows if r.startswith(">")) == 1


# -------------------------------------------------------------- editing --


def test_typing_then_enter_records_a_pending_edit():
    app = FakeApp()
    app.cfg_begin_edit()
    for ch in "70":
        app.cfg_type(ch)
    app.cfg_commit_edit()
    assert app.cfg_edits == {"Fan_Mode": 70}
    assert app.cfg_editing is False


def test_hex_is_accepted_for_a_mask():
    app = FakeApp()
    app.cfg_begin_edit()
    for ch in "0x1F":
        app.cfg_type(ch)
    app.cfg_commit_edit()
    assert app.cfg_edits["Fan_Mode"] == 31


def test_non_numeric_input_is_rejected_with_a_message():
    app = FakeApp()
    app.cfg_begin_edit()
    app.cfg_buf = "x"          # a lone 'x' passes the filter but is not a number
    app.cfg_commit_edit()
    assert app.cfg_edits == {}
    assert any("not a number" in t for _, t in app.said)


def test_letters_that_are_not_hex_digits_are_ignored():
    app = FakeApp()
    app.cfg_begin_edit()
    for ch in "7q0":
        app.cfg_type(ch)
    assert app.cfg_buf == "70"


def test_backspace_erases():
    app = FakeApp()
    app.cfg_begin_edit()
    app.cfg_type("7")
    app.cfg_type("0")
    app.cfg_backspace()
    assert app.cfg_buf == "7"


def test_committing_an_empty_buffer_records_nothing():
    app = FakeApp()
    app.cfg_begin_edit()
    app.cfg_commit_edit()
    assert app.cfg_edits == {}


# --------------------------------------------------------------- keys ----


def test_j_and_k_move_the_selection_and_wrap():
    app = FakeApp()
    key(app, k="j")
    assert app.cfg_sel == 1
    app.cfg_sel = len(app.cfg_params) - 1
    key(app, k="j")
    assert app.cfg_sel == 0
    key(app, k="k")
    assert app.cfg_sel == len(app.cfg_params) - 1


def test_e_starts_editing_and_esc_abandons_it():
    app = FakeApp()
    key(app, k="e")
    assert app.cfg_editing
    key(app, ch=27)
    assert not app.cfg_editing
    assert app.cfg_edits == {}


def test_keys_go_to_the_editor_while_editing():
    """A 'w' typed into a value must not fire a write."""
    app = FakeApp()
    key(app, k="e")
    key(app, k="7")
    key(app, k="w")
    assert app.cfg_buf == "7"
    assert app.calls == []


def test_enter_while_editing_commits():
    app = FakeApp()
    key(app, k="e")
    key(app, k="7")
    key(app, ch=10)
    assert app.cfg_edits == {"Fan_Mode": 7}


def test_w_writes_and_shift_w_writes_all():
    app = FakeApp()
    key(app, k="w")
    key(app, k="W")
    assert app.calls == [("write", False), ("write", True)]


def test_s_saves_and_shift_d_loads_defaults_and_r_rereads():
    app = FakeApp()
    key(app, k="s")
    key(app, k="D")
    key(app, k="R")
    assert app.calls == [("save", None), ("defaults", None), ("refresh", None)]


def test_c_and_esc_leave_the_screen():
    app = FakeApp()
    key(app, k="c")
    key(app, ch=27)
    assert app.calls == [("leave", None), ("leave", None)]


def test_a_toggles_the_actuate_permission_and_says_so():
    app = FakeApp()
    key(app, k="A")
    assert app.cfg_allow_actuate is True
    assert any("ALLOWED" in t for _, t in app.said)
    key(app, k="A")
    assert app.cfg_allow_actuate is False


def test_actions_are_refused_while_something_is_running():
    app = FakeApp()
    app.busy = True
    key(app, k="w")
    key(app, k="s")
    assert app.calls == []
    assert any("already running" in t for _, t in app.said)


def test_navigation_still_works_while_busy():
    """Reading the table during a write is harmless and useful."""
    app = FakeApp()
    app.busy = True
    key(app, k="j")
    assert app.cfg_sel == 1


# ----------------------------------------------------------- App wiring --


def test_write_refuses_an_actuating_parameter_until_a_is_pressed(monkeypatch):
    """The guard lives in App.config_write, so exercise the real method."""
    app = FakeApp()
    app.cfg_edits = {"HS_Cmd_DRIVE": 1}
    app.cfg_sel = next(i for i, (_, p) in enumerate(app.cfg_params)
                       if p.signal == "HS_Cmd_DRIVE")
    app._worker = lambda fn: app.calls.append(("worker", fn))
    tui.App.config_write(app)
    assert app.calls == []
    assert any("switches the DRIVE rail" in t for _, t in app.said)

    app.cfg_allow_actuate = True
    tui.App.config_write(app)
    assert app.calls and app.calls[0][0] == "worker"


def test_write_with_nothing_edited_says_so():
    app = FakeApp()
    app._worker = lambda fn: app.calls.append(("worker", fn))
    tui.App.config_write(app)
    assert app.calls == []
    assert any("nothing edited" in t for _, t in app.said)


def test_load_defaults_needs_pressing_twice():
    """It overwrites EEPROM, so it does not happen on one keystroke."""
    app = FakeApp()
    app._worker = lambda fn: app.calls.append(("worker", fn))

    tui.App.config_defaults(app)
    assert app.calls == []
    assert app.cfg_confirm_defaults is True
    assert any("again" in t for _, t in app.said)

    tui.App.config_defaults(app)
    assert app.calls and app.calls[0][0] == "worker"
    assert app.cfg_confirm_defaults is False


def test_leaving_warns_about_unwritten_edits():
    app = FakeApp()
    app.config_mode = True
    app.config_bus = None
    app.cfg_edits = {"Fan_Duty": 70}
    tui.App.leave_config(app)
    assert app.config_mode is False
    assert app.cfg_edits == {}
    assert any("never written" in t for _, t in app.said)


def test_config_help_names_the_destructive_keys():
    assert "s=SAVE-eeprom" in tui.CONFIG_HELP
    assert "D=defaults" in tui.CONFIG_HELP
    assert "c=config" in tui.HELP
