"""The TFT's Nextion project (gui.HMI), which tells the fake display what pages and names exist.

Like the .proto files, it belongs to NSPanelManager, which builds the TFTs:

- by default, fetched from GitHub at HMI_REF and cached under build/,
- NSPM_HMI_REF=<commit or branch> fetches another revision,
- NSPM_HMI=<path> uses a local file instead, e.g. an HMI that is not pushed yet,
- NSPM_HMI_REGION=eu tests against the EU layout (default: us).

HMI_REF is pinned, so a TFT change upstream cannot break these tests until someone bumps it.
"""

import os
import urllib.request
from functools import lru_cache

from nextion_tools.hmi import Project

# NSPManager/NSPanelManager devel, "Fixed small id difference in US tft that stoped home button
# from working": the last change to either HMI.
HMI_REF = "38f74e2bbc5ef7d1431e6618f8a96fd8fe42df16"
HMI_URL = "https://raw.githubusercontent.com/NSPManager/NSPanelManager/{ref}/docker/HMI_files/tft_automation/{region}/gui.HMI"

_BUILD = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build")


def hmi_path() -> str:
    local = os.environ.get("NSPM_HMI")
    if local:
        return os.path.abspath(local)
    ref = os.environ.get("NSPM_HMI_REF", HMI_REF)
    region = os.environ.get("NSPM_HMI_REGION", "us")
    cache = os.path.join(_BUILD, "hmi", ref, region)
    path = os.path.join(cache, "gui.HMI")
    # A branch moves, so only the pinned commit is served from cache.
    if os.path.exists(path) and ref == HMI_REF:
        return path
    os.makedirs(cache, exist_ok=True)
    with urllib.request.urlopen(HMI_URL.format(ref=ref, region=region), timeout=120) as r:
        data = r.read()
    with open(path + ".tmp", "wb") as f:
        f.write(data)
    os.replace(path + ".tmp", path)
    return path


@lru_cache(maxsize=None)
def load() -> Project:
    """The project, read once per test run."""
    return Project.load(hmi_path())
