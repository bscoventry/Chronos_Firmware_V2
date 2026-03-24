# Finding `__device_dts_ord_<N>` undefined reference

When **spi00** (or another node) is **disabled** in devicetree so that nrfx uses the hardware directly, the Zephyr SPI driver does not create a device for that node. If something still references that device (e.g. `&__device_dts_ord_123`), the link fails with **undefined reference to `__device_dts_ord_123`**.

**Common cause for ordinal 123 (spi00):** A **child** of spi00 is still enabled (e.g. the nRF54L15 DK SPI NOR flash `mx25r64`). The SPI NOR / flash driver then resolves the parent bus with `DEVICE_DT_GET(spi00)`, which pulls in the reference. **Fix:** In your overlay, add `&mx25r64 { status = "disabled"; };` so no driver is created for that child and spi00 is never referenced.

## 1. Confirm which node has that ordinal

In the **build** directory, open:

- `zephyr/include/generated/devicetree_generated.h`  
  or  
- `zephyr/include/generated/devicetree_ordinals.h` (if present)

Search for the ordinal (e.g. `123`). The macros there map ordinals to node paths (e.g. `spi00`, `/soc/.../spi@4a000`). That tells you which node has no device but is still referenced.

## 2. Find which object file references the symbol

From the **repo root** (or the directory that contains `build/`):

```bash
chmod +x scripts/find_device_ord_ref.sh
./scripts/find_device_ord_ref.sh 123
```

If your build directory is not `build/` or `build/Chronos_nRF54_2`, pass it as the second argument:

```bash
./scripts/find_device_ord_ref.sh 123 build/your_build_dir
```

The script uses `nm -u` (or `llvm-nm -u`) on every `.o` in the build and prints the path of any object that has an undefined reference to `__device_dts_ord_123`. That `.o` comes from a specific `.c` file (or generated source); that source is what pulls in the reference.

## 3. Find the exact call site

- Map the `.o` path back to source:  
  - `build/zephyr/CMakeFiles/...` → often `zephyr/drivers/...` or app source.  
  - `build/CMakeFiles/app.dir/...` → app or NCS modules under `src/`.
- In that source (or in a header it includes), search for:
  - `DEVICE_DT_GET(`
  - `DEVICE_DT_GET_BY_IDX(`
  - `DEVICE_INIT_DT_GET(`
  - `PINCTRL_DT_DEV_CONFIG_GET(` (uses device name; usually not a direct device ref)
  - Any macro that takes a `node_id` and expands to one of the above.

The reference is caused by that macro being invoked with the **disabled** node (e.g. spi00), so the device was never defined.

## 4. Fix options

- **Disable the feature that does it**  
  Example: if the referencer is in pinctrl “supported”/client handling, try in `prj.conf`:
  ```text
  CONFIG_PINCTRL=n
  ```
  If the link succeeds, the reference came from pinctrl-related code. You can then either keep pinctrl off or track down the generator/driver that references disabled nodes and fix or work around it. With `CONFIG_PINCTRL=n`, drivers that use `PINCTRL_DT_*` (e.g. UART) may need an alternative; your app uses nrfx for SPIM00.

- **Fix the referencer**  
  In NCS/Zephyr (or your app), change the code or generator so it only references **status-okay** nodes (e.g. use `DT_FOREACH_STATUS_OKAY_*` or skip disabled nodes). Prefer changing your app or overlay; avoid editing Nordic SDK vendor code if possible.

- **Do not enable the Zephyr SPI driver for that node**  
  Enabling spi00 so the Zephyr SPIM driver creates the device would fix the link but would conflict with nrfx using the same hardware. So keep spi00 disabled and remove the spurious reference instead.

## 5. Grep fallback

If the script finds nothing (e.g. no `nm`), from the build directory:

```bash
grep -r "dts_ord_123\|device_dts_ord_123" zephyr --include="*.c" --include="*.h"
```

Generated or preprocessed files under `zephyr/` may show which file mentions that ordinal.
