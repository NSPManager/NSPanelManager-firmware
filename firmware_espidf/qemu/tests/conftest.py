import logging
import os
import random
import re
import sys
from typing import List

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from nspm_qemu.rig import DEFAULT_BUILD_DIR, Rig  # noqa: E402
from nspm_qemu.stack_manager import Stack  # noqa: E402

LOG_ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "test-logs")


def pytest_configure(config):
    config.addinivalue_line("markers", "stack: runs against an NSPanelManager dev stack (needs NSPM_STACK_MQTT)")
    logging.getLogger("broker").setLevel(logging.WARNING)
    build_dir = os.environ.get("NSPM_QEMU_BUILD_DIR", DEFAULT_BUILD_DIR)
    if not os.path.exists(os.path.join(build_dir, "firmware.bin")):
        pytest.exit("no firmware in %s; build it with `pio run -e qemu` (or use qemu/run_tests.sh)" % build_dir, 2)


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    setattr(item, "rep_" + report.when, report)


@pytest.fixture
def make_rig(request):
    """Builds fresh emulated panels, stopped at the end of the test. Each one's console log is
    kept under build/test-logs/<test>/[<n>/].

    Tests marked `stack` get panels on the dev stack named by NSPM_STACK_MQTT (see
    nspm_qemu/stack_manager.py), and are skipped without one. There every panel gets a
    random MAC and its own hostname, so it is new to the stack, and panels from earlier
    runs or other tests never collide with it."""
    name = re.sub(r"[^\w.-]", "_", request.node.nodeid.split("::", 1)[-1])
    stack = None
    if request.node.get_closest_marker("stack"):
        stack = Stack.from_env()
        if stack is None:
            pytest.skip("no dev stack; set NSPM_STACK_MQTT=host:port")
    rigs: List[Rig] = []

    def make() -> Rig:
        workdir = os.path.join(LOG_ROOT, name, str(len(rigs))) if rigs else os.path.join(LOG_ROOT, name)
        kwargs = {}
        if stack is not None:
            # Espressif's OUI, as on a real panel.
            suffix = [random.randrange(256) for _ in range(3)]
            kwargs["mac"] = "C0:49:EF:" + ":".join("%02X" % b for b in suffix)
            kwargs["hostname"] = "qemu-" + "".join("%02x" % b for b in suffix)
        rigs.append(Rig(workdir, stack=stack, **kwargs))
        return rigs[-1]

    try:
        yield make
    finally:
        failed = any(getattr(request.node, "rep_" + w, None) is not None and getattr(request.node, "rep_" + w).failed
                     for w in ("setup", "call"))
        crashed = []
        for i, r in enumerate(rigs):
            r.stop()
            label = " %d (%s)" % (i, r.mac) if len(rigs) > 1 else ""
            if failed:
                sys.stderr.write("\n---- panel%s console (last 60 lines) ----\n%s\n" % (label, r.panel.tail(60)))
                sys.stderr.write("---- display: page=%r, last commands: %r\n" % (r.nextion.page, r.nextion.commands[-15:]))
            if r.panel.crash:
                crashed.append("panel%s: %s\n%s" % (label, r.panel.crash, r.panel.tail(40)))
        # A crash the test did not wait long enough to notice is still a failure.
        if crashed and not failed:
            pytest.fail("firmware crashed: " + "\n".join(crashed))


@pytest.fixture
def rig(make_rig):
    """A fresh emulated panel, not started yet."""
    return make_rig()


@pytest.fixture
def booted(rig):
    """A panel that has registered, received config, and is idle on its screensaver."""
    rig.boot()
    return rig


@pytest.fixture
def home(booted):
    """A panel woken from its screensaver to the home page."""
    booted.wake_to_home()
    return booted
