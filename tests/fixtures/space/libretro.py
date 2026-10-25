"""Locate the packaged libretro cores on single- and multiarch Linux hosts."""

import os
from pathlib import Path


CORE_NAMES = {
    "gb": "gambatte_libretro.so",
    "gg": "genesis_plus_gx_libretro.so",
}


def _directories() -> list[Path]:
    configured = os.environ.get("PROTO_TIME_LIBRETRO_DIR")
    candidates = [Path(configured)] if configured else []
    candidates.extend(
        [
            Path("/usr/lib/libretro"),
            Path("/usr/local/lib/libretro"),
            *Path("/usr/lib").glob("*/libretro"),
            *Path("/usr/local/lib").glob("*/libretro"),
        ]
    )
    result = []
    for directory in candidates:
        if directory not in result:
            result.append(directory)
    return result


def reference_cores() -> dict[str, Path | None]:
    return {
        system: next(
            (directory / filename for directory in _directories() if (directory / filename).is_file()),
            None,
        )
        for system, filename in CORE_NAMES.items()
    }
