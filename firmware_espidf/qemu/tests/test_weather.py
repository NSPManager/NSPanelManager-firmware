"""Weather on the screensaver, from the manager's retained weather update."""

import pytest

from nspm_qemu.proto import nspanel as pb


@pytest.fixture
def weather(rig):
    """A panel configured for the weather screensaver, booted and idle on it."""
    rig.manager.config.screensaver_mode = pb.NSPanelConfig.WEATHER_WITH_BACKGROUND
    rig.boot()
    rig.nextion.wait_for_page("screensaver")
    return rig


def test_weather_is_drawn_on_the_screensaver(weather):
    sent = weather.manager.publish_weather()
    n = weather.nextion
    n.wait_for_value("screensaver.curTemp.txt", sent.current_temperature_string)
    n.wait_for_value("screensaver.forWind5.txt", "10 km/h")  # the last label the update writes
    assert n.get("screensaver.forDay5.txt") == "Fri"
    assert n.get("screensaver.forDay1.txt") == "Mon"
    assert n.get("screensaver.forIcon3.txt") == "c"
    assert n.get("screensaver.curSunset.txt") == "18:20"
    assert n.get("screensaver2.curTemp.txt") == sent.current_temperature_string


@pytest.mark.display_errors_ok
@pytest.mark.xfail(strict=True, reason="GUI_SCREENSAVER_PAGE's forecast label names lack their screensaver. "
                                       "prefix, so the display rejects all 25 on any other page (also seen on a "
                                       "real panel at bkcmd=3)")
def test_weather_that_arrives_on_another_page_is_not_rejected(weather):
    weather.wake_to_home()
    n = weather.nextion
    mark = len(n.errors)
    weather.manager.publish_weather()
    n.wait_for_value("screensaver.curTemp.txt", "21°")  # prefixed: accepted on any page
    n.wait_for_command(r"^forWind5\.txt=", timeout=30)
    rejected = [text for text, _ in n.errors[mark:]]
    assert rejected == [], rejected
