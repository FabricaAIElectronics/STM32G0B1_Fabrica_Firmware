# Board configuration profiles

One JSON file per board, named `<board-id>.json`. Tracked in git on purpose: a
bench configuration that cannot be reviewed or diffed is folklore.

```json
{
  "schema": 1,
  "board": "powerstage",
  "note": "standard rig, 6S pack",
  "values": {
    "Fan_Mode": 2,
    "Fan_Duty": 40,
    "Fan_Auto_On_Temp": 45,
    "OC_Thr_DRIVE_mA": 8000
  }
}
```

Values are numbers, including enumerated signals - the DBC decides the bits.
Only parameters the board actually has may appear; `config apply` refuses a
profile naming anything else rather than silently skipping it.

Capture one from a board that is already set up the way you want:

```bash
./fabrica_cli.py config dump powerstage
```

Then review it, commit it, and push it to the next board:

```bash
./fabrica_cli.py config diff  powerstage
./fabrica_cli.py config apply powerstage --allow-transmit --save
```

`apply` writes only what differs, so re-applying to a configured rig puts no
frames on the bus at all. `--save` persists to EEPROM afterwards, and is opt-in
because writing a live value and committing it are different decisions.
