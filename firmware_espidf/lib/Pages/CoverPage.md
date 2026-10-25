# Cover page HMI requirements

The firmware supports a dedicated `cover` page for Home Assistant and OpenHAB covers. This file
describes the page the HMI/TFT must provide. The page is not in the stock TFT yet, so on a stock
panel the firmware logs a navigation failure and returns to the entities page.

## Page

- Page name: `cover`
- Append it after the existing pages. Do not insert it before an existing page, because page IDs
  are positional in the HMI and TFT.
- Add the page to both the EU and US HMIs, then build every theme TFT and the mirrored variants.
- Use the usual sub-page back button named `home`, and the usual screensaver timer.

## Components

| Name | Type | Purpose |
| --- | --- | --- |
| `current` | text | Cover name |
| `state` | text | Open / Closed / Opening / Closing / Unavailable / Unknown |
| `position_text` | text | Position, for example `50%` |
| `position_slider` | slider, 0–100 | Target position, 0 = closed, 100 = open |
| `b_open` | button | Open (label it “Open”) |
| `b_stop` | button | Stop (label it “Stop”) |
| `b_close` | button | Close (label it “Close”) |
| `home` | button | Back to the entities page |

The three buttons must send touch press events. The slider must send touch events; firmware reads
its current value and sends the position when the event arrives, like the light page's brightness
slider.

`GUI_COVER_CONTROL_PAGE` has fallback component IDs for a page whose components are added in the
order in the table. The firmware looks the real IDs up by name when the page opens, so the HMI may
order the components differently as long as the names match.

## Behaviour

- The open/stop/close buttons are hidden when the cover does not report that capability.
- The slider and `position_text` are hidden when the cover does not support setting a position.
- A position requested from the slider is shown until the cover reports it or until the request
  times out, so the slider does not jump back while the motor starts.
- Tilt is not part of this version of the page.

## Building

The HMI page still has to be compiled once in the Nextion Editor per region. After that, use the
TFT build tooling in the manager repository to produce every theme, and run its build checks.
