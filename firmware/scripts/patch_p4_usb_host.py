"""Link ESP-IDF's USB hub with a TONEX-sized enumeration buffer on ESP32-P4.

Arduino-ESP32 distributes ESP-IDF's USB host as prebuilt libraries. Its
default control buffer is 256 bytes, while TONEX uses a 419-byte configuration
descriptor. This script rebuilds only hub.c from the matching ESP-IDF source
with a 1 KB limit and links it before the stock USB archive.
"""

from pathlib import Path
from urllib.request import urlopen

Import("env")

IDF_TAG = "v5.5.5"
IDF_RAW = "https://raw.githubusercontent.com/espressif/esp-idf/{}/components/usb/"
build_dir = Path(env.subst("$BUILD_DIR")) / "usb_host_patch"
build_hub = build_dir / "hub.c"
private_headers = build_dir / "private_include"


def fetch(relative_path: str) -> Path:
    """Fetch the exact upstream ESP-IDF USB source if it is not cached."""
    destination = build_dir / relative_path
    if destination.is_file():
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    url = IDF_RAW.format(IDF_TAG) + relative_path
    try:
        with urlopen(url, timeout=30) as response:
            destination.write_bytes(response.read())
    except OSError as error:
        raise RuntimeError("Could not fetch {}: {}".format(url, error))
    return destination


source_hub = fetch("hub.c")
for header in ("ext_hub.h", "ext_port.h", "hcd.h", "hub.h", "usb_private.h", "usbh.h"):
    fetch("private_include/" + header)

source = source_hub.read_text()
source = source.replace(
    '#include "sdkconfig.h"',
    '#include "sdkconfig.h"\n'
    '#undef CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE\n'
    '#define CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE 1024',
    1,
)
build_hub.write_text(source)

env.Append(CPPPATH=[str(private_headers)])
hub_object = env.Object(str(build_hub.with_suffix(".o")), str(build_hub))
env.Prepend(LINKFLAGS=[hub_object])
env.Depends(env.subst("$BUILD_DIR/${PROGNAME}.elf"), hub_object)
env.AlwaysBuild(hub_object)
env.Default(hub_object)
