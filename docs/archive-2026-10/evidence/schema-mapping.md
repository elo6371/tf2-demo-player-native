# Item schema mapping probe

This mapping is deliberately local and evidence based. It reads the Valve KeyValues
file `tf/scripts/items/items_game.txt`; it does not infer a model path from a Demo
model index and it does not download workshop or third party files. The reusable
runtime catalog is `ItemCatalog` (`native/include/item_catalog.h`); the probe below
is an independent parser diagnostic that also validates strict file/error handling.

## Probe

```powershell
cmake --build native/build --config Release --target item_schema_probe --parallel 2
native/build/Release/item_schema_probe.exe `
  'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\items\items_game.txt' 303
```

The first line reports the numeric item-definition count. A selected item reports
the literal `model_player`, `model_world`, per-class model paths, and scalar values
under `visuals`. An absent definition returns `status=missing` and a non-zero exit.

## Acceptance evidence

The parser must reject a missing file and a document without a numeric `items`
entry. For a valid local installation it must load the complete file without
reading the entire VPK set or creating files under the game directory. Empty model
fields remain empty: callers must keep the Demo asset state `Unknown` until a real
entity field or a verified schema path supplies a model.
