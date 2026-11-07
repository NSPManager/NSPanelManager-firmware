# Cover page HMI requirements

The firmware supports a dedicated `cover` page for Home Assistant and OpenHAB covers. This file
describes the page in the HMI/TFT. On a stock panel without the page the firmware logs a navigation
failure and returns to the entities page.

## Page

- Page name: `cover`, on its own page slot in the HMI. It does not share a page with any other
  feature: `screensaver2` stays the "screensaver without weather" page.
- Button labels are written by the firmware: `k` on `b_open`, `||` on `b_stop`, `l` on `b_close`.
  In the stock HMI's ICU arrow fonts (`arrows25`, `arrows30`, `arrows40`) `!` is left, `"` is
  right, `k` is up and `l` is down, so `b_open` and `b_close` only need one of those fonts
  assigned. The arrow fonts have no stop glyph, so `b_stop` needs a text font.
- Keep the page small and the component count low; the HMI is close to the Nextion page-RAM limit,
  so the button labels are written by the firmware when the page opens instead of being stored.
- Add the page to both the EU and US HMIs, then build every theme TFT and the mirrored variants.
- Use the usual sub-page back button named `home`.

## Components

| Name | Type | Purpose |
| --- | --- | --- |
| `current` | text | Cover name |
| `state` | text | Open / Closed / Opening / Closing / Unavailable / Unknown, followed by the position when known |
| `pos` | slider, 0–100 | Target position, 0 = closed, 100 = open |
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
- The slider is hidden when the cover does not support setting a position.
- A position requested from the slider is shown until the cover reports it or until the request
  times out, so the slider does not jump back while the motor starts.
- Tilt is not part of this version of the page.

## Building

The page is already present in both region HMIs and all 12 shipped TFTs in the manager repository.
If the HMI is edited again, compile the template TFT once in the Nextion Editor per region, swap
the theme picture records, and validate the 12 TFT checksums/orientations.
