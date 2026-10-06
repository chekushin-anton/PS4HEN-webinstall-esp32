#!/usr/bin/env python3
"""Build board-specific flash images and firmware-selection manifests."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SKETCH_SOURCE = ROOT / "ESP32-S3-LittleFS-WebServer"
DATA_SOURCE = SKETCH_SOURCE / "data"
FLASHER = ROOT / "flasher"
FIRMWARE_OUT = FLASHER / "firmware"
MANIFEST_OUT = FLASHER / "manifests"
BOOT_APP0 = ROOT / "firmware" / "boot_app0.bin"

ESP32_PARTITION_16M = """# Name,     Type, SubType, Offset,   Size, Flags
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xe000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x200000,
app1,       app,  ota_1,   0x210000, 0x200000,
spiffs,     data, spiffs,  0x410000, 0xBE0000,
coredump,   data, coredump,0xFF0000, 0x10000,
"""

ESP32_PARTITION_4M = """# Name,     Type, SubType, Offset,  Size, Flags
nvs,        data, nvs,     0x9000,  0x5000,
otadata,    data, ota,     0xe000,  0x2000,
app0,       app,  ota_0,   0x10000, 0x180000,
spiffs,     data, spiffs,  0x190000,0x270000,
"""

BOUNDS = {
    "fw-g2allpro": {
        "page": "cache_g2allpro.html",
        "manifest": "g2allpro.manifest",
        "ranges": [
            {"min": 5.05, "max": 5.07, "label": "5.05–5.07"},
            {"min": 10.00, "max": 13.00, "label": "10.00–13.00"},
        ],
        "label": "5.05–5.07 и 10.00–13.00",
    },
    "fw-670-960": {
        "page": "cache_psfree.html",
        "manifest": "psfree.manifest",
        "ranges": [{"min": 6.70, "max": 9.60, "label": "6.70–9.60"}],
        "label": "6.70–9.60",
    },
    "fw-1302-1352": {
        "page": "cache1352.html",
        "manifest": "1352.manifest",
        "ranges": [{"min": 13.02, "max": 13.52, "label": "13.02–13.52"}],
        "label": "13.02–13.52",
    },
}


def run(args: list[str], *, cwd: Path | None = None) -> None:
    print("$ " + " ".join(str(arg) for arg in args), flush=True)
    subprocess.run(args, cwd=cwd, check=True)


def find_tool(package: str, name: str) -> Path:
    data_dir = Path(os.environ.get("ARDUINO_DIRECTORIES_DATA", Path.home() / ".arduino15"))
    tools_dir = data_dir / "packages" / package / "tools"
    matches = sorted(tools_dir.glob(f"**/{name}")) + sorted(tools_dir.glob(f"**/{name}.exe"))
    if not matches:
        raise FileNotFoundError(f"Could not find {name} under {tools_dir}; install the {package} Arduino core first")
    return matches[-1]


def compile_sketch(fqbn: str, partition_csv: str | None = None) -> dict[str, Path]:
    with tempfile.TemporaryDirectory(prefix="ps4hen-build-") as temporary:
        temp = Path(temporary)
        sketch = temp / SKETCH_SOURCE.name
        sketch.mkdir()
        shutil.copy2(SKETCH_SOURCE / f"{SKETCH_SOURCE.name}.ino", sketch / f"{SKETCH_SOURCE.name}.ino")
        if partition_csv:
            (sketch / "partitions.csv").write_text(partition_csv, encoding="utf-8")
        build_out = temp / "build"
        run(["arduino-cli", "compile", "--fqbn", fqbn, "--output-dir", str(build_out), str(sketch)])

        def one(pattern: str) -> Path:
            found = list(build_out.glob(pattern))
            if len(found) != 1:
                raise RuntimeError(f"Expected one build output matching {pattern}, found {len(found)}")
            return found[0]

        # Keep the temporary directory alive until callers have copied each result.
        result = {"firmware": one("*.ino.bin")}
        if partition_csv:
            result.update({
                "bootloader": one("*.bootloader.bin"),
                "partitions": one("*.partitions.bin"),
            })
            result["boot_app0"] = BOOT_APP0
        # Copy to the ignored build directory before the temporary directory is removed.
        return copy_build_outputs(result)


def copy_build_outputs(files: dict[str, Path]) -> dict[str, Path]:
    temporary_results = ROOT / "build" / "webinstall-results"
    temporary_results.mkdir(parents=True, exist_ok=True)
    copied: dict[str, Path] = {}
    for key, source in files.items():
        destination = temporary_results / f"{key}-{source.name}"
        shutil.copy2(source, destination)
        copied[key] = destination
    return copied


def copy_to_board_dir(files: dict[str, Path], board: str) -> dict[str, Path]:
    target_dir = FIRMWARE_OUT / board
    target_dir.mkdir(parents=True, exist_ok=True)
    result: dict[str, Path] = {}
    for key, source in files.items():
        name = {
            "firmware": "firmware.bin",
            "bootloader": "bootloader.bin",
            "partitions": "partitions.bin",
            "boot_app0": "boot_app0.bin",
        }[key]
        target = target_dir / name
        shutil.copy2(source, target)
        result[key] = target
    return result


def appcache_paths(bundle: str, stage: Path) -> None:
    profile = BOUNDS[bundle]
    for relative in ("index.html", "style.css", "ran/index.html", f"ran/{profile['page']}", f"ran/{profile['manifest']}"):
        copy_asset(relative, stage)

    manifest = DATA_SOURCE / "ran" / profile["manifest"]
    for line in manifest.read_text(encoding="utf-8").splitlines():
        entry = line.strip()
        if not entry or entry.startswith("#") or entry in {"CACHE MANIFEST", "CACHE:", "NETWORK:", "FALLBACK:", "*"}:
            continue
        source = (manifest.parent / entry).resolve()
        if not source.is_file():
            raise FileNotFoundError(f"AppCache entry {entry!r} in {manifest} is missing")
        relative = source.relative_to(DATA_SOURCE.resolve()).as_posix()
        copy_asset(relative, stage)

    router = stage / "ran" / "index.html"
    text = router.read_text(encoding="utf-8")
    ranges = json.dumps(profile["ranges"], separators=(",", ":"))
    marker = "var BUILD_FW_RANGES = null;"
    replacement = f"var BUILD_FW_RANGES = {ranges};"
    text = text.replace(marker, replacement)
    router.write_text(text, encoding="utf-8")


def copy_asset(relative: str, destination: Path) -> None:
    source = DATA_SOURCE / Path(relative)
    target = destination / Path(relative)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def make_filesystem(bundle: str, platform: str, size: int, tool: Path) -> Path:
    with tempfile.TemporaryDirectory(prefix=f"ps4hen-{bundle}-") as temporary:
        stage = Path(temporary) / "data"
        stage.mkdir()
        appcache_paths(bundle, stage)
        content_size = sum(item.stat().st_size for item in stage.rglob("*") if item.is_file())
        if content_size > size:
            raise RuntimeError(f"{bundle} needs {content_size:,} bytes; {platform} filesystem limit is {size:,} bytes")
        output_dir = FIRMWARE_OUT / "filesystems" / platform
        output_dir.mkdir(parents=True, exist_ok=True)
        image = output_dir / f"{bundle}.bin"
        run([str(tool), "-c", str(stage), "-s", hex(size), "-p", "256", "-b", "4096", str(image)])
        return image


def write_manifest(filename: str, name: str, chip: str, parts: list[dict[str, object]]) -> None:
    MANIFEST_OUT.mkdir(parents=True, exist_ok=True)
    manifest = {
        "name": name,
        "version": "2.0.0",
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": chip, "parts": parts}],
    }
    (MANIFEST_OUT / filename).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def esp32_parts(board_dir: str, filesystem: str, fs_offset: int, bootloader_offset: int = 0) -> list[dict[str, object]]:
    return [
        {"path": f"firmware/{board_dir}/bootloader.bin", "offset": bootloader_offset},
        {"path": f"firmware/{board_dir}/partitions.bin", "offset": 32768},
        {"path": f"firmware/{board_dir}/boot_app0.bin", "offset": 57344},
        {"path": f"firmware/{board_dir}/firmware.bin", "offset": 65536},
        {"path": filesystem, "offset": fs_offset},
    ]


def build() -> None:
    if not BOOT_APP0.is_file():
        raise FileNotFoundError(f"Missing ESP32 boot_app0.bin: {BOOT_APP0}")

    esp32_tool = find_tool("esp32", "mklittlefs")
    esp8266_tool = find_tool("esp8266", "mklittlefs")

    s3 = copy_to_board_dir(compile_sketch(
        "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,CDCOnBoot=default",
        ESP32_PARTITION_16M,
    ), "esp32-s3")
    write_manifest("esp32-s3-full.json", "PS4HEN — ESP32-S3 16 MB — все поддерживаемые FW", "ESP32-S3", [
        {"path": f"firmware/esp32-s3/{key}.bin", "offset": offset}
        for key, offset in (("bootloader", 0), ("partitions", 32768), ("boot_app0", 57344), ("firmware", 65536))
    ] + [{"path": "firmware/spiffs.bin", "offset": 0x410000}])
    run([str(esp32_tool), "-c", str(DATA_SOURCE), "-s", "0xBE0000", "-p", "256", "-b", "4096", str(FIRMWARE_OUT / "spiffs.bin")])

    for board, chip, fqbn in (
        ("esp32-c3", "ESP32-C3", "esp32:esp32:esp32c3:FlashSize=4M,PartitionScheme=custom,CDCOnBoot=cdc"),
        ("esp32-s2", "ESP32-S2", "esp32:esp32:esp32s2:FlashSize=4M,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc"),
    ):
        outputs = copy_to_board_dir(compile_sketch(fqbn, ESP32_PARTITION_4M), board)
        if outputs["firmware"].stat().st_size > 0x180000:
            raise RuntimeError(f"{board} firmware exceeds its 1.5 MiB app partition")
        for bundle in BOUNDS:
            image = make_filesystem(bundle, "esp32-4mb", 0x270000, esp32_tool)
            write_manifest(f"{board}-{bundle}.json", f"PS4HEN — {chip} 4 MB — {BOUNDS[bundle]['label']}", chip,
                           esp32_parts(board, f"firmware/filesystems/esp32-4mb/{bundle}.bin", 0x190000,
                                       0x1000 if chip == "ESP32-S2" else 0))

    esp8266_outputs = compile_sketch("esp8266:esp8266:generic:FlashSize=4M3M")
    esp8266_dir = FIRMWARE_OUT / "esp8266"
    esp8266_dir.mkdir(parents=True, exist_ok=True)
    firmware = esp8266_outputs["firmware"]
    if firmware.stat().st_size > 0x100000:
        raise RuntimeError(f"ESP8266 firmware is {firmware.stat().st_size:,} bytes; 4M3M layout allows 1 MiB for the app")
    shutil.copy2(firmware, esp8266_dir / "firmware.bin")
    for bundle in BOUNDS:
        image = make_filesystem(bundle, "esp8266-4mb", 0x300000, esp8266_tool)
        write_manifest(f"esp8266-{bundle}.json", f"PS4HEN — ESP8266 4 MB — {BOUNDS[bundle]['label']}", "ESP8266", [
            {"path": "firmware/esp8266/firmware.bin", "offset": 0},
            {"path": f"firmware/filesystems/esp8266-4mb/{bundle}.bin", "offset": 0x100000},
        ])


if __name__ == "__main__":
    build()
