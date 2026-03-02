# Filepath Parameter Browser Porting Guide

This repo adds a reusable `filepath` parameter model intended for Move Anything Shadow UI.

## What Was Added Here

- Reusable browser helpers: `src/shared/filepath_browser.mjs`
- Granny sample-path support in DSP:
  - new param key: `sample_path`
  - accepts absolute path or module-relative path
  - validates `.wav`
- Module metadata updated to expose:
  - `chain_params` entry with `"type": "filepath"`
  - `root` + `filter`

## Move Anything Port Steps

## 1. Copy Shared Helper

Copy this file into Move Anything:

- from: `src/shared/filepath_browser.mjs`
- to: `src/shared/filepath_browser.mjs` (in `move-anything` repo)

## 2. Import Helper in Shadow UI

In `move-anything/src/shadow/shadow_ui.js`, add imports:

```javascript
import {
    buildFilepathBrowserState,
    refreshFilepathBrowser,
    moveFilepathBrowserSelection,
    activateFilepathBrowserItem
} from '/data/UserData/move-anything/shared/filepath_browser.mjs';
```

## 3. Add a New View Constant

Extend `VIEWS` with one new screen state, e.g.:

```javascript
FILEPATH_BROWSER: 'filepathbrowser'
```

## 4. Add Browser UI State

Add state vars near other hierarchy editor state:

```javascript
let filepathBrowserState = null;
let filepathBrowserReturnView = VIEWS.HIERARCHY_EDITOR;
```

## 5. Open Browser from `filepath` Params

In `handleSelect()` under `VIEWS.HIERARCHY_EDITOR`, in the normal param-selection branch:

- detect selected param metadata with `getParamMetadata(key)`
- if `meta.type === 'filepath'`, open browser instead of toggling numeric edit mode

Suggested flow:

```javascript
const currentVal = getSlotParam(hierEditorSlot, buildHierarchyParamKey(key)) || '';
filepathBrowserState = buildFilepathBrowserState(meta, currentVal);
refreshFilepathBrowser(filepathBrowserState);
setView(VIEWS.FILEPATH_BROWSER);
```

## 6. Draw the Browser

Add `drawFilepathBrowser()` and call it in the main render switch.

Use existing style helpers (`drawHeader`, `drawMenuList`, `drawFooter`) so it matches Shadow UI.

Required UX behavior:

- Header: selected parameter label
- Divider: existing header rule from `drawHeader`
- List items:
  - `..` (when not at root)
  - `[folder] Name` for directories
  - `filename.wav` for files

## 7. Handle Input in Browser View

- `handleJog(delta)`: call `moveFilepathBrowserSelection(filepathBrowserState, delta)`
- `handleSelect()`:
  - `const result = activateFilepathBrowserItem(filepathBrowserState)`
  - if `result.action === 'open'`: `refreshFilepathBrowser(...)`
  - if `result.action === 'select'`:
    - call `setSlotParam(hierEditorSlot, buildHierarchyParamKey(result.key), result.value)`
    - return to hierarchy editor view
- `handleBack()`: return to hierarchy editor without changing value

## 8. Parameter Metadata Contract

This browser expects `chain_params` items like:

```json
{
  "key": "sample_path",
  "name": "Sample File",
  "type": "filepath",
  "root": "/data/UserData",
  "filter": ".wav"
}
```

Also supported for `filter`:

- string: `".wav"`
- array: `[".wav", ".aif"]`

## 9. Validate with Granny

After porting to Move Anything:

1. Open Granny in Shadow UI hierarchy editor
2. Select `Sample File`
3. Confirm browser opens with folder/file listing format
4. Select a `.wav` and confirm `sample_path` updates + sample reloads

## Notes

- Browser helper is logic-only and intentionally separate for easy reuse in other modules.
- Root-clamping is enforced: `..` cannot navigate above `root`.
- Directory entries are sorted before files.
